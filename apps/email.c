/////////////////////////////////////////////////////////////////
// emai//er - Simple Email User Agent vaguely inspired by Elm
// Handles INBOX in the format created by POP65
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// - TODO: Default to starting at end, not beginning (or option to sort backwards...)
// - TODO: Fix terrible scrollback algorithm!!
// - TODO: Editor for email composition functions

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <conio.h>
#include <string.h>
#include <ctype.h>
#include <apple2_filetype.h>

#define EMAIL_C
#include "email_common.h"

// Program constants
#define MSGS_PER_PAGE 18     // Number of messages shown on summary screen
#define MENU_ROW      22     // Row that the menu appears on
#define PROMPT_ROW    24     // Row that data entry prompt appears on
#define SCROLLBACK    25*80  // How many bytes to go back when paging up
#define READSZ        1024   // Size of buffer for copying files

// Characters
#define BELL          0x07
#define BACKSPACE     0x08
#define INVERSE       0x0f
#define DOWNARROW     0x0a
#define UPARROW       0x0b
#define RETURN        0x0d
#define NORMAL        0x0e
#define CURDOWN       0x0a
#define HOME          0x19
#define CLRLINE       0x1a
#define CURUP         0x1f
#define DELETE        0x7f

// Addresses
#define CURSORROW     0x0025
#define SYSTEMTIME    0xbf90

/*
 * Represents a date and time
 */
struct datetime {
    unsigned int  year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char ispd25format;
    unsigned char nodatetime;
};

char                  filename[80];
char                  userentry[80];
char                  linebuf[998+2]; // According to RFC2822 Section 2.1.1 (998+CRLF)
FILE                  *fp;
struct emailhdrs      *headers;
uint16_t              selection, prevselection;
uint16_t              num_msgs;    // Num of msgs shown in current page
uint16_t              total_msgs;  // Total number of message in mailbox
uint16_t              total_new;   // Total number of new messages
uint16_t              total_tag;   // Total number of tagged messages
uint16_t              first_msg;   // Msg numr: first message current page
char                  curr_mbox[80] = "INBOX";
static unsigned char  buf[READSZ];

#define ERR_NONFATAL 0
#define ERR_FATAL    1

/*
 * Put cursor at beginning of PROMPT_ROW
 */
void goto_prompt_row(void) {
  uint8_t i;
  putchar(HOME);
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(CURDOWN);
}

/*
 * Show non fatal error in PROMPT_ROW
 * Fatal errors are shown on a blank screen
 */
void error(uint8_t fatal, const char *fmt, ...) {
  va_list v;
  if (fatal) {
    clrscr();
    printf("\n\n%cFATAL ERROR:%c\n\n", INVERSE, NORMAL);
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf("\n\n\n\n[Press Any Key To Quit]");
    cgetc();
    exit(1);
  } else {
    goto_prompt_row();
    putchar(CLRLINE);
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf(" - [Press Any Key]");
    cgetc();
    putchar(CLRLINE);
  }
}

/*
 * Busy spinner
 */
void spinner(void) {
  static char chars[] = "|/-\\";
  static uint8_t i = 0;
  putchar(BACKSPACE);
  putchar(chars[(i++) % 4]);
}

/*
 * Read parms from POP65.CFG
 */
void readconfigfile(void) {
  fp = fopen("POP65.CFG", "r");
  if (!fp)
    error(ERR_FATAL, "Can't open config file POP65.CFG");
  fscanf(fp, "%s", cfg_server);
  fscanf(fp, "%s", cfg_user);
  fscanf(fp, "%s", cfg_pass);
  fscanf(fp, "%s", cfg_pop_delete);
  fscanf(fp, "%s", cfg_smtp_server);
  fscanf(fp, "%s", cfg_smtp_domain);
  fscanf(fp, "%s", cfg_emaildir);
  fscanf(fp, "%s", cfg_emailaddr);
  fclose(fp);
}

void readdatetime(unsigned char time[4], struct datetime *dt) {
    unsigned int d = time[0] + 256U * time[1];
    unsigned int t = time[2] + 256U * time[3];
    if ((d == 0) && (t == 0)) {
        dt->nodatetime = 1;
        return;
    }
    dt->nodatetime = 0;
    if (!(t & 0xe000)) {
        /* ProDOS 1.0 to 2.4.2 date format */
        dt->year   = (d & 0xfe00) >> 9;
        dt->month  = (d & 0x01e0) >> 5;
        dt->day    = d & 0x001f;
        dt->hour   = (t & 0x1f00) >> 8;
        dt->minute = t & 0x003f;
        dt->ispd25format = 0;
        if (dt->year < 40) /* See ProDOS-8 Tech Note 48 */
            dt->year += 2000;
        else
            dt->year += 1900;
    } else {
        /* ProDOS 2.5.0+ */
        dt->year   = t & 0x0fff;
        dt->month  = ((t & 0xf000) >> 12) - 1;
        dt->day    = (d & 0xf800) >> 11;
        dt->hour   = (d & 0x07c0) >> 6;
        dt->minute = d & 0x003f;
        dt->ispd25format = 1;
    }
}

