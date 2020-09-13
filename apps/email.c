/////////////////////////////////////////////////////////////////
// emai//er - Simple Email User Agent vaguely inspired by Elm
// Handles INBOX in the format created by POP65
// Bobbi June, July 2020
/////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <conio.h>
#include <string.h>
#include <ctype.h>
#include <apple2_filetype.h>

#include <errno.h> // DEBUG

#define EMAIL_C
#include "email_common.h"

// Program constants
#define MSGS_PER_PAGE 19     // Number of messages shown on summary screen
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
#define ESC           0x1b
#define CURUP         0x1f
#define DELETE        0x7f

// Addresses
#define CURSORROW     0x0025
#define SYSTEMTIME    0xbf90

char openapple[] = "\x0f\x1b""A\x18\x0e";

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

#pragma code-name (push, "LC")
void load_editor(uint8_t compose) {
  snprintf(userentry, 80, "%s %s", (compose ? "-compose" : "-reademail"), filename);
  snprintf(filename, 80, "%s/EDIT.SYSTEM", cfg_instdir);
  exec(filename, userentry);
}
#pragma code-name (pop)

#pragma code-name (push, "LC")
void load_pop65(void) {
  snprintf(filename, 80, "%s/POP65.SYSTEM", cfg_instdir);
  exec(filename, "EMAIL");
}
#pragma code-name (pop)

#pragma code-name (push, "LC")
void load_smtp65(void) {
  snprintf(filename, 80, "%s/SMTP65.SYSTEM", cfg_instdir);
  exec(filename, "EMAIL");
}
#pragma code-name (pop)

#pragma code-name (push, "LC")
void load_date65(void) {
  snprintf(filename, 80, "%s/DATE65.SYSTEM", cfg_instdir);
  exec(filename, "EMAIL");
}
#pragma code-name (pop)

/*
 * Put cursor at beginning of PROMPT_ROW
 */
#pragma code-name (push, "LC")
void goto_prompt_row(void) {
  uint8_t i;
  putchar(HOME);
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(CURDOWN);
}
#pragma code-name (pop)

#pragma code-name (push, "LC")
void clrscr2(void) {
  videomode(VIDEOMODE_80COL);
  clrscr();
}
#pragma code-name (pop)

/*
 * Show non fatal error in PROMPT_ROW
 * Fatal errors are shown on a blank screen
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Print spaces
 */
#pragma code-name (push, "LC")
void pr_spc(uint8_t n) {
  while (n--)
    putchar(' ');
}
#pragma code-name (pop)

/*
 * Save preferences
 */
#pragma code-name (push, "LC")
void save_prefs(void) {
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen("EMAIL.PREFS", "wb");
  if (!fp)
    return;
  fprintf(fp, "order:%s", (reverse ? "<" : ">"));
  fclose(fp);
}
#pragma code-name (pop)

/*
 * Load preferences
 */
#pragma code-name (push, "LC")
void load_prefs(void) {
  char order = 'a';
  fp = fopen("EMAIL.PREFS", "rb");
  if (!fp)
    return;
  fscanf(fp, "order:%s", &order);
  fclose(fp);
  reverse = (order == '<' ? 1 : 0);
}
#pragma code-name (pop)

/*
 * Print ASCII-art envelope
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Busy spinner
 */
#pragma code-name (push, "LC")
void spinner(void) {
  static char chars[] = "|/-\\";
  static uint8_t i = 0;
  putchar(BACKSPACE);
  putchar(chars[(i++) % 4]);
}
#pragma code-name (pop)

/*
 * Read parms from EMAIL.CFG
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Convert date/time bytes into struct datetime format.
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Print a date/time value in short format for the status bar
 */
#pragma code-name (push, "LC")
void printdatetime(struct datetime *dt) {
  if (dt->nodatetime)
    fputs("????-??-?? ??:??", stdout);
  else {
    printf("%04d-%02d-%02d %02d:%02d",
           dt->year, dt->month, dt->day, dt->hour, dt->minute);
  }
}
#pragma code-name (pop)

/*
 * Format a date/time in format suitable for an email
 * dt - structure representing date/time
 * s  - result is returned through this pointer
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Obtain the system date and time for the status bar
 */
