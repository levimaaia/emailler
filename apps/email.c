/////////////////////////////////////////////////////////////////
// emai//er - Simple Email User Agent vaguely inspired by Elm
// Handles INBOX in the format created by POP65
// Bobbi June 2020 - May 2021
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
#define LINEBUFSZ     1000   // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ        512    // Size of buffer for copying files

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
#define KEYBOARD      0xc000

static char closedapple[]  = "\x0f\x1b""@\x18\x0e";
static char openapple[]    = "\x0f\x1b""A\x18\x0e";
static char email[]        = "EMAIL";
static char email_cfg[]    = "EMAIL.CFG";
static char email_prefs[]  = "EMAIL.PREFS";
static char email_db[]     = "%s/%s/EMAIL.DB";
static char email_db_new[] = "%s/%s/EMAIL.DB.NEW";
static char next_email[]   = "%s/%s/NEXT.EMAIL";
static char email_file[]   = "%s/%s/EMAIL.%u";
static char inbox[]        = "INBOX";
static char outbox[]       = "OUTBOX";
static char news_outbox[]  = "NEWS.OUTBOX";
static char cant_open[]    = "Can't open %s";
static char cant_seek[]    = "Can't seek in %s";
static char cant_malloc[]  = "Can't alloc";
static char cant_delete[]  = "Can't delete %s";
static char cant_write[]   = "Can't write to %s";
static char mime_ver[]     = "MIME-Version: 1.0";
static char ct[]           = "Content-Type: ";
static char cte[]          = "Content-Transfer-Encoding: ";
static char qp[]           = "quoted-printable";
static char b64[]          = "base64";
static char unsupp_enc[]   = "** Unsupp encoding %s\n";
static char sb_err[]       = "Scrollback error";
static char a2_forever[]   = "%s: %s - Apple II Forever!\r\r";

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

static char              filename[80];
static char              userentry[80];
static char              linebuf[LINEBUFSZ];
static char              halfscreen[0x0400];
static FILE              *fp;
static struct emailhdrs  *headers;
static uint16_t          selection = 1;
static uint16_t          prevselection;
static uint16_t          num_msgs;        // Num of msgs shown in current page
static uint16_t          total_msgs;      // Total number of message in mailbox
static uint16_t          total_new;       // Total number of new messages
static uint16_t          total_tag;       // Total number of tagged messages
static uint16_t          first_msg = 1;   // Msg numr: first message current page
static uint8_t           reverse = 0;     // 0 normal, 1 reverse order
static char              curr_mbox[80] = "INBOX";
static unsigned char     buf[READSZ];

#define ERR_NONFATAL 0
#define ERR_FATAL    1

/*
 * Save preferences
 */
#pragma code-name (push, "LC")
void save_prefs(void) {
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(email_prefs, "wb");
  if (!fp)
    return;
  fprintf(fp, "o:%c\n", (reverse ? '<' : '>'));
  fprintf(fp, "m:%s\n", curr_mbox);
  fprintf(fp, "f:%d\n", first_msg);
  fprintf(fp, "s:%d\n", selection);
  fclose(fp);
}
#pragma code-name (pop)

/*
 * Load preferences
 */
#pragma code-name (push, "LC")
void load_prefs(void) {
  char order = 'a';
  fp = fopen(email_prefs, "rb");
  if (!fp)
    return;
  fscanf(fp, "o:%c\n", &order);
  fscanf(fp, "m:%s\n", curr_mbox);
  fscanf(fp, "f:%d\n", &first_msg);
  fscanf(fp, "s:%d\n", &selection);
  fclose(fp);
  reverse = (order == '<' ? 1 : 0);
}
#pragma code-name (pop)

/*
 * Load and run EDIT.SYSTEM
 * compose - Controls the arguments passed to EDIT.SYSTEM
 *           0: Email reading (-reademail)
 *           1: Email composition (-email)
 *           2: News composition (-news)
 */
#pragma code-name (push, "LC")
void load_editor(uint8_t compose) {
  save_prefs();
  snprintf(userentry, 80, "%s %s",
           (compose == 0 ? "-reademail" : (compose == 1 ? "-email" : "-news")),
           filename);
  snprintf(filename, 80, "%s/EDIT.SYSTEM", cfg_instdir);
  exec(filename, userentry);
}
#pragma code-name (pop)

/*
 * Load and run NNTP65.SYSTEM
 */
#pragma code-name (push, "LC")
void load_nntp65(void) {
  save_prefs();
  snprintf(filename, 80, "%s/NNTP65.SYSTEM", cfg_instdir);
  exec(filename, email);
}
#pragma code-name (pop)

/*
 * Load and run NNTP65UP.SYSTEM
 */
#pragma code-name (push, "LC")
void load_nntp65up(void) {
  save_prefs();
  snprintf(filename, 80, "%s/NNTP65UP.SYSTEM", cfg_instdir);
  exec(filename, email);
}
#pragma code-name (pop)

/*
 * Load and run POP65.SYSTEM
 */
#pragma code-name (push, "LC")
void load_pop65(void) {
  save_prefs();
  snprintf(filename, 80, "%s/POP65.SYSTEM", cfg_instdir);
  exec(filename, email);
}
#pragma code-name (pop)

/*
 * Load and run SMTP65.SYSTEM
 */
