/////////////////////////////////////////////////////////////////
// Simple Email User Agent
// Handles INBOX in the format created by POP65
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// TODO:
// - Purging deleted emails
// - Tagging of emails (and move and copy based on tags)
// - Email composition (new message, reply and forward)
// - Better error handling (maybe just clear screen before fatal error?)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <conio.h>
#include <string.h>
#include <ctype.h>

#define EMAIL_C
#include "email_common.h"

#define MSGS_PER_PAGE 18     // Number of messages shown on summary screen
#define MENU_ROW      22     // Row that the menu appears on
#define PROMPT_ROW    24     // Row that data entry prompt appears on
#define SCROLLBACK    25*80  // How many bytes to go back when paging up
#define READSZ        1024   // Size of buffer for copying files


char                  filename[80];
char                  userentry[80];
FILE                  *fp;
struct emailhdrs      *headers;
uint16_t              selection, prevselection;
uint16_t              num_msgs;    // Num of msgs shown in current page
uint16_t              total_msgs;  // Total number of message in mailbox
uint16_t              total_new;   // Total number of new messages
uint16_t              first_msg;   // Msg numr: first message current page
char                  curr_mbox[80] = "INBOX";
static unsigned char  buf[READSZ];

#define ERR_NONFATAL 0
#define ERR_FATAL    1

/*
 * Show non fatal error in PROMPT_ROW
 * Fatal errors are shown on a blank screen
 */
void error(uint8_t fatal, const char *fmt, ...) {
  va_list v;
  uint8_t i;
  if (fatal) {
    clrscr();
    printf("\n\n%cFATAL ERROR:%c\n\n", 0x0f, 0x0e);
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf("\n\n\n\n[Press Any Key To Quit]");
    cgetc();
    exit(1);
  } else {
    putchar(0x19);                          // HOME
    for (i = 0; i < PROMPT_ROW - 1; ++i) 
      putchar(0x0a);                        // CURSOR DOWN
    putchar(0x1a);                          // CLEAR LINE
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf(" - [Press Any Key]");
    cgetc();
    putchar(0x1a);                          // CLEAR LINE
  }
}

/*
 * Busy spinner
 */
void spinner(void) {
  static char chars[] = "|/-\\";
  static uint8_t i = 0;
  putchar(0x08); // BACKSPACE
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
  fscanf(fp, "%s", cfg_emaildir);
  fclose(fp);
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
 * change - if 1, then errors are treated as non-fatal (for V)iew command)
 * Returns 0 if okay, 1 on non-fatal error.
 */