/*
 * Print a date/time value in short format for the status bar
 */
void printdatetime(struct datetime *dt) {
  if (dt->nodatetime)
    fputs("????-??-?? ??:??", stdout);
  else {
    printf("%04d-%02d-%02d %02d:%02d",
           dt->year, dt->month, dt->day, dt->hour, dt->minute);
  }
}

/*
 * Format a date/time in format suitable for an email
 * dt - structure representing date/time
 * s  - result is returned through this pointer
 */
void datetimelong(struct datetime *dt, char *s) {
  static char *months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (dt->nodatetime)
    sprintf(s, "No system clock");
  else {
    // eg: "20 Jun 2020 hh:mm:00"
    sprintf(s, "%2d %s %04d %02d:%02d:00",
            dt->day, months[dt->month], dt->year, dt->hour, dt->minute);
  }
}

/*
 * Obtain the system date and time for the status bar
 */
void printsystemdate(void) {
  struct datetime dt;
  readdatetime((unsigned char*)(SYSTEMTIME), &dt);
  printdatetime(&dt);
}

/*
 * Free linked list rooted at headers
 */
void free_headers_list(void) {
  struct emailhdrs *h = headers;
  while (h) {
    free(h);
    h = h->next; // Not strictly legal, but will work
  }
  headers = NULL;
}

/*
 * Read EMAIL.DB and populate linked list rooted at headers
 * startnum - number of the first message to load (1 is the first)
 * initialize - if 1, then total_new and total_msgs are calculated
 * switchmbox - if 1, then errors are treated as non-fatal (for S)witch command)
 * Returns 0 if okay, 1 on non-fatal error.
 */
