/////////////////////////////////////////////////////////////////////////////
// Handle attaching files to outgoing messages
// Bobbi July 2020
/////////////////////////////////////////////////////////////////////////////

#include <conio.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "email_common.h"

#define PROMPT_ROW 23

#define EOL       '\r'

#define BELL      0x07
#define BACKSPACE 0x08
#define RETURN    0x0d
#define NORMAL    0x0e
#define INVERSE   0x0f
#define ESC       0x1b
#define DELETE    0x7f

#define NETBUFSZ  1500
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]
#define IOBUFSZ   4096

unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
char          linebuf[LINEBUFSZ];
char          userentry[80];
uint8_t       quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit
char          filename[80];
char          iobuf[IOBUFSZ];

struct attachinfo {
  char     filename[16];
  uint32_t size;
  struct attachinfo *next;
};

struct attachinfo *attachments = NULL;

#define ERR_NONFATAL 0
#define ERR_FATAL    1

/*
 * Annoying beep
 */
void beep(void) {
  uint8_t *p = (uint8_t*)0xc030; // Speaker
  uint8_t junk;
  uint16_t i;
  for (i = 0; i < 200; ++i) {
    junk = *p;
    for (junk = 0; junk < 50; ++junk); // Reduce pitch
  }
}

/*
 * Clear to EOL
 */
void clreol(void) {
  uint8_t x = wherex(), y = wherey();
  cclear(80 - x);
  gotoxy(x, y);
}

/*
 * Clear line
 */
void clrline(void) {
  uint8_t x = wherex();
  gotox(0);
  cclear(80);
  gotox(x);
}

/*
 * Put cursor at beginning of PROMPT_ROW
 */
void goto_prompt_row(void) {
  gotoxy(0, PROMPT_ROW);
}

/*
 * Show error messages
 */
void error(uint8_t fatal, const char *fmt, ...) {
  va_list v;
  if (fatal) {
    videomode(VIDEOMODE_80COL);
    clrscr();
    printf("\n\n%cFATAL ERROR:%c\n\n", INVERSE, NORMAL);
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf("\n\n\n\n[Press Any Key To Quit]");
    cgetc();
    exit(1);
  } else {
    putchar('\r');
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf(" - [Press Any Key]");
    cgetc();
    putchar('\r');
  }
}

/*
 * Spinner while encoding attachments
 */
void spinner(uint32_t sz, uint8_t final) {
  static char chars[] = "|/-\\";
  static char buf[10] = "";
  static uint8_t i = 0;
  uint8_t j;
  for (j = 0; j < strlen(buf); ++j)
    putchar(BACKSPACE);
  if (final) {
    sprintf(buf, " [%lu]\n", sz);
    printf("%s", buf);
    strcpy(buf, "");
  }
  else {
    sprintf(buf, "%c %lu", chars[(i++) % 4], sz);
    printf("%s", buf);
  }
}

/*
 * Read parms from EMAIL.CFG
 */
void readconfigfile(void) {
  FILE *fp = fopen("EMAIL.CFG", "r");
  if (!fp)
    error(ERR_FATAL, "Can't open config file EMAIL.CFG");
  fscanf(fp, "%s", cfg_server);
  fscanf(fp, "%s", cfg_user);
  fscanf(fp, "%s", cfg_pass);
  fscanf(fp, "%s", cfg_pop_delete);
  fscanf(fp, "%s", cfg_smtp_server);
  fscanf(fp, "%s", cfg_smtp_domain);
  fscanf(fp, "%s", cfg_instdir);
  fscanf(fp, "%s", cfg_emaildir);
  fscanf(fp, "%s", cfg_emailaddr);
  fclose(fp);
}

/*
 * Read a text file a line at a time
 * Returns number of chars in the line, or 0 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * writep - Pointer to buffer into which line will be written
 * n - length of buffer. Longer lines will be truncated and terminated with CR.
 */
uint16_t get_line(FILE *fp, char *writep, uint16_t n) {
  static uint16_t rd = 0; // Read
  static uint16_t end = 0; // End of valid data in buf
  uint16_t i = 0;
  while (1) {
    if (rd == end) {
      end = fread(buf, 1, READSZ, fp);
      rd = 0;
    }
    if (end == 0)
      goto done;
    if (i == n - 1) {
      writep[i - 1] = '\r';
      goto done;
    }
    writep[i++] = buf[rd++];
    if (writep[i - 1] == '\r')
      goto done;
  }
done:
  writep[i] = '\0';
  return i;
}