uint8_t read_email_db(uint16_t startnum, uint8_t initialize, uint8_t change) {
  struct emailhdrs *curr = NULL, *prev = NULL;
  uint16_t count = 0;
  uint16_t l;
  if (initialize) {
    total_new = total_msgs = 0;
  }
  free_headers_list();
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(change ? ERR_NONFATAL : ERR_FATAL, "Can't open %s", filename);
    if (change)
      return 1;
  }
  if (fseek(fp, (startnum - 1) * (sizeof(struct emailhdrs) - 2), SEEK_SET)) {
    error(change ? ERR_NONFATAL : ERR_FATAL, "Can't seek in %s", filename);
    if (change)
      return 1;
  }
  num_msgs = 0;
  while (1) {
    curr = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
    curr->next = NULL;
    l = fread(curr, 1, sizeof(struct emailhdrs) - 2, fp);
    ++count;
    if (l != sizeof(struct emailhdrs) - 2) {
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
  putchar(inverse ? 0xf : 0xe); // INVERSE or NORMAL
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
  printf("%02d|", h->emailnum);
  printfield(h->date, 0, 16);
  putchar('|');
  printfield(h->from, 0, 20);
  putchar('|');
  printfield(h->subject, 0, 38);
  //putchar('\r');
  putchar(0xe); // NORMAL
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
 * Show email summary
 */
void email_summary(void) {
  uint8_t i = 1;
  struct emailhdrs *h = headers;
  clrscr();
  if (num_msgs == 0)
    printf("%c[%s] No messages%c", 0x0f, curr_mbox, 0x0e);
  else
    printf("%c[%s] %u messages, %u new. Displaying %u-%u%c",
           0x0f, curr_mbox, total_msgs, total_new, first_msg,
           first_msg + num_msgs - 1, 0x0e);
  printf("\n\n");
  while (h) {
    print_one_email_summary(h, (i == selection));
    ++i;
    h = h->next;
  }
  putchar(0x19);                          // HOME
  for (i = 0; i < MENU_ROW - 1; ++i) 
    putchar(0x0a);                        // CURSOR DOWN
  printf("%cUp/K Prev  | Down/J Next | SPC/CR Read | D)el  | U)ndel | P)urge%c\n", 0x0f, 0x0e);
  printf("%cV)iew mbox | N)ew mbox   | A)rchive    | C)opy | M)ove  | Q)uit %c", 0x0f, 0x0e);
}

/*
 * Show email summary for nth email message in list of headers
 */
void email_summary_for(uint16_t n) {
  struct emailhdrs *h = headers;
  uint16_t j;
  h = get_headers(n);
  putchar(0x19);                          // HOME
  for (j = 0; j < n + 1; ++j) 
    putchar(0x0a);                        // CURSOR DOWN
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
 * Display email with simple pager functionality
 */
void email_pager(void) {
  uint32_t pos = 0;
  struct emailhdrs *h = get_headers(selection);
  uint8_t line, eof;
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
  clrscr();
  line = 6;
  fputs("Date:    ", stdout);
  printfield(h->date, 0, 39);
  fputs("\nFrom:    ", stdout);
  printfield(h->from, 0, 70);
  fputs("\nTo:      ", stdout);
  printfield(h->to, 0, 70);
  if (h->cc[0] != '\0') {
    fputs("\nCC:      ", stdout);
    printfield(h->cc, 0, 70);
    ++line;
  }
  fputs("\nSubject: ", stdout);
  printfield(h->subject, 0, 70);
  fputs("\n\n", stdout);
  while (1) {
    c = fgetc(fp);
    eof = feof(fp);
    if (!eof) {
      putchar(c);
      ++pos;
    }
    if (c == '\r') {
      ++line;
      if (line == 22) {
        putchar(0x0f); // INVERSE
        printf("[%05lu] SPACE continue reading | B)ack | T)op | H)drs | Q)uit", pos);
        putchar(0x0e); // NORMAL
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
          pos = h->skipbytes;
          fseek(fp, pos, SEEK_SET);
          goto restart;
          break;
        case 'H':
        case 'h':
          pos = 0;
          fseek(fp, pos, SEEK_SET);
          goto restart;
        break;
        case 'Q':
        case 'q':
          fclose(fp);
          return;
        default:
          putchar(7); // BELL
          goto retry1;
        }
        clrscr();
        line = 0;
      }
    } else if (eof) {
      putchar(0x0f); // INVERSE
      printf("[%05lu]      *** END ***       | B)ack | T)op | H)drs | Q)uit", pos);
      putchar(0x0e); // NORMAL
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
        pos = h->skipbytes;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'H':
      case 'h':
        pos = 0;
        fseek(fp, pos, SEEK_SET);
        goto restart;
        break;
      case 'Q':
      case 'q':
        fclose(fp);
        return;
      default:
        putchar(7); // BELL
        goto retry2;
      }
      clrscr();
      line = 0;
    }
  }
}

/*
 * Write updated email headers to EMAIL.DB
 */
void write_updated_headers(struct emailhdrs *h, uint16_t pos) {
  uint16_t l;
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, curr_mbox);
  fp = fopen(filename, "rb+");
  if (!fp)
    error(ERR_FATAL, "Can't open %s", filename);
  if (fseek(fp, (pos - 1) * (sizeof(struct emailhdrs) - 2), SEEK_SET))
    error(ERR_FATAL, "Can't seek in %s", filename);
  l = fwrite(h, 1, sizeof(struct emailhdrs) - 2, fp);
  if (l != sizeof(struct emailhdrs) - 2)
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
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't create EMAIL.DB");
    return;
  }
  fclose(fp);
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
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
void change_mailbox(char *mbox) {
  char prev_mbox[80];
  uint8_t err;
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

  // TODO
}

/*
 * Copies the current message to mailbox mbox.  If delete is 1 then
 * it will be marked as deleted in the source mbox
 */
