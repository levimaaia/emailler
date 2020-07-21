/////////////////////////////////////////////////////////////////
// emai//er - Simple Email User Agent vaguely inspired by Elm
// Handles INBOX in the format created by POP65
// Bobbi June, July 2020
/////////////////////////////////////////////////////////////////

// - TODO: Should decode MIME body even if not multipart
// - TODO: Add Base64 encoding
// - TODO: Feature to attach files to outgoing messages
// - TODO: Get rid of all uses of malloc(). Don't need it.
// - TODO: See TODOs further down for error handling

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
char                  halfscreen[0x0400];
FILE                  *fp;
struct emailhdrs      *headers;
uint16_t              selection, prevselection;
uint16_t              num_msgs;    // Num of msgs shown in current page
uint16_t              total_msgs;  // Total number of message in mailbox
uint16_t              total_new;   // Total number of new messages
uint16_t              total_tag;   // Total number of tagged messages
uint16_t              first_msg;   // Msg numr: first message current page
uint8_t               reverse;     // 0 normal, 1 reverse order
char                  curr_mbox[80] = "INBOX";
static unsigned char  buf[READSZ];

#define ERR_NONFATAL 0
#define ERR_FATAL    1

// Shove this up in the Language Card out of an abundance of caution
#pragma code-name (push, "LC")
void load_editor(void) {
  sprintf(userentry, "%s/EDIT.SYSTEM", cfg_instdir);
  exec(userentry, filename);
}

void load_pop65(void) {
  sprintf(filename, "%s/POP65.SYSTEM", cfg_instdir);
  exec(filename, "EMAIL");
}

void load_smtp65(void) {
  sprintf(filename, "%s/SMTP65.SYSTEM", cfg_instdir);
  exec(filename, "EMAIL");
}
#pragma code-name (pop)

/*
 * Put cursor at beginning of PROMPT_ROW
 */
void goto_prompt_row(void) {
  uint8_t i;
  putchar(HOME);
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(CURDOWN);
}

void clrscr2(void) {
  videomode(VIDEOMODE_80COL);
  clrscr();
}

/*
 * Show non fatal error in PROMPT_ROW
 * Fatal errors are shown on a blank screen
 */