/*
 * Base64 encode table
 */
static const char b64enc[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Encode Base64 format
 * p - Pointer to source buffer
 * q - Pointer to destination buffer
 * len - Length of buffer to encode
 * Returns length of encoded data
 */
uint16_t encode_base64(char *p, char *q, uint16_t len) {
  uint16_t j = 0;
  uint16_t i, ii;
  for (i = 0; i < len / 3; ++i) {
    ii = 3 * i;
    q[j++] = b64enc[(p[ii] & 0xfc) >> 2];
    q[j++] = b64enc[((p[ii] & 0x03) << 4) | ((p[ii + 1] & 0xf0) >> 4)];
    q[j++] = b64enc[((p[ii + 1] & 0x0f) << 2) | ((p[ii + 2] & 0xc0) >> 6)];
    q[j++] = b64enc[(p[ii + 2] & 0x3f)];
    if (((i + 1) % 18) == 0)
      q[j++] = '\r';
  }
  ii += 3;
  i = len - ii; // Bytes remaining to encode
  switch (i) {
  case 1:
    q[j++] = b64enc[(p[ii] & 0xfc) >> 2];
    q[j++] = b64enc[(p[ii] & 0x03) << 4];
    q[j++] = '=';
    q[j++] = '=';
    break;
  case 2:
    q[j++] = b64enc[(p[ii] & 0xfc) >> 2];
    q[j++] = b64enc[((p[ii] & 0x03) << 4) | ((p[ii + 1] & 0xf0) >> 4)];
    q[j++] = b64enc[(p[ii + 1] & 0x0f) << 2];
    q[j++] = '=';
    break;
  }
  q[j] = '\0';
  return j;
}

/*
 * Prompt for a name in the bottom line of the screen
 * Returns number of chars read.
 * prompt - Message to display before > prompt
 * is_file - if 1, restrict chars to those allowed in ProDOS filename
 * Returns number of chars read, or 255 if ESC pressed
 */
uint8_t prompt_for_name(char *prompt, uint8_t is_file) {
  uint16_t i;
  char c;
  cursor(0);
  goto_prompt_row();
  clreol();
  revers(1);
  cprintf("%s>", prompt);
  revers(0);
  gotox(2 + strlen(prompt));
  i = 0;
  while (1) {
    c = cgetc();
    if (is_file && !isalnum(c) && (c != RETURN) && (c != BACKSPACE) &&
        (c != DELETE) && (c != ESC) && (c != '.') && (c != '/')) {
      beep();
      continue;
    }
    switch (c) {
    case RETURN:
      goto done;
    case BACKSPACE:
    case DELETE:
      if (i > 0) {
        gotox(wherex() - 1);
        cputc(' ');
        gotox(wherex() - 1);
        --i;
      } else
        beep();
      break;
    case ESC:
      userentry[0] = '\0';
      i = 255;
      goto esc_pressed;
    default:
      cputc(c);
      userentry[i++] = c;
    }
    if (i == 79)
      goto done;
  }
done:
  userentry[i] = '\0';
esc_pressed:
  clrline();
  cursor(1);
  return i;
}

/*
 * Prompt ok?
 * Returns 0 for yes
 *         1 for no
 *         2 for ESC
 */
char prompt_okay(char *msg) {
  char c;
  cursor(0);
  goto_prompt_row();
  clreol();
  revers(1);
  cprintf("%s (y/n/ESC)", msg);
  revers(0);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N') || c == ESC)
      break;
    beep();
  }
  if ((c == 'y') || (c == 'Y'))
    c = 0;
  else if (c == ESC)
    c = 2;
  else
    c = 1;
  return c;
}

struct tabent {
  char     name[16];
  uint8_t  type;
  uint32_t size;
} *entry;

// It is best if FILESPERPAGE is a multiple of FILELINES
#define FILELINES 16       // Number of lines displayed in file_ui
#define FILESPERPAGE 160   // Number of files loaded into iobuf[]

/*
 * Draw one line in file chooser UI
 * i - index of file to draw
 * first - index of first file on screen
 * selected - index of currently selected file
 * entries - total number of file entries in directory
 */