uint8_t read_email_db(uint16_t startnum, uint8_t initialize, uint8_t switchmbox) {
  struct emailhdrs *curr = NULL, *prev = NULL;
  uint16_t count = 0;
  uint16_t l;
  if (initialize) {
    total_new = total_msgs = total_tag = 0;
  }
  free_headers_list();
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't open %s", filename);
    if (switchmbox)
      return 1;
  }
  if (fseek(fp, (startnum - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
    error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
    if (switchmbox)
      return 1;
  }
  num_msgs = 0;
  while (1) {
    curr = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
    if (!curr)
      error(ERR_FATAL, "Can't malloc()");
    curr->next = NULL;
    curr->tag = ' ';
    l = fread(curr, 1, EMAILHDRS_SZ_ON_DISK, fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK) {
      free(curr);
      fclose(fp);
      return 0;
    }
    if (count <= MSGS_PER_PAGE) {
      if (!prev)
        headers = curr;
      else
        prev->next = curr;
      prev = curr;
      ++num_msgs;
    } else
      if (!initialize) {
        fclose(fp);
        return 0;
      }
    if (initialize) {
      ++total_msgs;
      if (curr->status == 'N')
        ++total_new;
      if (curr->tag == 'T')
        ++total_tag;
    }
  }
  fclose(fp);
  return 0;
}

/*
 * Print a header field from char postion start to end,
 * padding with spaces as needed
 */
void printfield(char *s, uint8_t start, uint8_t end) {
  uint8_t i;
  uint8_t l = strlen(s);
  for (i = start; i < end; i++)
    putchar(i < l ? s[i] : ' ');
}

/*
 * Print one line summary of email headers for one message
 */
void print_one_email_summary(struct emailhdrs *h, uint8_t inverse) {
  putchar(inverse ? INVERSE : NORMAL);
  putchar(h->tag == 'T' ? 'T' : ' ');
  switch(h->status) {
  case 'N':
    putchar('*'); // New
    break;
  case 'R':
    putchar(' '); // Read
    break;
  case 'D':
    putchar('D'); // Deleted
    break;
  }
  //printf("%02d|", h->emailnum);
  putchar('|');
  printfield(h->date, 0, 16);
  putchar('|');
  printfield(h->from, 0, 20);
  putchar('|');
  printfield(h->subject, 0, 39);
  //putchar('\r');
  putchar(NORMAL);
}

/*
 * Get emailhdrs for nth email in list of headers
 */
struct emailhdrs *get_headers(uint16_t n) {
  uint16_t i = 1;
  struct emailhdrs *h = headers;
  while (h && (i < n)) {
    ++i;
    h = h->next;
  }
  return h;
}

/*
 * Print status bar at the top
 */
void status_bar(void) {
  putchar(HOME);
  putchar(INVERSE);
  fputs("                                                                ", stdout);
  printsystemdate();
  putchar(HOME);
  if (num_msgs == 0)
    printf("%c%s [%s] No messages ", INVERSE, PROGNAME, curr_mbox);
  else
    printf("%c[%s] %u msgs, %u new, %u tagged. Showing %u-%u. ",
           INVERSE, curr_mbox, total_msgs, total_new, total_tag, first_msg,
           first_msg + num_msgs - 1);
  putchar(NORMAL);
  putchar(CURDOWN);
  printf("\n");
}

/*
 * Show email summary
 */
void email_summary(void) {
  uint8_t i = 1;
  struct emailhdrs *h = headers;
  clrscr();
  status_bar();
  while (h) {
    print_one_email_summary(h, (i == selection));
    ++i;
    h = h->next;
  }
  putchar(HOME);
  for (i = 0; i < MENU_ROW - 1; ++i) 
    putchar(CURDOWN);
  printf("%cUp/K Prev | SPC/RET Read | A)rchive | C)opy | M)ove  | D)el   | U)ndel | P)urge %c", INVERSE, NORMAL);
  printf("%cDn/J Next | S)witch mbox | N)ew mbox| T)ag  | W)rite | R)eply | F)wd   | Q)uit  %c", INVERSE, NORMAL);
}

/*
 * Show email summary for nth email message in list of headers
 */
void email_summary_for(uint16_t n) {
  struct emailhdrs *h = headers;
  uint16_t j;
  h = get_headers(n);
  putchar(HOME);
  for (j = 0; j < n + 1; ++j) 
    putchar(CURDOWN);
  print_one_email_summary(h, (n == selection));
}

/*
 * Move the highlight bar when user selects different message
 */
void update_highlighted(void) {
  email_summary_for(prevselection);
  email_summary_for(selection);
}

/*
 * Read a text file a line at a time leaving the line in linebuf[]
 * Returns number of chars in the line, or -1 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * reset - if 1 then just reset the buffer and return
 */
int16_t get_line(FILE *fp, uint8_t reset, uint32_t *pos) {
  static uint16_t rd = 0;
  static uint16_t buflen = 0;
  uint8_t found = 0;
  uint16_t j = 0;
  uint16_t i;
  if (reset) {
    rd = buflen = 0;
    *pos = 0;
    return 0;
  }
  while (1) {
    for (i = rd; i < buflen; ++i) {
      linebuf[j++] = buf[i];
      ++(*pos);
      if (linebuf[j - 1] == '\r') {
        found = 1;
        break;
      }
    }
    if (found) {
      rd = i + 1;
      linebuf[j] = '\0';
      return j;
    }
    buflen = fread(buf, 1, READSZ, fp);
    if (buflen == 0) {
      rd = 0;
      return -1; // Hit EOF before we found EOL
    }
    rd = 0;
  }
}

/*
 * Convert hex char to value
 */
uint8_t hexdigit(char c) {
  if ((c >= '0') && (c <= '9'))
    return c - '0';
  else
    return c - 'A' + 10;
}

/*
 * Decode linebuf[] from quoted-printable format and print on screen
 */
void decode_quoted_printable(FILE *fp, uint8_t binary) {
  uint16_t i = 0;
  char c;
  if (!fp) {
    if (binary)
      return;
    fp = stdout;
  }
  while (c = linebuf[i]) {
    if (c == '=') {
      if (linebuf[i + 1] == '\r') // Trailing '=' is a soft EOL
        return;
      // Otherwise '=xx' where x is a hex digit
      c = 16 * hexdigit(linebuf[i + 1]) + hexdigit(linebuf[i + 2]);
      if ((c >= 0x20) && (c <= 0x7e))
        fputc(c, fp);
      i += 3;
    } else {
      fputc(c, fp);
      ++i;
    }
  }
}

/*
 * Base64 decode table
 */
static const int16_t b64dec[] =
  {62,-1,-1,-1,63,52,53,54,55,56,
   57,58,59,60,61,-1,-1,-1,-2,-1,
   -1,-1, 0, 1, 2, 3, 4, 5, 6, 7,
    8, 9,10,11,12,13,14,15,16,17,
   18,19,20,21,22,23,24,25,-1,-1,
   -1,-1,-1,-1,26,27,28,29,30,31,
   32,33,34,35,36,37,38,39,40,41,
   42,43,44,45,46,47,48,49,50,51};

/*
 * Decode linebuf[] from Base64 format and print on screen
 * Each line of base64 has up to 76 chars
 * TODO This is hideously slow!!
 */
void decode_base64(FILE *fp, uint8_t binary) {
  uint16_t i = 0;
  while (linebuf[i] != '\r') {
    fputc(b64dec[linebuf[i] - 43] << 2 | b64dec[linebuf[i + 1] - 43] >> 4, fp);
    if (linebuf[i + 2] != '=')
      fputc(b64dec[linebuf[i + 1] - 43] << 4 | b64dec[linebuf[i + 2] - 43] >> 2, fp);
    if (linebuf[i + 3] != '=')
      fputc(b64dec[linebuf[i + 2] - 43] << 6 | b64dec[linebuf[i + 3] - 43], fp);
    i += 4;
  }
}

/*
 * OK to d/l attachment?
 */
char prompt_okay_attachment(char *filename) {
  char c;
  printf("Okay to download %s? (y/n) >", filename);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N'))
      break;
    putchar(BELL);
  } 
  putchar(RETURN); // Go to col 0
  putchar(CURUP);
  putchar(CLRLINE);
  if ((c == 'y') || (c == 'Y'))
    return 1;
  else
    return 0;
}
/*
 * Display email with simple pager functionality
 * Includes support for decoding MIME headers
 */