void error(uint8_t fatal, const char *fmt, ...) {
  va_list v;
  if (fatal) {
    clrscr2();
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
 * Print spaces
 */
void pr_spc(uint8_t n) {
  while (n--)
    putchar(' ');
}
/*
 * Print ASCII-art envelope
 */
void envelope(void) {
  uint8_t i;
  putchar(NORMAL);
  putchar(HOME);
  for (i = 0; i < 8 - 1; ++i) 
    putchar(CURDOWN);
  pr_spc(30); puts("+------------------+");
  pr_spc(30); puts("|\\  \"Inbox Zero\"  /|");
  pr_spc(30); puts("| \\              / |");
  pr_spc(30); puts("|  \\            /  |");
  pr_spc(30); puts("|   +----------+   |");
  pr_spc(30); puts("|            Bobbi |");
  pr_spc(30); puts("+------------------+");
  pr_spc(30); puts("                 ... No messages in this mailbox");
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
 * Read parms from EMAIL.CFG
 */
void readconfigfile(void) {
  fp = fopen("EMAIL.CFG", "r");
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
 * Convert date/time bytes into struct datetime format.
 */
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
  uint8_t done_visible = 0;
  int32_t pos;
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
  if (reverse) {
    // TODO Streamline this once it works
    if (fseek(fp, 0, SEEK_END)) {
      fclose(fp);
      error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
      if (switchmbox)
        return 1;
    }
    // If the mailbox is empty this seek will fail
    if (fseek(fp, ftell(fp) - startnum * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
      fclose(fp);
      num_msgs = total_new = total_msgs = total_tag = 0;
      return 0;
    }
  } else {
    if (fseek(fp, (startnum - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
      fclose(fp);
      error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
      if (switchmbox)
        return 1;
    }
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
    } else {
      if (!initialize) {
        free(curr);
        fclose(fp);
        return 0;
      }
      done_visible = 1;
    }
    if (initialize) {
      ++total_msgs;
      if (curr->status == 'N')
        ++total_new;
      if (curr->tag == 'T')
        ++total_tag;
      if (done_visible)
        free(curr);
    }
    if (reverse) {
      // TODO Streamline this once it works
      pos = ftell(fp) - 2 * EMAILHDRS_SZ_ON_DISK;
      if (pos == -1 * EMAILHDRS_SZ_ON_DISK) {
        fclose(fp);
        return 0;
      }
      if (fseek(fp, pos, SEEK_SET)) {
        error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
        fclose(fp);
        return 0;
      }
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
  if (num_msgs == 0) {
    printf("%c%s [%s] No messages ", INVERSE, PROGNAME, curr_mbox);
    envelope();
  } else
    printf("%c[%s] %u msgs, %u new, %u tagged. Showing %u-%u. %c ",
           INVERSE, curr_mbox, total_msgs, total_new, total_tag, first_msg,
           first_msg + num_msgs - 1, (reverse ? '<' : '>'));
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
  clrscr2();
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
 * Read a text file a line at a time
 * Returns number of chars in the line, or -1 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * reset - if 1 then just reset the buffer and return
 * writep - Pointer to buffer into which line will be written
 * pos - position in file is updated via this pointer
 */
int16_t get_line(FILE *fp, uint8_t reset, char *writep, uint32_t *pos) {
  static uint16_t rd = 0; // Read
  static uint16_t wt = 0; // Write
  uint8_t found = 0;
  uint16_t j = 0;
  uint16_t i;
  if (reset) {
    rd = wt = 0;
    *pos = 0;
    return 0;
  }
  while (1) {
    while (rd < wt) {
      writep[j++] = buf[rd++];
      ++(*pos);
      if (writep[j - 1] == '\r') {
        found = 1;
        break;
      }
    }
    writep[j] = '\0';
    if (rd == wt) // Empty buf[]
      rd = wt = 0;
    if (found)
      return j;
    if (feof(fp))
      return -1;
    i = fread(&buf[wt], 1, READSZ - wt, fp);
    wt += i;
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
 * Decode linebuf[] from quoted-printable format in place
 * p - Pointer to buffer to decode. Results written in place.
 * Returns number of bytes decoded
 */
uint16_t decode_quoted_printable(char *p) {
  uint16_t i = 0, j = 0;
  char c;
  while (c = p[i]) {
    if (c == '=') {
      if (p[i + 1] == '\r') { // Trailing '=' is a soft '\r'
        p[j] = '\0';
        return j;
      }
      // Otherwise '=xx' where x is a hex digit
      c = 16 * hexdigit(linebuf[i + 1]) + hexdigit(linebuf[i + 2]);
      if ((c >= 0x20) && (c <= 0x7e))
        p[j++] = c;
      i += 3;
    } else {
      p[j++] = c;
      ++i;
    }
  }
  p[j] = '\0';
  return j;
}

/*
 * Base64 decode table
 */
static const int8_t b64dec[] =
  {62,-1,-1,-1,63,52,53,54,55,56,
   57,58,59,60,61,-1,-1,-1,-2,-1,
   -1,-1, 0, 1, 2, 3, 4, 5, 6, 7,
    8, 9,10,11,12,13,14,15,16,17,
   18,19,20,21,22,23,24,25,-1,-1,
   -1,-1,-1,-1,26,27,28,29,30,31,
   32,33,34,35,36,37,38,39,40,41,
   42,43,44,45,46,47,48,49,50,51};

/*
 * Decode linebuf[] from Base64 format in place
 * Each line of base64 has up to 76 chars, which decodes to up to 57 bytes
 * p - Pointer to buffer to decode. Results written in place.
 * Returns number of bytes decoded
 */
uint16_t decode_base64(char *p) {
  uint16_t i = 0, j = 0;
  while (p[i] != '\r') {
    p[j++] = b64dec[p[i] - 43] << 2 | b64dec[p[i + 1] - 43] >> 4;
    if (p[i + 2] != '=')
      p[j++] = b64dec[p[i + 1] - 43] << 4 | b64dec[p[i + 2] - 43] >> 2;
    if (linebuf[i + 3] != '=')
      p[j++] = b64dec[p[i + 2] - 43] << 6 | b64dec[p[i + 3] - 43];
    i += 4;
  }
  return j;
}

/*
 * Print line up to first '\r' or '\0'
 */
void putline(FILE *fp, char *s) {
  char *ret = strchr(s, '\r');
  if (ret)
    *ret = '\0';
  fputs(s, fp);
  if (ret)
    *ret = '\r';
}

/*
 * Perform word wrapping, for a line of text, which may contain multiple
 * embedded '\r' carriage returns, or no carriage return at all.
 * fp - File handle to use for output.
 * s - Pointer to pointer to input buffer. If all text is consumed, this is
 *     set to NULL.  If there is text left in the buffer to be consumed then
 *     the pointer will be advanced to point to the next text to process.
 * Returns 1 if the caller should invoke the routine again before obtaining
 * more input, or 0 if there is nothing more to do or caller needs to get more
 * input before next call.
 */
uint8_t word_wrap_line(FILE *fp, char **s) {
  static uint8_t col = 0;           // Keeps track of screen column
  char *ss = *s;
  char *ret = strchr(ss, '\r');
  uint16_t l = strlen(ss);
  char *nextline = NULL;
  uint16_t i;
  if (l == 0)
    return 0;                      // Need more input to proceed
  if (ret) {
    if (l > (ret - ss) + 1)        // If '\r' is not at the end ...
      nextline = ss + (ret - ss) + 1; // Keep track of next line(s)
    l = ret - ss;
  }
  if (ret) {
    if ((col + l) <= 80) {         // Fits on this line
      col += l;
      putline(fp, ss);
      if (ret) {
        col = 0;
        if (col + l != 80)
          fputc('\r', fp);
      }
      *s = nextline;
      return (*s ? 1 : 0);         // Caller should invoke again
    }
    i = 80 - col;                  // Doesn't fit, need to break
    if (i > l)
      i = l;
    while ((ss[--i] != ' ') && (i > 0));
    if (i == 0) {                  // No space character found
      if (col == 0)                // Doesn't fit on full line
        for (i = 0; i < 80; ++i) { // Truncate @80 chars
          fputc(ss[i], fp);
          *s = ss + l + 1;
      } else                       // There is stuff on this line already
        fputc('\r', fp);           // Try a blank line
      col = 0;
      return (ret ? (*s ? 0 : 1) : 0); // If EOL, caller should invoke again
    }
  } else {                         // No ret
    i = 80 - col;                  // Space left on line
    if (i > l)
      return 0;                    // Need more input to proceed
    while ((ss[--i] != ' ') && (i > 0));
    if (i == 0)                    // No space character found
      return 0;                    // Need more input to proceed
  }
  ss[i] = '\0';                    // Space was found, split line
  putline(fp, ss);
  fputc('\r', fp);
  col = 0;
  *s = ss + i + 1;
  return (*s ? 1 : 0);             // Caller should invoke again
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
 * Sanitize filename for ProDOS
 * Modifies s in place
 */
void sanitize_filename(char *s) {
  uint8_t i = 0, j = 0;
  char c;
  while (1) {
    c = s[i++];
    if (c == '\r') {
      s[j] = '\0';
      return;
    }
    if (isalnum(c) || c == '.' || c == '/')
      s[j++] = c;
  }
}

#define FROMAUX 0
#define TOAUX   1
/*
 * Aux memory copy routine
 */
void copyaux(char *src, char *dst, uint16_t len, uint8_t dir) {
    char **a1 = (char**)0x3c;
    char **a2 = (char**)0x3e;
    char **a4 = (char**)0x42;
    *a1 = src;
    *a2 = src + len - 1; // AUXMOVE moves length+1 bytes!!
    *a4 = dst;
    if (dir == TOAUX) {
        __asm__("sec");       // Copy main->aux
        __asm__("jsr $c311"); // AUXMOVE
    } else {
        __asm__("clc");       // Copy aux->main
        __asm__("jsr $c311"); // AUXMOVE
    }
}

/*
 * Save the current screen to the scrollback file
 * TODO: No error handling!!
 */
void save_screen_to_scrollback(FILE *fp) {
  fwrite((void*)0x0400, 0x0400, 1, fp); // Even cols
  copyaux((void*)0x400, halfscreen, 0x400, FROMAUX);
  fwrite(halfscreen, 0x0400, 1, fp); // Odd cols
}

/*
 * Load a screen from the scrollback file
 * Screens are numbered 1, 2, 3 ...
 * Does not trash the screen holes, which must be preserved!
 * TODO: No error handling!!
 */
void load_screen_from_scrollback(FILE *fp, uint8_t screen) {
  fseek(fp, (screen - 1) * 0x0800, SEEK_SET);
  fread(halfscreen, 0x0400, 1, fp); // Even cols
  memcpy((void*)0x400, halfscreen + 0x000, 0x077);
  memcpy((void*)0x480, halfscreen + 0x080, 0x077);
  memcpy((void*)0x500, halfscreen + 0x100, 0x077);
  memcpy((void*)0x580, halfscreen + 0x180, 0x077);
  memcpy((void*)0x600, halfscreen + 0x200, 0x077);
  memcpy((void*)0x680, halfscreen + 0x280, 0x077);
  memcpy((void*)0x700, halfscreen + 0x300, 0x077);
  memcpy((void*)0x780, halfscreen + 0x380, 0x077);
  fread(halfscreen, 0x0400, 1, fp); // Odd cols
  copyaux(halfscreen + 0x000, (void*)0x400, 0x077, TOAUX);
  copyaux(halfscreen + 0x080, (void*)0x480, 0x077, TOAUX);
  copyaux(halfscreen + 0x100, (void*)0x500, 0x077, TOAUX);
  copyaux(halfscreen + 0x180, (void*)0x580, 0x077, TOAUX);
  copyaux(halfscreen + 0x200, (void*)0x600, 0x077, TOAUX);
  copyaux(halfscreen + 0x280, (void*)0x680, 0x077, TOAUX);
  copyaux(halfscreen + 0x300, (void*)0x700, 0x077, TOAUX);
  copyaux(halfscreen + 0x380, (void*)0x780, 0x077, TOAUX);
  fseek(fp, 0, SEEK_END);
}

#define ENC_7BIT 0   // 7bit
#define ENC_QP   1   // Quoted-Printable
#define ENC_B64  2   // Base64
#define ENC_SKIP 255 // Do nothing

/*
 * Display email with simple pager functionality
 * Includes support for decoding MIME headers
 */
void email_pager(struct emailhdrs *h) {
  uint32_t pos = 0;
  uint8_t *cursorrow = (uint8_t*)CURSORROW, mime = 0;
  FILE *sbackfp = NULL;
  FILE *attachfp;
  uint16_t linecount, chars;
  uint8_t  mime_enc, mime_binary, eof, screennum, maxscreennum;
  char c, *readp, *writep;
  clrscr2();
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    if (sbackfp)
      fclose(sbackfp);
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  pos = h->skipbytes;
  fseek(fp, pos, SEEK_SET); // Skip over headers
restart:
  eof = 0;
  linecount = 0;
  readp = linebuf;
  writep = linebuf;
  attachfp = NULL;
  if (sbackfp)
    fclose(sbackfp);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  sprintf(filename, "%s/SCROLLBACK", cfg_emaildir);
  unlink(filename);
  sbackfp = fopen(filename, "wb+");
  if (!sbackfp) {
    error(ERR_NONFATAL, "No scrollback");
  }
  maxscreennum = screennum = 0;
  clrscr2();
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
  get_line(fp, 1, linebuf, &pos); // Reset buffer
  while (1) {
    if (!readp)
      readp = linebuf;
    if (!writep)
      writep = linebuf;
//printf("READ W=%p R=%p\n", writep, readp);
    if (get_line(fp, 0, writep, &pos) == -1)
      eof = 1;
    ++linecount;
    if ((mime >= 1) && (!strncmp(writep, "--", 2))) {
      if (attachfp)
        fclose(attachfp);
      if ((mime == 4) && mime_binary) {
        putchar(BACKSPACE); // Erase spinner
        puts("[OK]");
      }
      attachfp = NULL;
      mime = 2;
      mime_enc = ENC_7BIT;
      mime_binary = 0;
      readp = writep = NULL;
    } else if ((mime < 4) && (mime >= 2)) {
      if (!strncasecmp(writep, "Content-Type: ", 14)) {
        if (!strncmp(writep + 14, "text/plain", 10)) {
          mime = 3;
        } else if (!strncmp(writep + 14, "text/html", 9)) {
          printf("\n<Not showing HTML>\n");
          mime = 1;
        } else {
          mime_binary = 1;
          mime = 3;
        }
      } else if (!strncasecmp(writep, "Content-Transfer-Encoding: ", 27)) {
        mime = 3;
        if (!strncmp(writep + 27, "7bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, "quoted-printable", 16))
          mime_enc = ENC_QP;
        else if (!strncmp(writep + 27, "base64", 6))
          mime_enc = ENC_B64;
        else {
          printf("** Unsupp encoding %s\n", writep + 27);
          mime = 1;
        }
      } else if (strstr(writep, "filename=")) {
        sprintf(filename, "%s/ATTACHMENTS/%s",
                cfg_emaildir, strstr(writep, "filename=") + 9);
        sanitize_filename(filename);
        if (prompt_okay_attachment(filename)) {
          printf("** Attachment -> %s  ", filename);
          attachfp = fopen(filename, "wb");
          if (!attachfp)
            printf("\n** Can't open %s  ", filename);
        } else
          attachfp = NULL;
      } else if ((mime == 3) && (!strncmp(writep, "\r", 1))) {
        mime = 4;
        if (!attachfp && mime_binary) {
          mime_enc = ENC_SKIP; // Skip over binary MIME parts with no filename
          fputs("Skipping  ", stdout);
        }
      }
      readp = writep = NULL;
    } else if (mime == 4) {
      switch (mime_enc) {
      case ENC_QP:
        chars = decode_quoted_printable(writep);
        break;
       case ENC_B64:
        chars = decode_base64(writep);
        break;
       case ENC_SKIP:
        readp = writep = NULL;
        break;
      }
      if (mime_binary && !(linecount % 10))
        spinner();
    }
    if (readp) {
      if (mime == 0) {
        while (word_wrap_line(stdout, &readp) == 1);
        writep = NULL;
      }
      if (mime == 1) {
        readp = writep = NULL;
      }
      if (mime == 4) {
        if (mime_binary) {
          if (attachfp)
            fwrite(readp, 1, chars, attachfp);
          readp = writep = NULL;
        } else {
          while (word_wrap_line(stdout, &readp) == 1);
          if (readp) {
            chars = strlen(readp);
            memmove(linebuf, readp, strlen(readp));
            readp = linebuf;
            writep = linebuf + chars;
          } else
            writep = NULL;
        }
      }
    }
    if ((*cursorrow >= 21) || eof) {
      printf("\n%c[%05lu] %s | B)ack | T)op | H)drs | M)IME | Q)uit%c",
             INVERSE,
             pos,
             (eof ? "       ** END **      " : "SPACE continue reading"),
             NORMAL);
      if (sbackfp) {
        save_screen_to_scrollback(sbackfp);
        ++screennum;
        ++maxscreennum;
      }
retry:
      c = cgetc();
      switch (c) {
      case ' ':
        if (sbackfp && (screennum < maxscreennum)) {
          load_screen_from_scrollback(sbackfp, ++screennum);
          goto retry;
        } else {
          if (eof) {
            putchar(BELL);
            goto retry;
          }
        }
        break;
      case 'B':
      case 'b':
        if (sbackfp && (screennum > 1)) {
          load_screen_from_scrollback(sbackfp, --screennum);
          goto retry;
        } else {
          putchar(BELL);
          goto retry;
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
        if (sbackfp)
          fclose(sbackfp);
        fclose(fp);
        return;
      default:
        putchar(BELL);
        goto retry;
      }
      clrscr2();
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
 * fwd_to - Recipient (used for mode=='F' only)
 * Returns 0 if okay, 1 on error
 */
uint8_t write_email_headers(FILE *fp1, FILE *fp2, struct emailhdrs *h,
                            char mode, char *fwd_to) {
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
  } else
    fprintf(fp2, "To: %s\r", fwd_to);
  prompt_for_name("cc", 0);
  if (strlen(userentry) > 0)
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

  if (mode == 'F') {
    prompt_for_name("Fwd to", 0);
    if (strlen(userentry) == 0)
      return;
  }

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
    if (write_email_headers(fp, fp2, h, mode, userentry)) {
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
    if (prompt_okay("Open in editor - ")) {
      sprintf(filename, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
      load_editor();
    }
  }
}

/*
 * Return index into EMAIL.DB for current selection.
 */
uint16_t get_db_index(void) {
  if (!reverse)
    return first_msg + selection - 1;
  else
    return total_msgs - (first_msg + selection - 1) + 1;
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
    copy_to_mailbox(h, get_db_index(), mbox, delete, ' ');
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
      free(h);
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
  if (strlen(userentry) > 0)
    fprintf(fp, "cc: %s\r", userentry);
  fprintf(fp, "X-Mailer: %s - Apple II Forever!\r\r", PROGNAME);
  fclose(fp);

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email("OUTBOX", num + 1))
    return;

  // Not really an error but useful to have an alert
  sprintf(filename, "Created file %s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
  error(ERR_NONFATAL, filename);

  if (prompt_okay("Open in editor - ")) {
    sprintf(filename, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
    load_editor();
  }
}

/*
 * Keyboard handler
 */
void keyboard_hdlr(void) {
  struct emailhdrs *h;
  char c;
  while (1) {
    h = get_headers(selection);
    c = cgetc();
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
      if (h) {
        if (h->tag == 'T') {
          h->tag = ' ';
          --total_tag;
        } else {
          h->tag = 'T';
          ++total_tag;
        }
        write_updated_headers(h, get_db_index());
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
      if (h) {
        if (h->status == 'N')
          --total_new;
        h->status = 'R'; // Mark email read
        write_updated_headers(h, get_db_index());
        email_pager(h);
        email_summary();
      }
      break;
    case 'd':
    case 'D':
      if (h) {
        h->status = 'D';
        write_updated_headers(h, get_db_index());
        email_summary_for(selection);
      }
      break;
    case 'u':
    case 'U':
      if (h) {
        h->status = 'R';
        write_updated_headers(h, get_db_index());
        email_summary_for(selection);
      }
      break;
    case 'c':
    case 'C':
      if (h)
        if (prompt_for_name("Copy to mbox", 1))
          copy_to_mailbox_tagged(userentry, 0);
      break;
    case 'm':
    case 'M':
      if (h)
        if (prompt_for_name("Move to mbox", 1))
          copy_to_mailbox_tagged(userentry, 1);
      break;
    case 'a':
    case 'A':
      if (h) {
        goto_prompt_row();
        copy_to_mailbox_tagged("RECEIVED", 1);
      }
      break;
    case 'p':
    case 'P':
      if (h) {
        if (prompt_okay("Purge - ")) {
          purge_deleted();
          first_msg = 1;
          read_email_db(first_msg, 1, 0);
          selection = 1;
          email_summary();
        }
      }
      break;
    case 'r':
    case 'R':
      if (h)
        copy_to_mailbox(h, get_db_index(), "OUTBOX", 0, 'R');
      break;
    case 'f':
    case 'F':
      if (h)
        copy_to_mailbox(h, get_db_index(), "OUTBOX", 0, 'F');
      break;
    // Everything above here needs a selected message (h != NULL)
    // Everything below here does NOT need a selected message
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
    case ',':
    case '<':
      reverse = 1;
      switch_mailbox(curr_mbox);
      break;
    case '.':
    case '>':
      reverse = 0;
      switch_mailbox(curr_mbox);
      break;
    case 0x80 + 'e': // OA-E "Open message in editor"
    case 0x80 + 'E':
      sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
      load_editor();
      break;
    case 0x80 + 'r': // OA-R "Retrieve messages from server"
    case 0x80 + 'R':
      load_pop65();
      break;
    case 0x80 + 's': // OA-S "Send messages in Outbox to server"
    case 0x80 + 'S':
      load_smtp65();
      break;
    case 'q':
    case 'Q':
      if (prompt_okay("Quit - ")) {
        clrscr2();
        exit(0);
      }
    default:
      //printf("[%02x]", c);
      putchar(BELL);
    }
  }
}


void main(void) {
  uint8_t *pp;
  pp = (uint8_t*)0xbf98;
  if (!(*pp & 0x02))
    error(ERR_FATAL, "Need 80 cols");
  if ((*pp & 0x30) != 0x30)
    error(ERR_FATAL, "Need 128K");
  videomode(VIDEOMODE_80COL);
  readconfigfile();
  reverse = 0;
  first_msg = 1;
  read_email_db(first_msg, 1, 0);
  selection = 1;
  email_summary();
  keyboard_hdlr();
}