#pragma code-name (push, "LC")
void printsystemdate(void) {
  struct datetime dt;
  readdatetime((unsigned char*)(SYSTEMTIME), &dt);
  printdatetime(&dt);
}
#pragma code-name (pop)

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
#pragma code-name (push, "LC")
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
  snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't open %s", filename);
    if (switchmbox)
      return 1;
  }
  if (reverse) {
    if (fseek(fp, 0, SEEK_END)) {
      fclose(fp);
      error(switchmbox ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
      if (switchmbox)
        return 1;
    }
    // If the mailbox is empty this seek will fail
    if (fseek(fp, ftell(fp) - (uint32_t)startnum * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
      fclose(fp);
      num_msgs = total_new = total_msgs = total_tag = 0;
      return 0;
    }
  } else {
    if (fseek(fp, (uint32_t)(startnum - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
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
      pos = ftell(fp) - 2L * EMAILHDRS_SZ_ON_DISK;
      if (pos == -1L * EMAILHDRS_SZ_ON_DISK) {
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
#pragma code-name (pop)

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
  putchar('|');
  printfield(h->date, 0, 16);
  putchar('|');
  printfield(h->from, 0, 20);
  putchar('|');
  printfield(h->subject, 0, 39);
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
  for (i = 0; i < PROMPT_ROW - 2; ++i) 
    putchar(CURDOWN);
  printf("%cOA-? Help                                                                       %c", INVERSE, NORMAL);
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
  static uint16_t end = 0; // End of valid data in buf
  uint16_t i = 0;
  if (reset) {
    rd = end = 0;
    *pos = 0;
    return 0;
  }
  while (1) {
    if (rd == end) {
      end = fread(buf, 1, READSZ, fp);
      rd = 0;
    }
    if (end == 0)
      return -1; // EOF
    writep[i++] = buf[rd++];
    ++(*pos);
    if (writep[i - 1] == '\r') {
      writep[i] = '\0';
      return i;
    }
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
const int8_t b64dec[] =
  {62,-1,-1,-1,63,52,53,54,55,56,
   57,58,59,60,61,-1,-1,-1,-2,-1,
   -1,-1, 0, 1, 2, 3, 4, 5, 6, 7,
    8, 9,10,11,12,13,14,15,16,17,
   18,19,20,21,22,23,24,25,-1,-1,
   -1,-1,-1,-1,26,27,28,29,30,31,
   32,33,34,35,36,37,38,39,40,41,
   42,43,44,45,46,47,48,49,50,51};


/*
 * Decode Base64 format in place
 * Each line of base64 has up to 76 chars, which decodes to up to 57 bytes
 * p - Pointer to buffer to decode. Results written in place.
 * Returns number of bytes decoded
 */
#if 0
uint16_t decode_base64(char *p) {
  uint16_t i = 0, j = 0;
  const int8_t *b = b64dec - 43;
  while (p[i] != '\r') {
    p[j++] = b[p[i]] << 2 | b[p[i + 1]] >> 4;
    if (p[i + 2] != '=')
      p[j++] = b[p[i + 1]] << 4 | b[p[i + 2]] >> 2;
    if (linebuf[i + 3] != '=')
      p[j++] = b[p[i + 2]] << 6 | b[p[i + 3]];
    i += 4;
  }
  return j;
}
#endif

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
 * cols - Number of columns to break at
 * mode - 'R' for reply, 'F' for forward, otherwise ' '
 * Returns 1 if the caller should invoke the routine again before obtaining
 * more input, or 0 if there is nothing more to do or caller needs to get more
 * input before next call.
 */
uint8_t word_wrap_line(FILE *fp, char **s, uint8_t cols, char mode) {
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
    if ((col + l) <= cols) {         // Fits on this line
      col += l;
      putline(fp, ss);
      if (ret) {
        col = 0;
        if (col + l != cols) {
          fputc('\r', fp);
          if (mode == 'R') {
            fputc('>', fp);
            ++col;
          }
        }
      }
      *s = nextline;
      return (*s ? 1 : 0);         // Caller should invoke again
    }
    i = cols - col;                  // Doesn't fit, need to break
    if (i > l)
      i = l;
    while ((ss[--i] != ' ') && (i > 0));
    if (i == 0) {                  // No space character found
      if (col == 0)                // Doesn't fit on full line
        for (i = 0; i < cols; ++i) { // Truncate @cols chars
          fputc(ss[i], fp);
          *s = ss + l + 1;
      } else {                     // There is stuff on this line already
        col = 0;
        fputc('\r', fp);           // Try a blank line
        if (mode == 'R') {
          fputc('>', fp);
          ++col;
        }
      }
      return (ret ? (*s ? 0 : 1) : 0); // If EOL, caller should invoke again
    }
  } else {                         // No ret
    i = cols - col;                  // Space left on line
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
  if (mode == 'R') {
    fputc('>', fp);
    ++col;
  }
  *s = ss + i + 1;
  return (*s ? 1 : 0);             // Caller should invoke again
}

uint8_t prompt_for_name(char *, uint8_t); // Forward declaration

/*
 * OK to d/l attachment?
 */
char prompt_okay_attachment(char *filename) {
  char c;
  while (1) {
    printf("ProDOS filename:  %s\n", filename);
    printf("%c                                                      A)ccept | S)kip | R)ename%c\n", INVERSE, NORMAL);
    c = cgetc();
    switch (c) {
    case 'A':
    case 'a':
      return 1;
      break;
    case 'S':
    case 's':
      return 0;
      break;
    case 'R':
    case 'r':
      c = wherey();
      if (prompt_for_name("Save As", 2) == 255) {
        gotoxy(0, c);
        break; // ESC pressed
      }
      if (strlen(userentry) > 0) {
        if (userentry[0] == '/')
          strcpy(filename, userentry);
        else
          snprintf(filename, 80, "%s/ATTACHMENTS/%s", cfg_emaildir, userentry);
      }
      gotoxy(0, c);
      break;
    default:
      putchar(BELL);
    }
  } 
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
    if (j == 15) {
      s[j] = '\0';
      break;
    }
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
        __asm__("sta $c000"); // Turn off 80STORE
        __asm__("sec");       // Copy main->aux
        __asm__("jsr $c311"); // AUXMOVE
        __asm__("sta $c001"); // Turn on 80STORE
    } else {
        __asm__("sta $c000"); // Turn off 80STORE
        __asm__("clc");       // Copy aux->main
        __asm__("jsr $c311"); // AUXMOVE
        __asm__("sta $c001"); // Turn on 80STORE
    }
}

/*
 * Save the current screen to the scrollback file
 */
void save_screen_to_scrollback(FILE *fp) {
  if (fwrite((void*)0x0400, 0x0400, 1, fp) != 1) { // Even cols
    error(ERR_NONFATAL, "Can't write scrollback");
    return;
  }
  copyaux((void*)0x400, halfscreen, 0x400, FROMAUX);
  if (fwrite(halfscreen, 0x0400, 1, fp) != 1) { // Odd cols
    error(ERR_NONFATAL, "Can't write scrollback");
    return;
  }
}

/*
 * Load a screen from the scrollback file
 * Screens are numbered 1, 2, 3 ...
 * Does not trash the screen holes, which must be preserved!
 */
void load_screen_from_scrollback(FILE *fp, uint8_t screen) {
  if (fseek(fp, (screen - 1) * 0x0800, SEEK_SET)) {
    error(ERR_NONFATAL, "Can't seek scrollback");
    return;
  }
  if (fread(halfscreen, 0x0400, 1, fp) != 1) { // Even cols
    error(ERR_NONFATAL, "Can't read scrollback");
    return;
  }
  memcpy((void*)0x400, halfscreen + 0x000, 0x078);
  memcpy((void*)0x480, halfscreen + 0x080, 0x078);
  memcpy((void*)0x500, halfscreen + 0x100, 0x078);
  memcpy((void*)0x580, halfscreen + 0x180, 0x078);
  memcpy((void*)0x600, halfscreen + 0x200, 0x078);
  memcpy((void*)0x680, halfscreen + 0x280, 0x078);
  memcpy((void*)0x700, halfscreen + 0x300, 0x078);
  memcpy((void*)0x780, halfscreen + 0x380, 0x078);
  if (fread(halfscreen, 0x0400, 1, fp) != 1) { // Odd cols
    error(ERR_NONFATAL, "Can't read scrollback");
    return;
  }
  copyaux(halfscreen + 0x000, (void*)0x400, 0x078, TOAUX);
  copyaux(halfscreen + 0x080, (void*)0x480, 0x078, TOAUX);
  copyaux(halfscreen + 0x100, (void*)0x500, 0x078, TOAUX);
  copyaux(halfscreen + 0x180, (void*)0x580, 0x078, TOAUX);
  copyaux(halfscreen + 0x200, (void*)0x600, 0x078, TOAUX);
  copyaux(halfscreen + 0x280, (void*)0x680, 0x078, TOAUX);
  copyaux(halfscreen + 0x300, (void*)0x700, 0x078, TOAUX);
  copyaux(halfscreen + 0x380, (void*)0x780, 0x078, TOAUX);
  if (fseek(fp, 0, SEEK_END)) {
    error(ERR_NONFATAL, "Can't seek scrollback");
    return;
  }
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
  const int8_t *b = b64dec - 43;
  FILE *attachfp;
  uint16_t linecount, chars, skipbytes;
  uint8_t mime_enc, mime_binary, mime_hasfile, eof,
          screennum, maxscreennum, attnum;
  char c, *readp, *writep;
  clrscr2();
  snprintf(filename, 80, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    if (sbackfp)
      fclose(sbackfp);
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  pos = h->skipbytes;
  fseek(fp, pos, SEEK_SET); // Skip over headers
  mime_enc = ENC_7BIT;
restart:
  eof = 0;
  linecount = 0;
  readp = linebuf;
  writep = linebuf;
  attachfp = NULL;
  mime_binary = 0;
  mime_hasfile = 0;
  attnum = 0;
  if (sbackfp)
    fclose(sbackfp);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  snprintf(filename, 80, "%s/SCROLLBACK", cfg_emaildir);
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
  if (strncmp(h->to, "News:", 5) == 0) {
    fputs("\nNewsgrp: ", stdout);
    printfield(&(h->to[5]), 0, 70);
    if (h->cc[0] != '\0') {
      fputs("\nOrg:     ", stdout);
      printfield(h->cc, 0, 70);
    }
  } else {
    fputs("\nTo:      ", stdout);
    printfield(h->to, 0, 70);
    if (h->cc[0] != '\0') {
      fputs("\nCC:      ", stdout);
      printfield(h->cc, 0, 70);
    }
  }
  fputs("\nSubject: ", stdout);
  printfield(h->subject, 0, 70);
  fputs("\n\n", stdout);

  // We do not need all the email headers for the summary screen right now.
  // Freeing them can release up to nearly 8KB. The caller rebuilds the
  // summary info by calling read_email_db().
  skipbytes = h->skipbytes;
  free_headers_list();

  get_line(fp, 1, linebuf, &pos); // Reset buffer
  while (1) {
    if (!readp)
      readp = linebuf;
    if (!writep)
      writep = linebuf;
    if (get_line(fp, 0, writep, &pos) == -1) {
      eof = 1;
      goto endscreen;
    }
    ++linecount;
    if ((mime >= 1) && (!strncmp(writep, "--", 2))) {
      if (attachfp)
        fclose(attachfp);
      if ((mime == 4) && mime_hasfile) {
        putchar(BACKSPACE); // Erase spinner
        puts("[OK]");
      }
      attachfp = NULL;
      mime = 2;
      mime_enc = ENC_7BIT;
      mime_binary = 0;
      mime_hasfile = 0;
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
        else if (!strncmp(writep + 27, "8bit", 4))
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
        mime_hasfile = 1;
        snprintf(filename, 80, "%s", strstr(writep, "filename=") + 9);
        printf("%cAttachment %u                                                                   %c\n",
               INVERSE, ++attnum, NORMAL); 
        printf("  MIME filename:  %s", filename);
        sanitize_filename(filename);
        snprintf(userentry, 80, "%s/ATTACHMENTS/%s",
                cfg_emaildir, filename);
        strcpy(filename, userentry);
prompt_dl:
        if (prompt_okay_attachment(filename)) {
          printf("*** Attachment -> %s  ", filename);
          attachfp = fopen(filename, "wb");
          if (!attachfp) {
            printf("\n*** Can't open %s %d\n", filename, errno);
            goto prompt_dl;
          }
        } else
          attachfp = NULL;
      } else if ((mime == 3) && (!strncmp(writep, "\r", 1))) {
        mime = 4;
        if (!attachfp && mime_hasfile) {
          mime_enc = ENC_SKIP; // Skip over MIME parts user chose to skip
          printf("*** Skipping      %s  ", filename);
        } else if (!attachfp && mime_binary) {
          mime_enc = ENC_SKIP; // Skip over binary MIME parts with no filename
          printf("\n");
        }
      }
      readp = writep = NULL;
    } else if (mime == 4) {
      switch (mime_enc) {
      case ENC_QP:
        chars = decode_quoted_printable(writep);
        break;
       case ENC_B64:
        //chars = decode_base64(writep);
        {
        uint8_t i = 0, j = 0;
        while (writep[i] != '\r') {
          writep[j++] = b[writep[i]] << 2 | b[writep[i + 1]] >> 4;
          if (writep[i + 2] != '=')
            writep[j++] = b[writep[i + 1]] << 4 | b[writep[i + 2]] >> 2;
          if (linebuf[i + 3] != '=')
            writep[j++] = b[writep[i + 2]] << 6 | b[writep[i + 3]];
          i += 4;
        }
        chars = j;
        }
        break;
       case ENC_SKIP:
        readp = writep = NULL;
        break;
      }
      if (mime_hasfile && !(linecount % 10))
        spinner();
    }
    if (readp) {
      if ((mime == 0) || ((mime == 4) && !mime_hasfile)) {
        do {
          c = word_wrap_line(stdout, &readp, 80, 0);
          if (*cursorrow == 22)
            break; 
        } while (c == 1);
        if (readp) {
          chars = strlen(readp);
          memmove(linebuf, readp, strlen(readp));
          readp = linebuf;
          writep = linebuf + chars;
        } else
          writep = NULL;
      }
      if ((mime == 4) && mime_hasfile) {
        if (attachfp)
          fwrite(readp, 1, chars, attachfp);
        readp = writep = NULL;
      }
      if (mime == 1) {
        readp = writep = NULL;
      }
    }
endscreen:
    if (!mime_hasfile && ((*cursorrow == 22) || eof)) {
      printf("\n%c[%07lu] %s         | B)ack | T)op | H)drs | M)IME | Q)uit%c",
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
        pos = skipbytes;
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
        pos = 0;
        fseek(fp, pos, SEEK_SET);
        get_line(fp, 1, linebuf, &pos); // Reset buffer
        do {
          get_line(fp, 0, linebuf, &pos);
          if (!strncasecmp(linebuf, "Content-Transfer-Encoding: ", 27)) {
            mime = 4;
            if (!strncmp(linebuf + 27, "7bit", 4))
              mime_enc = ENC_7BIT;
            else if (!strncmp(linebuf + 27, "8bit", 4))
              mime_enc = ENC_7BIT;
            else if (!strncmp(linebuf + 27, "quoted-printable", 16))
              mime_enc = ENC_QP;
            else if (!strncmp(linebuf + 27, "base64", 6))
              mime_enc = ENC_B64;
            else {
              printf("** Unsupp encoding %s\n", linebuf + 27);
              mime = 1;
            }
            break;
          }
        } while (linebuf[0] != '\r');
        pos = skipbytes;
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
  snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "rb+");
  if (!fp)
    error(ERR_FATAL, "Can't open %s", filename);
  if (fseek(fp, (uint32_t)(pos - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET))
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
  snprintf(filename, 80, "%s/%s", cfg_emaildir, mbox);
  if (mkdir(filename)) {
    error(ERR_NONFATAL, "Can't create dir %s", filename);
    return;
  }
  snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't create EMAIL.DB");
    return;
  }
  fclose(fp);
  snprintf(filename, 80, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
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
  snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  snprintf(filename, 80, "%s/%s/EMAIL.DB.NEW", cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    fclose(fp);
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
  while (1) {
    l = fread(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK)
      goto done;
    if (h->status == 'D') {
      snprintf(filename, 80, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
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
  snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  if (unlink(filename)) {
    error(ERR_NONFATAL, "Can't delete %s", filename);
    return;
  }
  snprintf(userentry, 80, "%s/%s/EMAIL.DB.NEW", cfg_emaildir, curr_mbox);
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
  snprintf(filename, 80, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
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
  snprintf(filename, 80, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
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
 *           if 2, restrict chars to those allowed in ProDOS path
 * Returns number of chars read, or 255 if ESC pressed
 */
uint8_t prompt_for_name(char *prompt, uint8_t is_file) {
  uint16_t i;
  char c;
  goto_prompt_row();
  printf("%s>", prompt);
  i = 0;
  while (1) {
    c = cgetc();
    if ((is_file > 0) && !isalnum(c) && (c != RETURN) && (c != BACKSPACE) &&
        (c != DELETE) && (c != ESC) && (c != '.') && (c != '/')) {
      putchar(BELL);
      continue;
    }
    if ((is_file == 1) && (c == '/')) {
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
    case ESC:
      userentry[0] = '\0';
      i = 255;
      goto esc_pressed;
    default:
      putchar(c);
      userentry[i++] = c;
    }
    if (i == 79)
      goto done;
  }
done:
  userentry[i] = '\0';
esc_pressed:
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
 * Returns 0 if okay, 1 on error, 255 if ESC pressed
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
  if (prompt_for_name("cc", 0) == 255)
    return 255; // ESC pressed
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
 * Returns 1 for yes
 *         0 for no or ESC
 */
char prompt_okay(char *msg) {
  char c;
  goto_prompt_row();
  printf("%sSure? (y/n/ESC)", msg);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N') || (c == ESC))
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
 * Obtain the body of an email to include in a reply or forwarded message
 * For a plain text email, the body is everything after the headers
 * For a MIME multipart email, we take the first Text/Plain section
 * Email file to read is expected to be already open using fp
 * f - File handle for destination file (also already open)
 * mode - 'R' if reply, 'F' if forward
 */
void get_email_body(struct emailhdrs *h, FILE *f, char mode) {
  uint32_t pos = 0;
  uint8_t mime = 0;
  const int8_t *b = b64dec - 43;
  uint16_t chars;
  uint8_t  mime_enc, mime_binary;
  char c, *readp, *writep;
  mime = 0;
  pos = 0;
  fseek(fp, pos, SEEK_SET);
  get_line(fp, 1, linebuf, &pos); // Reset buffer
  do {
    spinner();
    get_line(fp, 0, linebuf, &pos);
    if (!strncasecmp(linebuf, "MIME-Version: 1.0", 17))
      mime = 1;
    if (!strncasecmp(linebuf, "Content-Transfer-Encoding: ", 27)) {
      mime = 4;
      if (!strncmp(linebuf + 27, "7bit", 4))
        mime_enc = ENC_7BIT;
      else if (!strncmp(linebuf + 27, "8bit", 4))
        mime_enc = ENC_7BIT;
      else if (!strncmp(linebuf + 27, "quoted-printable", 16))
        mime_enc = ENC_QP;
      else if (!strncmp(linebuf + 27, "base64", 6))
        mime_enc = ENC_B64;
      else {
        error(ERR_NONFATAL, "Unsupp encoding %s\n", linebuf + 27);
        return;
      }
      break;
    }
  } while (linebuf[0] != '\r');
  pos = h->skipbytes;
  fseek(fp, pos, SEEK_SET);
  readp = linebuf;
  writep = linebuf;
  get_line(fp, 1, linebuf, &pos); // Reset buffer
  while (1) {
    if (!readp)
      readp = linebuf;
    if (!writep)
      writep = linebuf;
    if (get_line(fp, 0, writep, &pos) == -1)
      break;
    if ((mime >= 1) && (!strncmp(writep, "--", 2))) {
      if ((mime == 4) && !mime_binary) // End of Text/Plain MIME section
        break;
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
        else if (!strncmp(writep + 27, "8bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, "quoted-printable", 16))
          mime_enc = ENC_QP;
        else if (!strncmp(writep + 27, "base64", 6))
          mime_enc = ENC_B64;
        else {
          printf("** Unsupp encoding %s\n", writep + 27);
          mime = 1;
        }
      } else if ((mime == 3) && (!strncmp(writep, "\r", 1))) {
        mime = 4;
        if (mime_binary)
          mime_enc = ENC_SKIP; // Skip over binary MIME parts
      }
      readp = writep = NULL;
    } else if (mime == 4) {
      switch (mime_enc) {
      case ENC_QP:
        chars = decode_quoted_printable(writep);
        break;
       case ENC_B64:
        //chars = decode_base64(writep);
        {
        uint8_t i = 0, j = 0;
        while (writep[i] != '\r') {
          writep[j++] = b[writep[i]] << 2 | b[writep[i + 1]] >> 4;
          if (writep[i + 2] != '=')
            writep[j++] = b[writep[i + 1]] << 4 | b[writep[i + 2]] >> 2;
          if (linebuf[i + 3] != '=')
            writep[j++] = b[writep[i + 2]] << 6 | b[writep[i + 3]];
          i += 4;
        }
        chars = j;
        }
        break;
       case ENC_SKIP:
        readp = writep = NULL;
        break;
      }
    }
    if (readp) {
      if ((mime == 0) || ((mime == 4) && !mime_binary)) {
        do {
          c = word_wrap_line(f, &readp, 78, mode);
        } while (c == 1);
        if (readp) {
          chars = strlen(readp);
          memmove(linebuf, readp, strlen(readp));
          readp = linebuf;
          writep = linebuf + chars;
        } else
          writep = NULL;
      }
      if ((mime == 4) && mime_binary) {
        readp = writep = NULL;
      }
      if (mime == 1) {
        readp = writep = NULL;
      }
    }
  }
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
  uint16_t num, buflen, l, written;
  FILE *fp2;

  if (mode == 'F') {
    if (prompt_for_name("Fwd to", 0) == 255)
      return; // ESC pressed
    if (strlen(userentry) == 0)
      return;
  }

  // Read next number from dest/NEXT.EMAIL
  if (get_next_email(mbox, &num))
    return;

  // Open source email file
  snprintf(filename, 80, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  // Open destination email file
  snprintf(filename, 80, "%s/%s/EMAIL.%u", cfg_emaildir, mbox, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    fclose(fp);
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  if (mode != ' ')
    l = write_email_headers(fp, fp2, h, mode, userentry);
    if (l == 1)
      error(ERR_NONFATAL, "Invalid email header");
    if ((l == 1) || (l == 255)) {
      fclose(fp);
      fclose(fp2);
      return;
    }

  // Make sure spinner is in the right place
  if ((mode == 'R') || (mode == 'F'))
    goto_prompt_row();

  // Copy email body
  putchar(' '); // For spinner
  if (mode == 'R')
    fputc('>', fp2);
  if ((mode == 'R') || (mode == 'F')) {
    get_email_body(h, fp2, mode);
  } else {
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
  }

  putchar(BACKSPACE);
  putchar(' ');
  putchar(BACKSPACE);

  fclose(fp);
  fclose(fp2);

  // Update dest/EMAIL.DB unless this is R)eply or F)orward
  // The upshot of this is we never create EMAIL.DB in OUTBOX
  if (mode == ' ') {
    snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
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
    snprintf(filename, 80, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
    load_editor(1);
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
  snprintf(filename, 80, "%u tagged - ", total_tag);
  if (!prompt_okay(filename))
    return 0;
  h = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
  if (!h)
    error(ERR_FATAL, "Can't malloc()");
  while (1) {
    snprintf(filename, 80, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
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
  snprintf(filename, 80, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  fprintf(fp, "From: %s\r", cfg_emailaddr);
  if (prompt_for_name("To", 0) == 255)
    goto done; // ESC pressed
  if (strlen(userentry) == 0)
    goto done;
  fprintf(fp, "To: %s\r", userentry);
  if (prompt_for_name("Subject", 0) == 255)
    goto done; // ESC pressed
  fprintf(fp, "Subject: %s\r", userentry);
  readdatetime((unsigned char*)(SYSTEMTIME), &dt);
  datetimelong(&dt, userentry);
  fprintf(fp, "Date: %s\r", userentry);
  if (prompt_for_name("cc", 0) == 255)
    goto done; // ESC pressed
  if (strlen(userentry) > 0)
    fprintf(fp, "cc: %s\r", userentry);
  fprintf(fp, "X-Mailer: %s - Apple II Forever!\r\r", PROGNAME);
  fclose(fp);

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email("OUTBOX", num + 1))
    return;

  snprintf(filename, 80, "%s/OUTBOX/EMAIL.%u", cfg_emaildir, num);
  load_editor(1);
done:
  fclose(fp);
}

/*
 * Display help screen
 */
#pragma code-name (push, "LC")
void help(void) {
  clrscr2();
  printf("%c%s HELP%c\n", INVERSE, PROGNAME, NORMAL);
  puts("------------------------------------------+------------------------------------");
  puts("Email Summary Screen                      | Email Pager");
  puts("  [Up]   / K        Previous message      |   [Space]   Page forward");
  puts("  [Down] / J        Next message          |   B         Page back");
  puts("  [Space] / [Ret]   Read current message  |   T         Go to top");
  puts("  Q                 Quit to ProDOS        |   M         MIME mode");
  puts("------------------------------------------+   H         Show email headers");
  puts("Message Management                        |   Q         Return to summary");     
  puts("  S   Switch mailbox                      +------------------------------------");
  puts("  N   Create new mailbox");
  puts("  T   Tag current message for group Archive/Move/Copy");
  puts("  A   Archive current/tagged message to 'received' mailbox");
  puts("  C   Copy current/tagged message to another mailbox");
  puts("  M   Move current/tagged message to another mailbox");
  puts("  D   Mark current message deleted");
  puts("  U   Undelete current message if marked deleted");
  puts("  P   Purge messages marked as deleted");
  puts("------------------------------------------+------------------------------------");
  puts("Message Composition                       | Invoke Helper Programs");
  printf("  W   Write an email message              |   %s-R     Retrieve messages\n", openapple);
  printf("  R   Reply to current message            |   %s-S     Send outbox\n", openapple);
  printf("  F   Forward current message             |   %s-D     Set date using NTP\n", openapple);
  fputs("------------------------------------------+------------------------------------", stdout);
  cgetc();
}
#pragma code-name (pop)

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
        read_email_db(first_msg, 0, 0); // email_pager() deletes the headers
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
      if (h) {
        c = prompt_for_name("Copy to mbox", 1);
        if ((c != 0) && (c != 255))
          copy_to_mailbox_tagged(userentry, 0);
      }
      break;
    case 'm':
    case 'M':
      if (h) {
        c = prompt_for_name("Move to mbox", 1);
        if ((c != 0) && (c != 255))
          copy_to_mailbox_tagged(userentry, 1);
      }
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
      c = prompt_for_name("New mbox", 1);
      if ((c != 0) && (c != 255))
        new_mailbox(userentry);
      break;
    case 's':
    case 'S':
      c = prompt_for_name("Switch mbox", 1);
      if ((c != 0) && (c != 255))
        switch_mailbox(userentry);
      break;
    case 'w':
    case 'W':
      create_blank_outgoing();
      break;
    case ',':
    case '<':
      reverse = 1;
      save_prefs();
      switch_mailbox(curr_mbox);
      break;
    case '.':
    case '>':
      reverse = 0;
      save_prefs();
      switch_mailbox(curr_mbox);
      break;
    case 0x80 + 'd': // OA-D "Update date using NTP"
    case 0x80 + 'D':
      load_date65();
      break;
    case 0x80 + 'e': // OA-E "Open message in editor"
    case 0x80 + 'E':
      snprintf(filename, 80, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
      load_editor(0);
      break;
    case 0x80 + 'r': // OA-R "Retrieve messages from server"
    case 0x80 + 'R':
      load_pop65();
      break;
    case 0x80 + 's': // OA-S "Send messages in Outbox to server"
    case 0x80 + 'S':
      load_smtp65();
      break;
    case 0x80 + '?': // OA-? "Help"
      help();
      email_summary();
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
  load_prefs();
  first_msg = 1;
  read_email_db(first_msg, 1, 0);
  selection = 1;
  email_summary();
  keyboard_hdlr();
}