void file_ui_draw(uint16_t i, uint16_t first, uint16_t selected, uint16_t entries) {
  struct tabent *entry;
  gotoxy(5, i - first + 6);
  if (i < entries) {
    entry = (struct tabent*)iobuf + i;
    if (entry->type == 0x0f) {
      snprintf(userentry, 80, "[ %s ]                               ", entry->name);
      userentry[34] = '\0';
    } else {
      snprintf(userentry, 80, "  %s                   ", entry->name);
      snprintf(&userentry[18], 60, "  %8lu  ", entry->size);
      switch (entry->type) {
      case 0x04:
        sprintf(&userentry[30], "TXT ");
        break;
      case 0x06:
        sprintf(&userentry[30], "BIN ");
        break;
      case 0x19:
        sprintf(&userentry[30], "ADB ");
        break;
      case 0x1a:
        sprintf(&userentry[30], "AWP ");
        break;
      case 0x1b:
        sprintf(&userentry[30], "ASP ");
        break;
      case 0xfc:
        sprintf(&userentry[30], "BAS ");
        break;
      case 0xff:
        sprintf(&userentry[30], "SYS ");
        break;
      default:
        sprintf(&userentry[30], "$%02x ", entry->type);
      }
    }
    if (i == selected)
      revers(1);
    cputs(userentry);
    if (i == selected)
      revers(0);
  } else {
    strcpy(userentry, "                                  ");
    cputs(userentry);
  }
}

/*
 * File chooser UI
 * first - index of first file on screen
 * selected - index of currently selected file
 * entries - total number of file entries in directory
 */
void file_ui_draw_all(uint16_t first, uint16_t selected, uint16_t entries) {
  uint16_t i;
  uint16_t last = first + FILELINES;
  for (i = first; i < last; ++i)
    file_ui_draw(i, first, selected, entries);
}

/*
 * Perform ProDOS MLI ON_LINE call to
 * write all online volume names into iobuf[]
 * Return the number of entries
 */
uint16_t online(void) {
  uint16_t entries = 0;
  struct tabent *entry;
  uint8_t i, j, len;
  __asm__("lda #$00"); // All devices
  __asm__("sta mliparam + 1");
  __asm__("lda #<%v", iobuf); // iobuf LSB
  __asm__("sta mliparam + 2");
  __asm__("lda #>%v", iobuf); // iobuf MSB
  __asm__("ina");             // Bump up 256 bytes into iobuf
  __asm__("sta mliparam + 3");
  __asm__("lda #$c5"); // ON_LINE
  __asm__("ldx #$02"); // Two parms
  __asm__("jsr callmli");
  entry = (struct tabent*)iobuf;
  for (i = 0; i < 16; ++i) {
    len = iobuf[256 + i * 16] & 0x0f;
    if (len > 0) {
      entry->type = 0x0f;
      entry->name[0] = '/';
      for (j = 0; j < len; ++j)
        entry->name[j + 1] = iobuf[256 + i * 16 + j + 1];
      entry->name[j + 1] = '\0';
      ++entry;
      ++entries;
    }
  }
  return entries;
}

/*
 * Read directory entries and populate table in iobuf[]
 * for file_ui()
 * page - page index, each page has FILESPERPAGE entries
 *        page is 0, 1, 2, 3 ...
 * num_entries - number of entries read
 * Returns 0 there are more files, 1 if end of directory reached
 */
uint8_t read_dir(uint8_t page, uint16_t *num_entries) {
  DIR *dp;
  struct dirent *ent;
  struct tabent *entry;
  uint8_t rc = 0;
  uint16_t entries = 0, i = 0;
  entry = (struct tabent*)iobuf;
  if (page == 0) {
    strcpy(entry->name, ".."); // Add fake '..' entry
    entry->type = 0x0f;
    ++entry;
    ++entries;
  }
  dp = opendir(".");
  while (1) {
    ent = readdir(dp);
    if (!ent) {
      rc = 1;
      break;
    }
    if (++i < page * FILESPERPAGE)
      continue;
    if (entries >= FILESPERPAGE)
      break;
    memcpy(entry->name, ent->d_name, 16);
    entry->type = ent->d_type;
    entry->size = ent->d_size;
    ++entry;
    ++entries;
  }
  closedir(dp);
  *num_entries = entries;
  return rc;
}

/*
 * File chooser UI
 * Leaves file name in userentry[], or empty string if error/cancel
 * msg1 - Message for top line
 * msg2 - Message for second line
 * msg3 - Message for third line
 */