void email_pager(void) {
  uint32_t pos = 0;
  uint8_t *p = (uint8_t*)CURSORROW, mime = 0;
  struct emailhdrs *h = get_headers(selection);
  FILE *attachfp;
  uint16_t linecount;
  uint8_t  mime_enc, mime_binary, eof;
  char c;
  clrscr();
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  pos = h->skipbytes;
  fseek(fp, pos, SEEK_SET); // Skip over headers
restart:
  eof = 0;
  linecount = 0;
  attachfp = NULL;
  clrscr();
  fputs("Date:    ", stdout);
  printfield(h->date, 0, 39);
  fputs("\nFrom:    ", stdout);
  printfield(h->from, 0, 70);
  fputs("\nTo:      ", stdout);
  printfield(h->to, 0, 70);
  if (h->cc[0] != '\0') {
    fputs("\nCC:      ", stdout);
    printfield(h->cc, 0, 70);
  }
  fputs("\nSubject: ", stdout);
  printfield(h->subject, 0, 70);
  fputs("\n\n", stdout);
  get_line(fp, 1, &pos); // Reset buffer
  while (1) {
    if (get_line(fp, 0, &pos) == -1) {
      eof = 1;
    } else {
      ++linecount;
      if ((mime >= 1) && (!strncmp(linebuf, "--", 2))) {
        if (attachfp)
          fclose(attachfp);
        if ((mime == 4) && mime_binary) {
          putchar(BACKSPACE); // Erase spinner
          puts("[OK]");
        }
        attachfp = NULL;
        mime = 2;
        mime_enc = 0;
        mime_binary = 0;
      } else if ((mime < 4) && (mime >= 2)) {
        if (!strncasecmp(linebuf, "Content-Type: ", 14)) {
          if (!strncmp(linebuf + 14, "text/plain", 10))
            mime = 3;
          else if (!strncmp(linebuf + 14, "text/html", 9)) {
            printf("\n<Not showing HTML>\n");
            mime = 1;
          } else {
            mime_binary = 1;
            mime = 3;
          }
        } else if (!strncasecmp(linebuf, "Content-Transfer-Encoding: ", 27)) {
          mime = 3;
          if (!strncmp(linebuf + 27, "7bit", 4))
            mime_enc = 0;
          else if (!strncmp(linebuf + 27, "quoted-printable", 16))
            mime_enc = 1;
          else if (!strncmp(linebuf + 27, "base64", 6))
            mime_enc = 2;
          else {
            printf("** Unsupp encoding %s\n", linebuf + 27);
            mime = 1;
          }
        } else if (strstr(linebuf, "filename=")) {
          sprintf(filename, "%s/ATTACHMENTS/%s",
                  cfg_emaildir, strstr(linebuf, "filename=") + 9);
          filename[strlen(filename) - 1] = '\0'; // Remove '\r'
          if (prompt_okay_attachment(filename)) {
            printf("** Attachment -> %s  ", filename);
            attachfp = fopen(filename, "wb");
            if (!attachfp)
              printf("** Can't open %s\n", filename);
          } else
            mime = 1;
        } else if ((mime == 3) && (!strncmp(linebuf, "\r", 1))) {
          mime = 4;
        }
      }
      if (mime == 0)
        fputs(linebuf, stdout);
      if (mime == 4) {
        switch (mime_enc) {
        case 0:
          fputs(linebuf, stdout);
          break;
        case 1:
          decode_quoted_printable(attachfp, mime_binary);
          break;
        case 2:
          decode_base64(attachfp, mime_binary);
          break;
        }
        if (mime_binary && (linecount % 10 == 0))
          spinner();
      }
    }
    if ((*p) == 22) { // Use the CURSOR ROW location
      printf("\n%c[%05lu] SPACE continue reading | B)ack | T)op | H)drs | M)IME | Q)uit%c",
             INVERSE, pos, NORMAL);
retry1:
      c = cgetc();
      switch (c) {
      case ' ':
        break;
      case 'B':
      case 'b':
        if (pos < h->skipbytes + (uint32_t)(SCROLLBACK)) {
          pos = h->skipbytes;
          fseek(fp, pos, SEEK_SET);
          goto restart;
        } else {
          pos -= (uint32_t)(SCROLLBACK);
          fseek(fp, pos, SEEK_SET);
        }
        break;
      case 'T':
      case 't':
        mime = 0;
        pos = h->skipbytes;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'H':
      case 'h':
        mime = 0;
        pos = 0;
        fseek(fp, pos, SEEK_SET);
        goto restart;
      break;
      case 'M':
      case 'm':
        mime = 1;
        pos = h->skipbytes;
        fseek(fp, pos, SEEK_SET);
        goto restart;
      case 'Q':
      case 'q':
        if (attachfp)
          fclose(attachfp);
        fclose(fp);
        return;
      default:
        putchar(BELL);
        goto retry1;
      }
      clrscr();
    } else if (eof) {
      putchar(INVERSE);
      printf("\n%c[%05lu]      *** END ***       | B)ack | T)op | H)drs | M)IME | Q)uit%c",
             INVERSE, pos, NORMAL);
      putchar(NORMAL);
retry2:
      c = cgetc();
      switch (c) {
      case 'B':
      case 'b':
        if (pos < h->skipbytes + (uint32_t)(SCROLLBACK)) {
          pos = h->skipbytes;
          fseek(fp, pos, SEEK_SET);
          goto restart;
        } else {
          pos -= (uint32_t)(SCROLLBACK);
          fseek(fp, pos, SEEK_SET);
        }
        break;
      case 'T':
      case 't':
        mime = 0;
        pos = h->skipbytes;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'H':
      case 'h':
        mime = 0;
        pos = 0;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'M':
      case 'm':
        mime = 1;
        pos = h->skipbytes;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'Q':
      case 'q':
        if (attachfp)
          fclose(attachfp);
        fclose(fp);
        return;
      default:
        putchar(BELL);
        goto retry2;
      }
      clrscr();
    }
  }
}