#pragma code-name (push, "LC")
void load_smtp65(void) {
  save_prefs();
  snprintf(filename, 80, "%s/SMTP65.SYSTEM", cfg_instdir);
  exec(filename, email);
}
#pragma code-name (pop)

/*
 * Load and run DATE65.SYSTEM
 */
#pragma code-name (push, "LC")
void load_date65(void) {
  save_prefs();
  snprintf(filename, 80, "%s/DATE65.SYSTEM", cfg_instdir);
  exec(filename, email);
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

void status_bar(void); // Forward declaration

/*
 * Check for key pressed. If so, call cgetc().
 * Otherwise spin and update the status line waiting for a keypress.
 */
char cgetc_update_status() {
  uint16_t ctr = 0;
  while (*((char*)KEYBOARD) < 128) {
    ++ctr;
    if (ctr == 20000) {
      status_bar();
      goto_prompt_row();
      ctr = 0;
    }
  }
  return cgetc();
}

/*
 * Show non fatal error in PROMPT_ROW
 * Fatal errors are shown on a blank screen
 */
#pragma code-name (push, "LC")
void error(uint8_t fatal, const char *fmt, ...) {
  va_list v;
  va_start(v, fmt);
  if (fatal) {
    clrscr2();
    printf("\n\n%cFATAL ERROR:%c\n\n", INVERSE, NORMAL);
    vprintf(fmt, v);
    printf("\n\n\n\n[Press Any Key To Quit]");
    cgetc();
    exit(1);
  } else {
    goto_prompt_row();
    putchar(CLRLINE);
    vprintf(fmt, v);
    printf(" - [Press Any Key]");
    cgetc();
    putchar(CLRLINE);
  }
  va_end(v);
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
 * Print ASCII-art envelope
 */
#if 0
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
#endif

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
  fp = fopen(email_cfg, "r");
  if (!fp)
    error(ERR_FATAL, cant_open, email_cfg);
  fscanf(fp, "%s%s%s%s%s%s%s%s%s", cfg_server, cfg_user, cfg_pass,
                                   cfg_pop_delete, cfg_smtp_server,
                                   cfg_smtp_domain, cfg_instdir,
                                   cfg_emaildir, cfg_emailaddr);
  fclose(fp);
}
#pragma code-name (pop)

/*
 * Convert date/time bytes into struct datetime format.
 */
#pragma code-name (push, "LC")
void readdatetime(struct datetime *dt) {
    unsigned char *time = (unsigned char*)SYSTEMTIME;
    unsigned int d, t;
    __asm__("jsr $bf06"); // ProDOS DATETIME call: Updates vals at SYSTEMTIME
    d = time[0] + 256U * time[1];
    t = time[2] + 256U * time[3];
    if ((d | t) == 0) {
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
  readdatetime(&dt);
  printdatetime(&dt);
}
#pragma code-name (pop)

/*
 * Free linked list rooted at headers
 */
#pragma code-name (push, "LC")
void free_headers_list(void) {
  struct emailhdrs *h = headers;
  while (h) {
    free(h);
    h = h->next; // Not strictly legal, but will work
  }
  headers = NULL;
}
#pragma code-name (pop)

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
  snprintf(filename, 80, email_db, cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(switchmbox ? ERR_NONFATAL : ERR_FATAL, cant_open, filename);
    if (switchmbox)
      return 1;
  }
  if (reverse) {
    if (fseek(fp, 0, SEEK_END)) {
      fclose(fp);
      error(switchmbox ? ERR_NONFATAL : ERR_FATAL, cant_seek, filename);
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
      error(switchmbox ? ERR_NONFATAL : ERR_FATAL, cant_seek, filename);
      if (switchmbox)
        return 1;
    }
  }
  num_msgs = 0;
  goto_prompt_row();
  putchar(CLRLINE);
  fputs("Loading  ", stdout);
  while (1) {
    spinner();
    curr = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
    if (!curr)
      error(ERR_FATAL, cant_malloc);
    curr->next = NULL;
    curr->tag = ' ';
    l = fread(curr, 1, EMAILHDRS_SZ_ON_DISK, fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK) {
      free(curr);
      break;
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
        break;
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
      if (pos == -1L * EMAILHDRS_SZ_ON_DISK)
        break;
      if (fseek(fp, pos, SEEK_SET)) {
        error(switchmbox ? ERR_NONFATAL : ERR_FATAL, cant_seek, filename);
        break;
      }
    }
  }
  fclose(fp);
  return 0;
}
#pragma code-name (pop)

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
uint16_t decode_base64(char *p) {
  uint16_t i = 0, j = 0;
  const int8_t *b = b64dec - 43;
  while (p[i] && (p[i] != '\r') && (p[i] != '?')) {
    p[j++] = b[p[i]] << 2 | b[p[i + 1]] >> 4;
    if (p[i + 2] != '=')
      p[j++] = b[p[i + 1]] << 4 | b[p[i + 2]] >> 2;
    if (p[i + 3] != '=')
      p[j++] = b[p[i + 2]] << 6 | b[p[i + 3]];
    i += 4;
  }
  p[j] = '\0';
  return j;
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
 * Decode buffer from quoted-printable format in place
 * p - Pointer to buffer to decode. Results written in place.
 * Returns number of bytes decoded
 */
uint16_t decode_quoted_printable(uint8_t *p) {
  uint16_t i = 0, j = 0;
  uint8_t c;
  while (c = p[i]) {
    if (c == '=') {
      if (p[i + 1] == '\r') // Trailing '=' is a soft '\r'
        break;
      // Otherwise '=xx' where x is a hex digit
      c = 16 * hexdigit(p[i + 1]) + hexdigit(p[i + 2]);
      p[j++] = c;
      i += 3;
    } else if (c == '?')
      break;
    else {
      p[j++] = c;
      ++i;
    }
  }
  p[j] = '\0';
  return j;
}

/*
 * Print a header field from char postion start to end,
 * padding with spaces as needed
 */
#pragma code-name (push, "LC")
void printfield(char *s, uint8_t start, uint8_t end) {
  uint8_t i;
  uint8_t l = strlen(s);
  for (i = start; i < end; i++)
    putchar(i < l ? s[i] : ' ');
}
#pragma code-name (pop)

/*
 * Decode Subject header which may be encoded Quoted-Printable or Base64
 * p - pointer to subject header content
 * Decoded (and sanitized) text is returned in linebuf[]
 */
void decode_subject(char *p) {
  uint8_t i = 0, j = 0;
  if (strncasecmp(p, "=?utf-8?", 8) == 0) {
    strcpy(linebuf, p + 10); // Skip '=?UTF-8?x?'
    if (p[8] == 'B')
      decode_base64(linebuf);
    else
      decode_quoted_printable(linebuf);
    while (linebuf[i]) {
      if ((linebuf[i] <= 127) && (linebuf[i] >= 32))
        linebuf[j++] = linebuf[i];
      else if (linebuf[i] > 191)     // 11xxxxxx
        linebuf[j++] = '#';
      ++i;
    }
    linebuf[j] = '\0';
  } else
    strcpy(linebuf, p);
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
  decode_subject(h->subject);
  printfield(linebuf, 0, 39);
  putchar(NORMAL);
}

/*
 * Get emailhdrs for nth email in list of headers
 */
#pragma code-name (push, "LC")
struct emailhdrs *get_headers(uint16_t n) {
  uint16_t i = 1;
  struct emailhdrs *h = headers;
  while (h && (i < n)) {
    ++i;
    h = h->next;
  }
  return h;
}
#pragma code-name (pop)

/*
 * Print status bar at the top
 */
#define MAXSTATLEN 62
void status_bar(void) {
  uint8_t i;
  if (num_msgs == 0) {
    sprintf(linebuf, "%s [%s] No messages ", PROGNAME, curr_mbox);
    //envelope();
  } else
    sprintf(linebuf, "[%s] %u msgs, %u new, %u tagged. Showing %u-%u. %c ",
           curr_mbox, total_msgs, total_new, total_tag, first_msg,
           first_msg + num_msgs - 1, (reverse ? '<' : '>'));
  if (strlen(linebuf) > MAXSTATLEN) {
    linebuf[MAXSTATLEN] = '\0';
    linebuf[MAXSTATLEN-3] = linebuf[MAXSTATLEN-2] = linebuf[MAXSTATLEN-1] = '.';
  }
  putchar(HOME);
  putchar(INVERSE);
  fputs(linebuf, stdout);
  for (i = 0; i < MAXSTATLEN+2 - strlen(linebuf); ++i)
    fputc(' ', stdout);
  printsystemdate();
  putchar(HOME);
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
#pragma code-name (push, "LC")
void update_highlighted(void) {
  email_summary_for(prevselection);
  email_summary_for(selection);
}
#pragma code-name (pop)

/*
 * Read a text file a line at a time
 * Returns number of chars in the line, or 0 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * reset - if 1 then just reset the buffer and return
 * writep - Pointer to buffer into which line will be written
 * n - length of buffer. Longer lines will be truncated and terminated with CR.
 * pos - position in file is updated via this pointer
 */
uint16_t get_line(FILE *fp, uint8_t reset, char *writep, uint16_t n, uint32_t *pos) {
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
      goto done;
    if (i == n - 1) {
      writep[i - 1] = '\r';
      goto done;
    }
    writep[i++] = buf[rd++];
    ++(*pos);
    if (writep[i - 1] == '\r')
      goto done;
  }
done:
  writep[i] = '\0';
  return i;
}

/*
 * Print line up to first '\r' or '\0'
 */
void putline(FILE *f, char *s) {
  while ((*s != NULL) && (*s != '\r')) {
    if ((*s <= 127) && (*s >= 32))
      fputc(*s, f);
    else if (*s > 191)   // 11xxxxxx
      fputc('#', f);
    ++s;
  }
}

/*
 * Perform word wrapping, for a line of text, which may contain multiple
 * embedded '\r' carriage returns, or no carriage return at all.
 * fp - File handle to use for output.
 * s - Pointer to pointer to input buffer. If all text is consumed, this is
 *     set to NULL.  If there is text left in the buffer to be consumed then
 *     the pointer will be advanced to point to the next text to process.
 * cols - Number of columns to break at
 * mode - 'R' for reply, 'F' for forward, 'N' for news follow-up, otherwise ' '
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
    if ((col + l) <= cols) {         // Fits on this line
      col += l;
      putline(fp, ss);
      if (ret) {
        col = 0;
        if (col + l != cols) {
          fputc('\r', fp);
          if ((mode == 'R') || (mode == 'N')) {
            fputc('>', fp);
            ++col;
          }
        }
      }
      *s = nextline;
      return (*s ? 1 : 0);         // Caller should invoke again
    }
    i = cols - col;                  // Doesn't fit, need to break
    while ((ss[--i] != ' ') && (i > 0));
    if (i == 0) {                  // No space character found
      if (col == 0) {              // Doesn't fit on full line
        for (i = 0; i <= cols; ++i) { // Truncate @cols chars
          if ((ss[i] <= 127) && (ss[i] >= 32))
            fputc(ss[i], fp);
          else if (ss[i] > 191)    // 11xxxxxx
            fputc('#', fp);
        }
        *s = ss + l + 1;
      } else {                     // There is stuff on this line already
        col = 0;
        fputc('\r', fp);           // Try a blank line
        if ((mode == 'R') || (mode == 'N')) {
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
  if ((mode == 'R') || (mode == 'N')) {
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
    if (c == '\r')
      break;
    if (isalnum(c) || c == '.' || c == '/')
      s[j++] = c;
    if (j == 15) {
      break;
    }
  }
  s[j] = '\0';
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
    *a2 = src + len - 1;  // AUXMOVE moves length+1 bytes!!
    *a4 = dst;
    if (dir == TOAUX)
        __asm__("sec");   // Copy main->aux
    else
        __asm__("clc");   // Copy aux->main
    __asm__("sta $c000"); // Turn off 80STORE
    __asm__("jsr $c311"); // AUXMOVE
    __asm__("sta $c001"); // Turn on 80STORE
}

/*
 * Save the current screen to the scrollback file
 */
void save_screen_to_scrollback(FILE *fp) {
  if (fwrite((void*)0x0400, 0x0400, 1, fp) != 1) { // Even cols
    error(ERR_NONFATAL, sb_err);
    return;
  }
  copyaux((void*)0x400, halfscreen, 0x400, FROMAUX);
  if (fwrite(halfscreen, 0x0400, 1, fp) != 1) { // Odd cols
    error(ERR_NONFATAL, sb_err);
    return;
  }
}

/*
 * Load a screen from the scrollback file
 * Screens are numbered 1, 2, 3 ...
 * Does not trash the screen holes, which must be preserved!
 */
void load_screen_from_scrollback(FILE *fp, uint8_t screen) {
  uint8_t i;
  if (fseek(fp, (screen - 1) * 0x0800, SEEK_SET) ||
     (fread(halfscreen, 0x0400, 1, fp) != 1)) { // Even cols
    error(ERR_NONFATAL, sb_err);
    return;
  }
  for (i = 0; i < 8; ++i)
    memcpy((char*)0x400 + i * 0x80, halfscreen + i * 0x80, 0x078);
  if (fread(halfscreen, 0x0400, 1, fp) != 1) { // Odd cols
    error(ERR_NONFATAL, sb_err);
    return;
  }
  for (i = 0; i < 8; ++i)
    copyaux(halfscreen + i * 0x80, (char*)0x400 + i * 0x80, 0x078, TOAUX);
  if (fseek(fp, 0, SEEK_END)) {
    error(ERR_NONFATAL, sb_err);
    return;
  }
}

/*
 * Check if text at p is a MIME boundary.
 * p is assumed to point to start of line.
 * Valid MIME boundaries start with "--", then have a char sequence
 * with no spaces, then CR
 */
uint8_t is_mime_boundary(char *p) {
 if (strncmp(p, "--", 2))  // Must start with "--"
   return 0;
 p += 2;
 do {
   if (*p == ' ')               // Can not contain ' '
     return 0;
   ++p;
 } while(*p && (*p != '\r'));
 return 1;
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
  static struct emailhdrs hh;
  uint32_t pos = 0;
  uint8_t *cursorrow = (uint8_t*)CURSORROW, mime = 0;
  FILE *sbackfp = NULL;
  const int8_t *b = b64dec - 43;
  FILE *attachfp;
  uint16_t linecount, chars;
  uint8_t mime_enc, mime_binary, mime_hasfile, eof,
          screennum, maxscreennum, attnum;
  uint8_t c, *readp, *writep;

  // We do not need all the email headers for the summary screen right now.
  // Freeing them can release up to nearly 8KB. The caller rebuilds the
  // summary info by calling read_email_db().
  hh = *h;
  free_headers_list();

  clrscr2();
  snprintf(filename, 80, email_file, cfg_emaildir, curr_mbox, hh.emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    if (sbackfp)
      fclose(sbackfp);
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }
  pos = hh.skipbytes;
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
    error(ERR_NONFATAL, sb_err);
  }
  maxscreennum = screennum = 0;
  clrscr2();
  fputs("Date:    ", stdout);
  printfield(hh.date, 0, 39);
  fputs("\nFrom:    ", stdout);
  printfield(hh.from, 0, 70);
  if (strncmp(hh.to, "News:", 5) == 0) {
    fputs("\nNewsgrp: ", stdout);
    printfield(&(hh.to[5]), 0, 70);
    if (hh.cc[0] != '\0') {
      fputs("\nOrg:     ", stdout);
      printfield(hh.cc, 0, 70);
    }
  } else {
    fputs("\nTo:      ", stdout);
    printfield(hh.to, 0, 70);
    if (hh.cc[0] != '\0') {
      fputs("\nCC:      ", stdout);
      printfield(hh.cc, 0, 70);
    }
  }
  fputs("\nSubject: ", stdout);
  decode_subject(hh.subject);
  printfield(linebuf, 0, 70);
  fputs("\n\n", stdout);
  get_line(fp, 1, linebuf, LINEBUFSZ, &pos); // Reset buffer
  while (1) {
    if (!readp)
      readp = linebuf;
    if (!writep)
      writep = linebuf;
    if (get_line(fp, 0, writep, (LINEBUFSZ - (writep - linebuf)), &pos) == 0) {
      eof = 1;
      goto endscreen;
    }
    ++linecount;
    if ((mime >= 1) && is_mime_boundary(writep)) {
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
      if (!strncasecmp(writep, ct, 14)) {
        if (!strncmp(writep + 14, "text/plain", 10)) {
          mime = 3;
        } else if (!strncmp(writep + 14, "text/html", 9)) {
          printf("\n<Not showing HTML>\n");
          mime = 1;
        } else {
          mime_binary = 1;
          mime = 3;
        }
      } else if (!strncasecmp(writep, cte, 27)) {
        mime = 3;
        if (!strncmp(writep + 27, "7bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, "8bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, qp, 16))
          mime_enc = ENC_QP;
        else if (!strncmp(writep + 27, b64, 6))
          mime_enc = ENC_B64;
        else {
          printf(unsupp_enc, writep + 27);
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
            printf("\n*** Can't open %s\n", filename);
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
        chars = decode_base64(writep);
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
        pos = hh.skipbytes;
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
        mime_enc = ENC_7BIT;
        mime_binary = 0;
        pos = 0;
        fseek(fp, pos, SEEK_SET);
        get_line(fp, 1, linebuf, LINEBUFSZ, &pos); // Reset buffer
        do {
          get_line(fp, 0, linebuf, LINEBUFSZ, &pos);
          if (!strncasecmp(linebuf, ct, 14))
            mime = 4;
          if (!strncasecmp(linebuf, cte, 27)) {
            mime = 4;
            if (!strncmp(linebuf + 27, "7bit", 4))
              mime_enc = ENC_7BIT;
            else if (!strncmp(linebuf + 27, "8bit", 4))
              mime_enc = ENC_7BIT;
            else if (!strncmp(linebuf + 27, qp, 16))
              mime_enc = ENC_QP;
            else if (!strncmp(linebuf + 27, b64, 6))
              mime_enc = ENC_B64;
            else {
              mime = 0;
              break;
            }
          }
        } while (linebuf[0] != '\r');
        pos = hh.skipbytes;
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
  snprintf(filename, 80, email_db, cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "rb+");
  if (!fp)
    error(ERR_FATAL, cant_open, filename);
  if (fseek(fp, (uint32_t)(pos - 1) * EMAILHDRS_SZ_ON_DISK, SEEK_SET))
    error(ERR_FATAL, cant_seek, filename);
  l = fwrite(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
  if (l != EMAILHDRS_SZ_ON_DISK)
    error(ERR_FATAL, cant_write, filename);
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
  snprintf(filename, 80, email_db, cfg_emaildir, mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't create EMAIL.DB");
    return;
  }
  fclose(fp);
  snprintf(filename, 80, next_email, cfg_emaildir, mbox);
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
  uint8_t i = 0;
  // Treat '.' as shortcut for INBOX
  if (!strcmp(mbox, "."))
    strcpy(mbox, inbox);
  while(mbox[i]) {
    mbox[i] = toupper(mbox[i]);
    ++i;
  }
  strcpy(prev_mbox, curr_mbox);
  strcpy(curr_mbox, mbox);
  first_msg = 1;
  i = read_email_db(first_msg, 1, 1); // Errors non-fatal
  if (i) {
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
    error(ERR_FATAL, cant_malloc);
  snprintf(filename, 80, email_db, cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    free(h);
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }
  snprintf(filename, 80, email_db_new, cfg_emaildir, curr_mbox);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    free(h);
    fclose(fp);
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }
  while (1) {
    l = fread(h, 1, EMAILHDRS_SZ_ON_DISK, fp);
    ++count;
    if (l != EMAILHDRS_SZ_ON_DISK)
      goto done;
    if (h->status == 'D') {
      snprintf(filename, 80, email_file, cfg_emaildir, curr_mbox, h->emailnum);
      if (unlink(filename))
        error(ERR_NONFATAL, cant_delete, filename);
      goto_prompt_row();
      putchar(CLRLINE);
      printf("%u msgs deleted", ++delcount);
    } else {
      l = fwrite(h, 1, EMAILHDRS_SZ_ON_DISK, fp2);
      if (l != EMAILHDRS_SZ_ON_DISK) {
        error(ERR_NONFATAL, cant_write, filename);
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
  snprintf(filename, 80, email_db, cfg_emaildir, curr_mbox);
  if (unlink(filename)) {
    error(ERR_NONFATAL, cant_delete, filename);
    return;
  }
  snprintf(userentry, 80, email_db_new, cfg_emaildir, curr_mbox);
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
  snprintf(filename, 80, next_email, cfg_emaildir, mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, cant_open, filename);
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
  snprintf(filename, 80, next_email, cfg_emaildir, mbox);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, cant_open, filename);
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
      error(ERR_NONFATAL, "Bad address '%s'", p);
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
 * Write subject line to file
 * f - File descriptor to write to
 * s - Subject text
 * Adds 'Re: ' to subject line unless it is already there
 */
void subject_response(FILE *f, char *s) {
  decode_subject(s);
  fprintf(f, "Subject: %s%s\r", (strncmp(s, "Re: ", 4) ? "Re: " : ""), linebuf);
}

/*
 * Write subject line to file
 * f - File descriptor to write to
 * s - Subject text
 * Adds 'Fwd: ' to subject line unless it is already there
 */
void subject_forward(FILE *f, char *s) {
  decode_subject(s);
  fprintf(f, "Subject: %s%s\r", (strncmp(s, "Fwd: ", 5) ? "Fwd: " : ""), linebuf);
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
  if (mode == 'F')
    subject_forward(fp2, buf);
  else
    subject_response(fp2, buf);
  readdatetime(&dt);
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
  fprintf(fp2, a2_forever, "X-Mailer", PROGNAME);
  if (mode == 'R') {
    truncate_header(h->date, buf, 40);
    fprintf(fp2, "On %s, ", buf);
    truncate_header(h->from, buf, 80);
    fprintf(fp2, "%s wrote:\r\r", buf);
  } else {
    fprintf(fp2, "-------- Forwarded Message --------\r");
    decode_subject(h->subject);
    truncate_header(linebuf, buf, 80);
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
 * Write news article headers for follow-up postings
 * fp1  - File handle of the article being replied/forwarded
 * fp2  - File handle of the destination article
 * h    - headers of the article being replied/forwarded
 * Returns 0 if okay, 1 on error.
 */
uint8_t write_news_headers(FILE *fp1, FILE *fp2, struct emailhdrs *h, uint16_t num) {
  struct datetime dt;
  uint32_t pos;
  uint8_t refstate = 0, idstate = 0;
  fprintf(fp2, "From: %s\r", cfg_emailaddr);
  truncate_header(&(h->to[5]), buf, 80);
  fprintf(fp2, "Newsgroups: %s\r", buf);
  truncate_header(h->subject, buf, 80);
  subject_response(fp2, buf);
  readdatetime(&dt);
  datetimelong(&dt, userentry);
  fprintf(fp2, "Date: %s\r", userentry);
  fprintf(fp2, "Message-ID: <%05d-%s>\r", num, cfg_emailaddr);
  get_line(fp1, 1, linebuf, LINEBUFSZ, &pos); // Reset buffer
  while (1) {
    get_line(fp1, 0, linebuf, LINEBUFSZ, &pos);
    if (linebuf[0] == '\r') { // End of headers
      if ((idstate == 1) && (refstate == 0))
        fprintf(fp2, "References: %s", userentry);
      break;
    }
    if (strncmp(linebuf, "Message-ID:", 11) == 0) {
      if (refstate == 2)
        fprintf(fp2, &(linebuf[11]));
      else
        strncpy(userentry, &(linebuf[11]), 80);
      idstate = 1;
    } else if (strncmp(linebuf, "References:", 11) == 0) {
      if (refstate == 0) {
        fprintf(fp2, linebuf);
        refstate = 1;
      }
    } else if (linebuf[0] == ' ') {
      if (refstate == 1)
        fprintf(fp2, "%s", linebuf);
    } else {
      if (refstate == 1) {
        if (idstate == 1)
          fprintf(fp2, "%s", userentry);
        refstate = 2;
      }
    }
  }
  fprintf(fp2, a2_forever, "User-Agent", PROGNAME);
  truncate_header(h->from, buf, 80);
  fprintf(fp2, "%s wrote:\r\r", buf);
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
    c = cgetc_update_status();
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
 * mode - 'R' if reply, 'F' if forward, 'N' if news follow-up
 */
void get_email_body(struct emailhdrs *h, FILE *f, char mode) {
  uint16_t chars;
  char c, *readp, *writep;
  uint32_t pos = 0;
  const int8_t *b = b64dec - 43;
  uint8_t  mime = 0, mime_enc = ENC_7BIT, mime_binary = 0;
  fseek(fp, pos, SEEK_SET);
  get_line(fp, 1, linebuf, LINEBUFSZ, &pos); // Reset buffer
  do {
    spinner();
    get_line(fp, 0, linebuf, LINEBUFSZ, &pos);
    if (!strncasecmp(linebuf, mime_ver, 17))
      mime = 1;
    if (!strncasecmp(linebuf, ct, 14))
      mime = 4;
    if (!strncasecmp(linebuf, cte, 27)) {
      mime = 4;
      if (!strncmp(linebuf + 27, "7bit", 4))
        mime_enc = ENC_7BIT;
      else if (!strncmp(linebuf + 27, "8bit", 4))
        mime_enc = ENC_7BIT;
      else if (!strncmp(linebuf + 27, qp, 16))
        mime_enc = ENC_QP;
      else if (!strncmp(linebuf + 27, b64, 6))
        mime_enc = ENC_B64;
      else {
        error(ERR_NONFATAL, unsupp_enc, linebuf + 27);
        return;
      }
    }
  } while (linebuf[0] != '\r');
  pos = h->skipbytes;
  fseek(fp, pos, SEEK_SET);
  readp = linebuf;
  writep = linebuf;
  mime_binary = 0;
  get_line(fp, 1, linebuf, LINEBUFSZ, &pos); // Reset buffer
  while (1) {
    if (!readp)
      readp = linebuf;
    if (!writep)
      writep = linebuf;
    if (get_line(fp, 0, writep, (LINEBUFSZ - (writep - linebuf)), &pos) == 0)
      break;
    if ((mime >= 1) && is_mime_boundary(writep)) {
      if ((mime == 4) && !mime_binary) // End of Text/Plain MIME section
        break;
      mime = 2;
      mime_enc = ENC_7BIT;
      mime_binary = 0;
      readp = writep = NULL;
    } else if ((mime < 4) && (mime >= 2)) {
      if (!strncasecmp(writep, ct, 14)) {
        if (!strncmp(writep + 14, "text/plain", 10)) {
          mime = 3;
        } else if (!strncmp(writep + 14, "text/html", 9)) {
          printf("\n<Not showing HTML>\n");
          mime = 1;
        } else {
          mime_binary = 1;
          mime = 3;
        }
      } else if (!strncasecmp(writep, cte, 27)) {
        mime = 3;
        if (!strncmp(writep + 27, "7bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, "8bit", 4))
          mime_enc = ENC_7BIT;
        else if (!strncmp(writep + 27, qp, 16))
          mime_enc = ENC_QP;
        else if (!strncmp(writep + 27, b64, 6))
          mime_enc = ENC_B64;
        else {
          printf(unsupp_enc, writep + 27);
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
        chars = decode_base64(writep);
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
 * mode - 'R' for reply, 'F' for forward, 'N' for news follow-up, otherwise ' '
 */
void copy_to_mailbox(struct emailhdrs *h, uint16_t idx,
                     char *mbox, uint8_t delete, char mode) {
  uint16_t num, buflen, l, written;
  FILE *fp2;

  if (mode == 'N') {
    if (strncmp(h->to, "News:", 5) != 0) {
      error(ERR_NONFATAL, "Not a news article");
      return;
    }
  }

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
  snprintf(filename, 80, email_file, cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }

  // Open destination email file
  snprintf(filename, 80, email_file, cfg_emaildir, mbox, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    fclose(fp);
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }

  l = 0;
  if ((mode == 'R') || (mode == 'F')) {
    l = write_email_headers(fp, fp2, h, mode, userentry);
    if (l == 1)
      error(ERR_NONFATAL, "Invalid email header");
  } else if (mode == 'N') {
    l = write_news_headers(fp, fp2, h, num);
    if (l == 1)
      error(ERR_NONFATAL, "Invalid article header");
  }
  if (l > 0) {
    fclose(fp);
    fclose(fp2);
    return;
  }

  // Make sure spinner is in the right place
  if ((mode == 'R') || (mode == 'F') || (mode == 'N'))
    goto_prompt_row();

  // Copy email body
  putchar(' '); // For spinner
  if ((mode == 'R') || (mode == 'N'))
    fputc('>', fp2);
  if ((mode == 'R') || (mode == 'F') || (mode == 'N')) {
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
    snprintf(filename, 80, email_db, cfg_emaildir, mbox);
    _filetype = PRODOS_T_BIN;
    _auxtype = 0;
    fp = fopen(filename, "ab");
    if (!fp) {
      error(ERR_NONFATAL, cant_open, filename);
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
    snprintf(filename, 80, email_file, cfg_emaildir, mbox, num);
    load_editor(mode == 'N' ? 2 : 1);
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
    error(ERR_FATAL, cant_malloc);
  while (1) {
    snprintf(filename, 80, email_db, cfg_emaildir, curr_mbox);
    _filetype = PRODOS_T_BIN;
    _auxtype = 0;
    fp = fopen(filename, "rb+");
    if (!fp) {
      free(h);
      error(ERR_NONFATAL, cant_open, filename);
      return 1;
    }
    if (fseek(fp, count * EMAILHDRS_SZ_ON_DISK, SEEK_SET)) {
      error(ERR_NONFATAL, cant_seek, filename);
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
void create_blank_outgoing(void) {
  struct datetime dt;
  uint16_t num;
  if (get_next_email(outbox, &num))
    return;
  snprintf(filename, 80, email_file, cfg_emaildir, outbox, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, cant_open, filename);
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
  readdatetime(&dt);
  datetimelong(&dt, userentry);
  fprintf(fp, "Date: %s\r", userentry);
  if (prompt_for_name("cc", 0) == 255)
    goto done; // ESC pressed
  if (strlen(userentry) > 0)
    fprintf(fp, "cc: %s\r", userentry);
  fprintf(fp, a2_forever, "X-Mailer", PROGNAME);
  fclose(fp);
  if (update_next_email(outbox, num + 1))
    return;
  snprintf(filename, 80, email_file, cfg_emaildir, outbox, num);
  load_editor(1);
done:
  fclose(fp);
}

/*
 * Create a blank news article and put it in NEWS.OUTBOX
 * NEWS.OUTBOX is not a 'proper' mailbox (no EMAIL.DB)
 */
void create_blank_news(void) {
  struct datetime dt;
  uint16_t num;
  if (get_next_email(news_outbox, &num))
    return;
  snprintf(filename, 80, email_file, cfg_emaildir, news_outbox, num);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, cant_open, filename);
    return;
  }
  fprintf(fp, "From: %s\r", cfg_emailaddr);
  if (prompt_for_name("Newsgroup(s)", 0) == 255)
    goto done; // ESC pressed
  if (strlen(userentry) == 0)
    goto done;
  fprintf(fp, "Newsgroups: %s\r", userentry);
  if (prompt_for_name("Subject", 0) == 255)
    goto done; // ESC pressed
  fprintf(fp, "Subject: %s\r", userentry);
  readdatetime(&dt);
  datetimelong(&dt, userentry);
  fprintf(fp, "Date: %s\r", userentry);
  fprintf(fp, "Message-ID: <%05d-%s>\r", num, cfg_emailaddr);
  fprintf(fp, a2_forever, "User-Agent", PROGNAME);
  fclose(fp);
  if (update_next_email(news_outbox, num + 1))
    return;
  snprintf(filename, 80, email_file, cfg_emaildir, news_outbox, num);
  load_editor(2);
done:
  fclose(fp);
}

/*
 * Display help screen
 */
void help(uint8_t num) {
  char *p;
  char c;
  uint16_t i, s;
  uint8_t cont;
  revers(0);
  cursor(0);
  clrscr2();
  snprintf(filename, 80, "%s/EMAILHELP%u.TXT", cfg_instdir, num);
  fp = fopen(filename, "rb");
  if (!fp) {
    printf("Can't open help file\n\n");
    goto done;
  }
  p = buf;
  do {
    s = fread(p, 1, READSZ, fp);
    cont = (s == READSZ ? 1 : 0);
    for (i = 0; i < s; ++i) {
      c = p[i];
      if (c == '{')
        printf("%s", openapple);
      else if (c == '}')
        printf("%s", closedapple);
      else if ((c != '\r') && (c != '\n'))
        putchar(c);
    }
  } while (cont);
done:
  fclose(fp);
}

/*
 * Move to the next message, if there is one.
 * Called by the code that handles down arrow.
 * Returns 1 if there are more messages below, 0 otherwise
 */
uint8_t go_to_next_message(void) {
  if (selection < num_msgs) {
    prevselection = selection;
    ++selection;
    update_highlighted();
    return 1;
  } else if (first_msg + selection - 1 < total_msgs) {
    first_msg += MSGS_PER_PAGE;
    read_email_db(first_msg, 0, 0);
    selection = 1;
    email_summary();
    return 1;
  } else
    return 0;
}

/*
 * Keyboard handler
 */
void keyboard_hdlr(void) {
  struct emailhdrs *h;
  char c;
  while (1) {
    h = get_headers(selection);
    c = cgetc_update_status();
    if (*(uint8_t*)0xc062 & 0x80) { // Closed Apple depressed
      switch (c) {
      case 'r':    // CA-R "Retrieve news via NNTP"
      case 'R':
        load_nntp65();
        break;
      case 's':    // CA-S "Sent news via NNTP"
      case 'S':
        load_nntp65up();
        break;
      case 'p':    // CA-P "Post news article"
      case 'P':
        create_blank_news();
        break;
      case 'f':    // CA-F "Follow-up news article"
      case 'F':
        if (h)
          copy_to_mailbox(h, get_db_index(), news_outbox, 0, 'N');
        break;
      }
      continue;
    }
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
    case 'j':
    case 'J':
    case DOWNARROW:
      go_to_next_message();
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
        go_to_next_message();
      }
      break;
    case 'd':
    case 'D':
      if (h) {
        h->status = 'D';
        write_updated_headers(h, get_db_index());
        email_summary_for(selection);
        go_to_next_message();
      }
      break;
    case 'u':
    case 'U':
      if (h) {
        h->status = 'R';
        write_updated_headers(h, get_db_index());
        email_summary_for(selection);
        go_to_next_message();
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
        copy_to_mailbox(h, get_db_index(), outbox, 0, 'R');
      break;
    case 'f':
    case 'F':
      if (h)
        copy_to_mailbox(h, get_db_index(), outbox, 0, 'F');
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
      switch_mailbox(curr_mbox);
      break;
    case '.':
    case '>':
      reverse = 0;
      switch_mailbox(curr_mbox);
      break;
    case 0x80 + 'd': // OA-D "Update date using NTP"
    case 0x80 + 'D':
      load_date65();
      break;
    case 0x80 + 'e': // OA-E "Open message in editor"
    case 0x80 + 'E':
      snprintf(filename, 80, email_file, cfg_emaildir, curr_mbox, h->emailnum);
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
      help(1);
      c = cgetc();
      email_summary();
      break;
    case 'q':
    case 'Q':
      if (prompt_okay("Quit - ")) {
        save_prefs();
        clrscr2();
        exit(0);
      }
    default:
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
  //printf("heapmemavail=%d heapmaxavail=%d\n", _heapmemavail(), _heapmaxavail());
  readconfigfile();
  load_prefs();
  read_email_db(first_msg, 1, 0);
  email_summary();
  keyboard_hdlr();
}