void file_ui(char *msg1, char *msg2, char *msg3) {
  struct tabent *entry;
  char c;
  uint16_t entries;
  uint8_t end_of_dir;
  uint16_t first = 0, current = 0;
  uint8_t toplevel = 0, page = 0;
restart:
  clrscr();
  cursor(0);
  gotoxy(0,0);
  revers(1);
  cprintf("%s", msg1);
  revers(0);
  gotoxy(0,1);
  cprintf("%s", msg2);
  gotoxy(0,2);
  cprintf("%s", msg3);
  getcwd(userentry, 80);
  gotoxy(0,4);
  revers(1);
  cprintf("%s", (toplevel ? "Volumes" : userentry));
  revers(0);
  if (toplevel)
    entries = online();
  else
    end_of_dir = read_dir(page, &entries);
redraw:
  file_ui_draw_all(first, current, entries);
  while (1) {
    c = cgetc();
    switch (c) {
    case 0x0b:  // Up
      if (current > 0)
        --current;
      else
        if (page > 0) {
          --page;
          current = FILESPERPAGE - 1;
          first = current - FILELINES + 1;
          goto restart;
        }
      if (current < first) {
        if (first > FILELINES)
          first -= FILELINES;
        else
          first = 0;
        goto redraw;
      }
      file_ui_draw(current, first, current, entries);
      file_ui_draw(current + 1, first, current, entries);
      break;
    case 0x0a:  // Down
      if (current < entries - 1)
        ++current;
      else if (!end_of_dir) {
        ++page;
        first = current = 0;
        goto restart;
      }
      if (current >= first + FILELINES) {
        first += FILELINES;
        goto redraw;
      }
      file_ui_draw(current - 1, first, current, entries);
      file_ui_draw(current, first, current, entries);
      break;
    case EOL:
      entry = (struct tabent*)iobuf + current;
      switch (entry->type) {
      case 0x0f: // Directory
        getcwd(userentry, 80);
        if (strcmp(entry->name, "..") == 0) {
          for (c = strlen(userentry); c > 0; --c)
            if (userentry[c] == '/') {
              userentry[c] = '\0';
              break;
            }
          if (c <= 1)
            toplevel = 1;
          else
            chdir(userentry);
          first = current = 0;
          goto restart;
        } else {
          if (toplevel) {
            strcpy(userentry, entry->name);
            chdir(userentry);
          } else {
            getcwd(userentry, 80);
            strcat(userentry, "/");
            strcat(userentry, entry->name);
            chdir(userentry);
          }
          toplevel = 0;
          first = current = 0;
          goto restart;
        }
        break;
      case 0x04: // ASCII text
        getcwd(userentry, 80);
        strcat(userentry, "/");
        strcat(userentry, entry->name);
        goto done;
        break;
      default:
        getcwd(userentry, 80);
        strcat(userentry, "/");
        strcat(userentry, entry->name);
        goto done;
      }
      break;
    case ESC:
      strcpy(userentry, "");
      goto done;
      break;
    case 0x09: // Tab
      if (prompt_for_name("Enter filename", 0) == 255)
        goto restart; // ESC pressed
      if (userentry[0] == '/')    // Absolute path
        goto done;
      getcwd(linebuf, 80);       // Otherwise relative path
      strcat(linebuf, "/");
      strcat(linebuf, userentry);
      strcpy(userentry, linebuf);
      strcpy(linebuf, "");
      goto done;
    }
  }
done:
  clrscr();
  cursor(1);
}

/*
 * Optionally attach files to outgoing email.
 * filename - Name of file containing email message
 */