/*
 * Write updated email headers to EMAIL.DB
 */
void write_updated_headers(struct emailhdrs *h, uint16_t pos) {
  uint16_t l;
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "rb+");
  if (!fp)
    error(ERR_FATAL, "Can't open %s", filename);
  if (fseek(fp, (pos - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET))
    error(ERR_FATAL, "Can't seek in %s", filename);
  l = fwrite(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
  if (l != EMAILHDRS_SZ_ON_DISK)
    error(ERR_FATAL, "Can't write to %s", filename);
  fclose(fp);
}

/*
 * Create new mailbox
 * Create directory, EMAIL.DB and NEXT.EMAIL files
 */
void new_mailbox(char *mbox) {
  sprintf(filename, "%s/%s", cfg_emaildir, mbox);
  if (mkdir(filename)) {
    error(ERR_NONFATAL, "Can't create dir %s", filename);
    return;
  }
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't create EMAIL.DB");
    return;
  }
  fclose(fp);
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't create NEXT.EMAIL");
    return;
  }
  fprintf(fp, "1");
  fclose(fp);
}

/*
 * Change current mailbox
 */
void switch_mailbox(char *mbox) {
  char prev_mbox[80];
  uint8_t err;
  // Treat '.' as shortcut for INBOX
  if (!strcmp(mbox, "."))
    strcpy(mbox, "INBOX");
  strcpy(prev_mbox, curr_mbox);
  strcpy(curr_mbox, mbox);
  first_msg = 1;
  err = read_email_db(first_msg, 1, 1); // Errors non-fatal
  if (err) {
    strcpy(curr_mbox, prev_mbox);
    return;
  }
  selection = 1;
  email_summary();
}

/*
 * Purge deleted messages from current mailbox
 */
void purge_deleted(void) {
  uint16_t count = 0, delcount = 0;
  struct emailhdrs *h;
  FILE *fp2;
  uint16_t l;
  h = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
  if (!h)
    error(ERR_FATAL, "Can't malloc()");
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  sprintf(filename, "%s/%s/EMAIL.DB.NEW", cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  while (1) {
    l = fread(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK)
      goto done;
    if (h->status == 'D') {
      sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
      if (unlink(filename)) {
        error(ERR_NONFATAL, "Can't delete %s", filename);
      }
      goto_prompt_row();
      putchar(CLRLINE);
      printf("%u msgs deleted", ++delcount);
    } else {
      l = fwrite(h, 1, EMAILHDRS_SZ_ON_DISK, fp2);
      if (l != EMAILHDRS_SZ_ON_DISK) {
        error(ERR_NONFATAL, "Can't write to %s", filename);
        free(h);
        fclose(fp);
        fclose(fp2);
        return;
      }
    }
  }
done:
  free(h);
  fclose(fp);
  fclose(fp2);
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  if (unlink(filename)) {
    error(ERR_NONFATAL, "Can't delete %s", filename);
    return;
  }
  sprintf(userentry, "%s/%s/EMAIL.DB.NEW", cfg_emaildir, curr_mbox);
  if (rename(userentry, filename)) {
    error(ERR_NONFATAL, "Can't rename %s", userentry);
    return;
  }
  read_email_db(first_msg, 1, 0);
  email_summary();
}

/*
 * Get next email number from NEXT.EMAIL
 * Returns 1 on error, 0 if all is good
 */
uint8_t get_next_email(char *mbox, uint16_t *num) {
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s/NEXT.EMAIL", mbox);
    return 1;
  }
  fscanf(fp, "%u", num);
  fclose(fp);
  return 0;
}

/*
 * Update NEXT.EMAIL file
 */