void copy_to_mailbox(char *mbox, uint8_t delete) {
  struct emailhdrs *h = get_headers(selection);
  uint16_t num, buflen, written;
  FILE *fp2;

  // Read next number from dest/NEXT.EMAIL
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s/NEXT.EMAIL for read", mbox);
    return;
  }
  fscanf(fp, "%u", &num);
  fclose(fp);

  // Open source email file
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, curr_mbox, h->emailnum);
  fp = fopen(filename, "rb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  // Open destination email file
  sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, mbox, num);
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }
 
  // Copy email
  putchar(' '); // For spinner
  while (1) {
    buflen = fread(buf, 1, READSZ, fp);
    spinner();
    if (buflen == 0)
      break;
    written = fwrite(buf, 1, buflen, fp2);
    if (written != buflen) {
      error(ERR_NONFATAL, "Write error");
      fclose(fp);
      fclose(fp2);
      return;
    }
  }
  putchar(0x08); // Erase spinner
  putchar(' ');
  putchar(0x08);

  fclose(fp);
  fclose(fp2);

  // Update dest/EMAIL.DB
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
  fp = fopen(filename, "ab");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s/EMAIL.DB for write", mbox);
    return;
  }
  buflen = h->emailnum; // Just reusing buflen as a temporary
  h->emailnum = num;
  fwrite(h, sizeof(struct emailhdrs) - 2, 1, fp);
  h->emailnum = buflen;
  fclose(fp);

  // Update dest/NEXT.EMAIL, incrementing count by 1
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s/NEXT.EMAIL for write", mbox);
    return;
  }
  fprintf(fp, "%u", num + 1);
  fclose(fp);

  if (delete) {
    h->status = 'D';
    write_updated_headers(h, first_msg + selection - 1);
    email_summary_for(selection);
  }
}

/*
 * Prompt ok?
 */
char prompt_okay(void) {
  uint16_t i;
  char c;
  putchar(0x19);                          // HOME
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(0x0a);                        // CURSOR DOWN
  printf("Sure? (y/n)");
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N'))
      break;
    putchar(7); // BELL
  } 
  if ((c == 'y') || (c == 'Y'))
    c = 1;
  else
    c = 0;
  putchar(0x1a);                          // CLEAR LINE
  return c;
}

/*
 * Prompt for a name in the line below the menu, store it in userentry
 * Returns number of chars read.
 */
uint8_t prompt_for_name(void) {
  uint16_t i;
  char c;
  putchar(0x19);                          // HOME
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(0x0a);                        // CURSOR DOWN
  printf(">>>");
  i = 0;
  while (1) {
    c = cgetc();
    if (!isalnum(c) && (c != 0x0d) && (c != 0x08) && (c != 0x7f) && (c != '.')) {
      putchar(7); // BELL
      continue;
    }
    switch (c) {
    case 0x0d:                            // RETURN KEY
      goto done;
    case 0x08:                            // BACKSPACE
    case 0x7f:                            // DELETE
      if (i > 0) {
        putchar(0x08);
        putchar(' ');
        putchar(0x08);
        --i;
      } else
        putchar(7); // BELL
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
  putchar(0x1a);                          // CLEAR LINE
  putchar(0x19);                          // HOME
  for (c = 0; c < PROMPT_ROW - 1; ++c) 
    putchar(0x0a);                        // CURSOR DOWN
  return i;
}

/*
 * Keyboard handler
 */
void keyboard_hdlr(void) {
  struct emailhdrs *h;
  uint8_t i;
  while (1) {
    char c = cgetc();
    switch (c) {
    case 'k':
    case 'K':
    case 0xb: // UP-ARROW
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
    case 0xa: // DOWN-ARROW
      if (selection < num_msgs) {
        prevselection = selection;
        ++selection;
        update_highlighted();
      } else if (first_msg + selection + 1 < total_msgs) {
        first_msg += MSGS_PER_PAGE;
        read_email_db(first_msg, 0, 0);
        selection = 1;
        email_summary();
      }
      break;
    case 0x0d: // RETURN KEY
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
      if (prompt_for_name())
        copy_to_mailbox(userentry, 0);
      break;
    case 'm':
    case 'M':
      if (prompt_for_name())
        copy_to_mailbox(userentry, 1);
      break;
    case 'a':
    case 'A':
      putchar(0x19);                          // HOME
      for (i = 0; i < PROMPT_ROW - 1; ++i) 
        putchar(0x0a);                        // CURSOR DOWN
      copy_to_mailbox("RECEIVED", 1);
      break;
    case 'p':
    case 'P':
      purge_deleted();
      break;
    case 'n':
    case 'N':
      if (prompt_for_name())
        new_mailbox(userentry);
      break;
    case 'v':
    case 'V':
      if (prompt_for_name())
        change_mailbox(userentry);
      break;
    case 'q':
    case 'Q':
      if (prompt_okay()) {
        clrscr();
        exit(0);
      }
    default:
      //printf("[%02x]", c);
      putchar(7); // BELL
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