void attach(char *fname) {
  FILE *fp, *fp2, *destfp;
  uint16_t chars, i;
  uint32_t size;
  char *s, c;
  struct attachinfo *a;
  struct attachinfo *latest = NULL;
  uint8_t attachcount = 0;
  videomode(VIDEOMODE_80COL);
  printf("%c%s ATTACHER%c\n\n", 0x0f, PROGNAME, 0x0e);
  fp = fopen(fname, "rb+");
  if (!fp)
    error(ERR_FATAL, "Can't open %s", fname);
  snprintf(filename, 80, "%s/OUTBOX/TMPFILE", cfg_emaildir);
  destfp = fopen(filename, "wb");
  if (!destfp)
    error(ERR_FATAL, "Can't open TMPFILE");

  printf("  Copying email content ...  "); // Space is for spinner to eat
  size = 0;
  while ((chars = get_line(fp, linebuf, LINEBUFSZ)) != 0) {
    size += chars;
    if (linebuf[0] == '\r')
      break;
    fputs(linebuf, destfp);
    spinner(size, 0);
  }
  fprintf(destfp, "MIME-Version: 1.0\r");
  fprintf(destfp, "Content-Type: multipart/mixed; boundary=a2forever\r\r");
  fprintf(destfp, "This is a multi-part message in MIME format.\r");
  fprintf(destfp, "--a2forever\r");
  fprintf(destfp, "Content-Type: text/plain; charset=US-ASCII\r");
  fprintf(destfp, "Content-Transfer-Encoding: 7bit\r\r");
  while ((chars = get_line(fp, linebuf, LINEBUFSZ)) != 0) {
    size += chars;
    fputs(linebuf, destfp);
    spinner(size, 0);
  }
  spinner(size, 1);

  while (1) {
    cursor(0);
    if (attachcount == 1)
      printf("\n  There is currently 1 attachment.\n\n", attachcount);
    else
      printf("\n  There are currently %u attachments.\n\n", attachcount);
    a = attachments;
    i = 1;
    if (attachcount > 0)
      printf("  #   Bytes  Filename\n");
    while (a) {
      if (i == 13) {
        printf(" < ... More Attachments, Not Shown ... >\n");
        break;
      }
      printf("%3d %7lu  %s\n", i++, a->size, a->filename);
      a = a->next;
    }
    gotoxy(0, 21);
    printf("%c A)dd attachment | D)one with attachments |                                     %c", INVERSE, NORMAL);
ask:
    c = cgetc();
    if ((c == 'D') || (c == 'd'))
      goto done;
    if ((c != 'A') && (c != 'a')) {
      beep();
      goto ask;
    }
    snprintf(userentry, 80, "Attachment #%u : Select a File to Attach", attachcount);
    file_ui(userentry,
            "",
            " Select file from tree browser, or [Tab] to enter filename. [Esc] cancels.");
    printf("%c%s ATTACHER%c\n\n", 0x0f, PROGNAME, 0x0e);
    if (strlen(userentry) == 0) {
      beep();
      printf("  ** No file was selected!\n"); 
      continue;
    }
    s = strrchr(userentry, '/');
    if (!s)
      s = userentry;
    else
      s = s + 1; // Character after the slash
    if (strlen(s) == 0) {
      beep();
      sprintf(linebuf, "  ** Illegal trailing slash '%s'!\n", userentry);
      printf(linebuf);
      continue;
    }
    fp2 = fopen(userentry, "rb");
    if (!fp2) {
      beep();
      sprintf(linebuf, "  ** Can't open '%s'!\n", userentry);
      printf(linebuf);
      continue;
    }
    fprintf(destfp, "\r--a2forever\r");
    fprintf(destfp, "Content-Type: application/octet-stream\r");
    fprintf(destfp, "Content-Transfer-Encoding: base64\r");
    fprintf(destfp, "Content-Disposition: attachment; filename=%s;\r\r", s);
    printf("  Attaching '%s' ...  ", userentry); // Space is for spinner to eat
    size = 0;
    do {
      i = fread(buf, 1, 72 * 3 / 4 * 5, fp2); // Multiple of 72*3/4 bytes
      size += i;
      if (i == 0)
        break;
      i = encode_base64(buf, buf + READSZ / 2, i);
      i = fwrite(buf + READSZ / 2, 1, i, destfp);
      spinner(size, 0);
    } while (!feof(fp2));
    fclose(fp2);
    spinner(size, 1);
    ++attachcount;
    if (!attachments)
      attachments = latest = malloc(sizeof(struct attachinfo));
    else {
      latest->next = malloc(sizeof(struct attachinfo));
      latest = latest->next;
    }
    strcpy(latest->filename, userentry);
    latest->size = size;
    latest->next = NULL;
  }
done:
  fprintf(destfp, "\r--a2forever--\r");
  fclose(fp);
  fclose(destfp);
  if (unlink(fname))
    error(ERR_FATAL, "Can't delete %s", fname);
  if (rename(filename, fname))
    error(ERR_FATAL, "Can't rename %s to %s", filename, fname);
}

/*
 * Exec EMAIL.SYSTEM
 */
void load_email(void) {
  revers(0);
  clrscr();
  chdir(cfg_instdir);
  exec("EMAIL.SYSTEM", NULL);
}

void main(int argc, char *argv[]) {
  readconfigfile();
  if (argc == 2) {
    quit_to_email = 1;
    attach(argv[1]);
  } else
    error(ERR_FATAL, "No email file specified");
  if (quit_to_email)
    load_email();
}