uint8_t update_next_email(char *mbox, uint16_t num) {
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s/NEXT.EMAIL", mbox);
    return 1;
  }
  fprintf(fp, "%u", num);
  fclose(fp);
  return 0;
}

/*
 * Copy string p to q truncating any trailing spaces.
 */
void truncate_header(char *p, char *q, uint8_t l) {
  int16_t last_char = -1;
  uint8_t i;
  for (i = 0; i < l; ++i) {
    q[i] = p[i];
    if ((p[i] != ' ') && (p[i] != '\0'))
      last_char = i;
  }
  q[last_char + 1] = '\0';
}

/*
 * Parse 'from' address, of the form 'Joe Smith <jsmith@example.org>'
 * If < > angle brackets are present, just return the text within them,
 * otherwise return the whole string.
 * p - input buffer
 * q - output buffer
 * Returns 0 if okay, 1 on error
 */
uint8_t parse_from_addr(char *p, char *q) {
  char *start = strchr(p, '<');
  char *end   = strchr(p, '>');
  uint8_t l;
  if (!start) {
    strcpy(q, p);
    return 0;
  } else {
    if (end)
      l = end - start - 1;
    else {
      error(ERR_NONFATAL, "Bad address format '%s'", p);
      return 1;
    }
  }
  strncpy(q, start + 1, l);
  q[l] = '\0';
  return 0;
}

/*
 * Prompt for a name in the line below the menu, store it in userentry
 * Returns number of chars read.
 * prompt - Message to display before > prompt
 * is_file - if 1, restrict chars to those allowed in ProDOS filename
 * Returns number of chars read
 */
uint8_t prompt_for_name(char *prompt, uint8_t is_file) {
  uint16_t i;
  char c;
  goto_prompt_row();
  printf("%s>", prompt);
  i = 0;
  while (1) {
    c = cgetc();
    if (is_file && !isalnum(c) && (c != RETURN) && (c != BACKSPACE) && (c != DELETE) && (c != '.')) {
      putchar(BELL);
      continue;
    }
    switch (c) {
    case RETURN:
      goto done;
    case BACKSPACE:
    case DELETE:
      if (i > 0) {
        putchar(BACKSPACE);
        putchar(' ');
        putchar(BACKSPACE);
        --i;
      } else
        putchar(BELL);
      break;
    default:
      putchar(c);
      userentry[i++] = c;
    }
    if (i == 79)
      goto done;
  }
done:
  userentry[i] = '\0';
  putchar(CLRLINE);
  goto_prompt_row();
  return i;
}

/*
 * Write email headers for replies and forwarded messages
 * fp1  - File handle of the mail message being replied/forwarded
 * fp2  - File handle of the destination mail message
 * h    - headers of the message being replied/forwarded
 * mode - 'R' for reply, 'F' for forward
 * Returns 0 if okay, 1 on error
 */
uint8_t write_email_headers(FILE *fp1, FILE *fp2, struct emailhdrs *h, char mode) {
  struct datetime dt;
  fprintf(fp2, "From: %s\r", cfg_emailaddr);
  truncate_header(h->subject, buf, 80);
  fprintf(fp2, "Subject: %s: %s\r", (mode == 'F' ? "Fwd" : "Re"), buf);
  readdatetime((unsigned char*)(SYSTEMTIME), &dt);
  datetimelong(&dt, buf);
  fprintf(fp2, "Date: %s\r", buf);
  if (mode == 'R') {
    truncate_header(h->from, filename, 80);
    if (parse_from_addr(filename, buf))
      return 1;
    fprintf(fp2, "To: %s\r", buf);
  } else {
    prompt_for_name("Fwd to", 0);
    if (strlen(userentry) == 0)
      return 0;
    fprintf(fp2, "To: %s\r", userentry);
  }
  prompt_for_name("cc", 0);
  fprintf(fp2, "cc: %s\r", userentry);
  fprintf(fp2, "X-Mailer: %s - Apple II Forever!\r\r", PROGNAME);
  if (mode == 'R') {
    truncate_header(h->date, buf, 40);
    fprintf(fp2, "On %s, ", buf);
    truncate_header(h->from, buf, 80);
    fprintf(fp2, "%s wrote:\r\r", buf);
  } else {
    fprintf(fp2, "-------- Forwarded Message --------\r");
    truncate_header(h->subject, buf, 80);
    fprintf(fp2, "Subject: %s\r", buf);
    truncate_header(h->date, buf, 40);
    fprintf(fp2, "Date: %s\r", buf);
    truncate_header(h->from, buf, 80);
    fprintf(fp2, "From: %s\r", buf);
    truncate_header(h->to, buf, 80);
    fprintf(fp2, "To: %s\r\r", buf);
  }
  fseek(fp1, h->skipbytes, SEEK_SET); // Skip headers when copying
  return 0;
}

