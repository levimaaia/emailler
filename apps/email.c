/////////////////////////////////////////////////////////////////
// emai//er - Simple Email User Agent vaguely inspired by Elm
// Handles INBOX in the format created by POP65
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// TODO:
// - Fix terrible scrollback algorithm!!
// - Email composition (write, reply and forward)

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
uint16_t              total_tag;   // Total number of tagged messages
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
  fscanf(fp, "%s", cfg_emailaddr);
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
  putchar(inverse ? 0xf : 0xe); // INVERSE or NORMAL
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

void status_bar(void) {
  putchar(0x19);                          // HOME
  if (num_msgs == 0)
    printf("%cemai//er v0.1 [%s] No messages%c", 0x0f, curr_mbox, 0x0e);
  else
    printf("%cemai//er v0.1 [%s] %u messages, %u new, %u tagged. Displaying %u-%u%c",
           0x0f, curr_mbox, total_msgs, total_new, total_tag, first_msg,
           first_msg + num_msgs - 1, 0x0e);
  printf("\n\n");
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
  putchar(0x19);                          // HOME
  for (i = 0; i < MENU_ROW - 1; ++i) 
    putchar(0x0a);                        // CURSOR DOWN
  printf("%cUp/K Prev | SPC/RET Read | A)rchive | C)opy | M)ove  | D)el   | U)ndel | P)urge %c", 0x0f, 0x0e);
  printf("%cDn/J Next | S)witch mbox | N)ew mbox| T)ag  | W)rite | R)eply | F)wd   | Q)uit  %c", 0x0f, 0x0e);
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
  uint8_t *p = (uint8_t*)0x25; // CURSOR ROW!!
  struct emailhdrs *h = get_headers(selection);
  uint8_t eof;
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
  while (1) {
    c = fgetc(fp);
    eof = feof(fp);
    if (!eof) {
      putchar(c);
      ++pos;
    }
    if (c == '\r') {
      if ((*p) == 22) { // Use the CURSOR ROW location
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
void switch_mailbox(char *mbox) {
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
      putchar(0x19);                          // HOME
      for (l = 0; l < PROMPT_ROW - 1; ++l) 
        putchar(0x0a);                        // CURSOR DOWN
      putchar(0x1a);                          // CLEAR LINE
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
 * Copies the current message to mailbox mbox.
 * h is a pointer to the emailheaders for the message to copy
 * idx is the index of the message in EMAIL.DB in the source mailbox (1-based)
 * mbox is the name of the destination mailbox
 * delete - if set to 1 then the message will be marked as deleted in the source mbox
 */
void copy_to_mailbox(struct emailhdrs *h, uint16_t idx, char *mbox, uint8_t delete) {
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
  fp2 = fopen(filename, "wb");
  if (!fp2) {
    fclose(fp);
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

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email(mbox, num + 1))
    return;

  if (delete)
    h->status = 'D';
  h->tag = ' '; // Untag files after copy or move
  write_updated_headers(h, idx);
  email_summary_for(selection);
}

/*
 * Prompt ok?
 */
char prompt_okay(char *msg) {
  uint16_t i;
  char c;
  putchar(0x19);                          // HOME
  for (i = 0; i < PROMPT_ROW - 1; ++i) 
    putchar(0x0a);                        // CURSOR DOWN
  printf("%sSure? (y/n)", msg);
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
    copy_to_mailbox(h, first_msg + selection - 1, mbox, delete);
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
      putchar(0x19);                          // HOME
      for (l = 0; l < PROMPT_ROW - 1; ++l) 
        putchar(0x0a);                        // CURSOR DOWN
      putchar(0x1a);                          // CLEAR LINE
      printf("%u/%u:", ++tagcount, total_tag);
      copy_to_mailbox(h, count, mbox, delete);
    }
  }
err:
  free(h);
  fclose(fp);
  return 1;
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
 * Create a blank outgoing message and put it in OUTBOX.
 * OUTBOX is not a 'proper' mailbox (no EMAIL.DB)
 */
void create_blank_outgoing() {
  uint16_t num;

  // Read next number from dest/NEXT.EMAIL
  if (get_next_email("OUTGOING", &num))
    return;

  // Open destination email file
  sprintf(filename, "%s/OUTGOING/EMAIL.%u", cfg_emaildir, num);
  fp = fopen(filename, "wb");
  if (!fp) {
    error(ERR_NONFATAL, "Can't open %s", filename);
    return;
  }

  fprintf(fp, "From: %s\n", cfg_emailaddr);
  fprintf(fp, "Subject: \n");
  fprintf(fp, "Date: TODO: put date in here!!\n");
  fprintf(fp, "To: \n");
  fprintf(fp, "cc: \n\n");
  fclose(fp);

  // Update dest/NEXT.EMAIL, incrementing count by 1
  if (update_next_email("OUTGOING", num + 1))
    return;

  // Not really an error but useful to have an alert
  sprintf(filename, "Created file %s/OUTGOING/EMAIL.%u", cfg_emaildir, num);
  error(ERR_NONFATAL, filename);
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
        copy_to_mailbox_tagged(userentry, 0);
      break;
    case 'm':
    case 'M':
      if (prompt_for_name())
        copy_to_mailbox_tagged(userentry, 1);
      break;
    case 'a':
    case 'A':
      putchar(0x19);                          // HOME
      for (i = 0; i < PROMPT_ROW - 1; ++i) 
        putchar(0x0a);                        // CURSOR DOWN
      copy_to_mailbox_tagged("RECEIVED", 1);
      break;
    case 'p':
    case 'P':
      if (prompt_okay("Purge - "))
        purge_deleted();
      break;
    case 'n':
    case 'N':
      if (prompt_for_name())
        new_mailbox(userentry);
      break;
    case 's':
    case 'S':
      if (prompt_for_name())
        switch_mailbox(userentry);
      break;
    case 'w':
    case 'W':
      create_blank_outgoing();
      break;
    case 'r':
    case 'R':
      // TODO
      break;
    case 'f':
    case 'F':
      // TODO
      break;
    case 'q':
    case 'Q':
      if (prompt_okay("Quit - ")) {
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