/*
 * Copies the current message to mailbox mbox.
 * h is a pointer to the emailheaders for the message to copy
 * idx is the index of the message in EMAIL.DB in the source mailbox (1-based)
 * mbox is the name of the destination mailbox
 * delete - if set to 1 then the message will be marked as deleted in the
 *          source mbox
 * mode - 'R' for reply, 'F' for forward, otherwise ' '
 */
void copy_to_mailbox(struct emailhdrs *h, uint16_t idx,
                     char *mbox, uint8_t delete, char mode) {
  uint16_t num, buflen, written, l;
  FILE *fp2;

  // Read next number from dest/NEXT.EMAIL
  if (get_next_email(mbox, &num))
    return;

  // Open source email file
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  // Open destination email file
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, mbox, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    fclose(fp);
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  if (mode != ' ')
    if (write_email_headers(fp, fp2, h, mode)) {
      error(ERR_NONFATAL, "Invalid email header");
      fclose(fp);
      fclose(fp2);
      return;
    }

  // Make sure spinner is in the right place
  if ((mode == 'R') || (mode == 'F'))
    goto_prompt_row();

  // Copy email
  putchar(' '); // For spinner
  while (1) {
    buflen = fread(buf, 1, READSZ, fp);
    spinner();
    if (buflen == 0)
      break;
    written = fwrite(buf, 1, buflen, fp2);
    if (written != buflen) {
      error(ERR_NONFATAL, "Write error during copy");
      fclose(fp);
      fclose(fp2);
      return;
    }
  }
  putchar(BACKSPACE);
  putchar(' ');
  putchar(BACKSPACE);

  fclose(fp);
  fclose(fp2);

  // Update dest/EMAIL.DB unless this is R)eply or F)orward
  // The upshot of this is we never create EMAIL.DB in OUTBOX
  if (mode == ' ') {
    sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
    _filetype = PRODOS_T_BIN;
    _auxtype = 0;
    fp = fopen(filename, "ab");
    if (!fp) {
      error(ERR_NONFATAL, "Can't open %s/EMAIL.DB", mbox);
      return;
    }
    buflen = h->emailnum; // Just reusing buflen as a temporary
    h->emailnum = num;
    l = fwrite(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
    if (l != EMAILHDRS_SZ_ON_DISK) {
      error(ERR_NONFATAL, "Can't write to %s/EMAIL.DB %u %u", mbox);
      return;
    }
    h->emailnum = buflen ;
    fclose(fp);
  }

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email(mbox, num + 1))
    return;

  if (delete)
    h->status = 'D';
  h->tag = ' '; // Untag files after copy or move
  write_updated_headers(h, idx);
  email_summary_for(selection);

  if (mode != ' ') {
    // Not really an error but useful to have an alert
    sprintf(filename, "Created %s %s/OUTBOX/EMAIL.%u",
            (mode == 'R' ? "reply" : "fwded msg"), cfg_emaildir, num);
    error(ERR_NONFATAL, filename);
  }
}

/*
 * Prompt ok?
 */
char prompt_okay(char *msg) {
  char c;
  goto_prompt_row();
  printf("%sSure? (y/n)", msg);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N'))
      break;
    putchar(BELL);
  } 
  if ((c == 'y') || (c == 'Y'))
    c = 1;
  else
    c = 0;
  putchar(CLRLINE);
  return c;
}

/*
 * Check if there are tagged messages.  If not, just call copy_to_mailbox()
 * on the current message.  If they are, prompt the user and, if affirmative,
 * iterate through the tagged messages calling copy_to_mailbox() on each.
 */
uint8_t copy_to_mailbox_tagged(char *mbox, uint8_t delete) {
  uint16_t count = 0, tagcount = 0;
  struct emailhdrs *h;
  uint16_t l;
  if (total_tag == 0) {
    h = get_headers(selection);
    copy_to_mailbox(h, first_msg + selection - 1, mbox, delete, ' ');
    return 0;
  }
  sprintf(filename, "%u tagged - ", total_tag);
  if (!prompt_okay(filename))
    return 0;
  h = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
  if (!h)
    error(ERR_FATAL, "Can't malloc()");
  while (1) {
    sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
    _filetype = PRODOS_T_BIN;
    _auxtype = 0;
    fp = fopen(filename, "rb+");
    if (!fp) {
      error(ERR_NONFATAL, "Can't open %s", filename);
      return 1;
    }
    if (fseek(fp, count * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
      error(ERR_NONFATAL, "Can't seek in %s", filename);
      goto err;
    }
    l = fread(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
    fclose(fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK) {
      free(h);
      read_email_db(first_msg, 1, 0);
      email_summary();
      return 0;
    }
    if (h->tag == 'T') {
      h->tag = ' '; // Don't want it tagged in the destination
      goto_prompt_row();
      putchar(CLRLINE);
      printf("%u/%u:", ++tagcount, total_tag);
      copy_to_mailbox(h, count, mbox, delete, ' ');
    }
  }
err:
  free(h);
  fclose(fp);
  return 1;
}

/*
 * Create a blank outgoing message and put it in OUTBOX.
 * OUTBOX is not a 'proper' mailbox (no EMAIL.DB)
 */
void create_blank_outgoing() {
  struct datetime dt;
  uint16_t num;

  // Read next number from dest/NEXT.EMAIL
  if (get_next_email("OUTBOX", &num))
    return;

  // Open destination email file
  sprintf(filename, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  fprintf(fp, "From: %s\r", cfg_emailaddr);
  prompt_for_name("To", 0);
  if (strlen(userentry) == 0)
    return;
  fprintf(fp, "To: %s\r", userentry);
  prompt_for_name("Subject", 0);
  fprintf(fp, "Subject: %s\r", userentry);
  readdatetime((unsigned char*)(SYSTEMTIME), &dt);
  datetimelong(&dt, userentry);
  fprintf(fp, "Date: %s\r", userentry);
  prompt_for_name("cc", 0);
  fprintf(fp, "cc: %s\r", userentry);
  fprintf(fp, "X-Mailer: %s - Apple II Forever!\r\r", PROGNAME);
  fclose(fp);

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email("OUTBOX", num + 1))
    return;

  // Not really an error but useful to have an alert
  sprintf(filename, "Created file %s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
  error(ERR_NONFATAL, filename);
}

/*
 * Keyboard handler
 */
void keyboard_hdlr(void) {
  struct emailhdrs *h;
  while (1) {
    char c = cgetc();
    switch (c) {
    case 'k':
    case 'K':
    case UPARROW:
      if (selection > 1) {
        prevselection = selection;
        --selection;
        update_highlighted();
      } else if (first_msg > MSGS_PER_PAGE) {
        first_msg -= MSGS_PER_PAGE;
        read_email_db(first_msg, 0, 0);
        selection = num_msgs;
        email_summary();
      }
      break;
    case 't':
    case 'T':
      h = get_headers(selection);
      if (h) {
        if (h->tag == 'T') {
          h->tag = ' ';
          --total_tag;
        } else {
          h->tag = 'T';
          ++total_tag;
        }
        write_updated_headers(h, first_msg + selection - 1);
        email_summary_for(selection);
        status_bar();
      }
      // Fallthrough so tagging also moves down!!!
    case 'j':
    case 'J':
    case DOWNARROW:
      if (selection < num_msgs) {
        prevselection = selection;
        ++selection;
        update_highlighted();
      } else if (first_msg + selection - 1 < total_msgs) {
        first_msg += MSGS_PER_PAGE;
        read_email_db(first_msg, 0, 0);
        selection = 1;
        email_summary();
      }
      break;
    case RETURN:
    case ' ':
      h = get_headers(selection);
      if (h) {
        if (h->status == 'N')
          --total_new;
        h->status = 'R'; // Mark email read
        write_updated_headers(h, first_msg + selection - 1);
      }
      email_pager();
      email_summary();
      break;
    case 'd':
    case 'D':
      h = get_headers(selection);
      if (h) {
        h->status = 'D';
        write_updated_headers(h, first_msg + selection - 1);
        email_summary_for(selection);
      }
      break;
    case 'u':
    case 'U':
      h = get_headers(selection);
      if (h) {
        h->status = 'R';
        write_updated_headers(h, first_msg + selection - 1);
        email_summary_for(selection);
      }
      break;
    case 'c':
    case 'C':
      if (prompt_for_name("Copy to mbox", 1))
        copy_to_mailbox_tagged(userentry, 0);
      break;
    case 'm':
    case 'M':
      if (prompt_for_name("Move to mbox", 1))
        copy_to_mailbox_tagged(userentry, 1);
      break;
    case 'a':
    case 'A':
      goto_prompt_row();
      copy_to_mailbox_tagged("RECEIVED", 1);
      break;
    case 'p':
    case 'P':
      if (prompt_okay("Purge - ")) {
        purge_deleted();
        switch_mailbox(curr_mbox);
      }
      break;
    case 'n':
    case 'N':
      if (prompt_for_name("New mbox", 1))
        new_mailbox(userentry);
      break;
    case 's':
    case 'S':
      if (prompt_for_name("Switch mbox", 1))
        switch_mailbox(userentry);
      break;
    case 'w':
    case 'W':
      create_blank_outgoing();
      break;
    case 'r':
    case 'R':
      h = get_headers(selection);
      copy_to_mailbox(h, first_msg + selection - 1, "OUTBOX", 0, 'R');
      break;
    case 'f':
    case 'F':
      h = get_headers(selection);
      copy_to_mailbox(h, first_msg + selection - 1, "OUTBOX", 0, 'F');
      break;
    case 'q':
    case 'Q':
      if (prompt_okay("Quit - ")) {
        clrscr();
        exit(0);
      }
    default:
      //printf("[%02x]", c);
      putchar(BELL);
    }
  }
}

void main(void) {
  videomode(VIDEOMODE_80COL);
  readconfigfile();
  first_msg = 1;
  read_email_db(first_msg, 1, 0);
  selection = 1;
  email_summary();
  keyboard_hdlr();
}

