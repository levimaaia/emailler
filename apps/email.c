/////////////////////////////////////////////////////////////////
// Simple Email User Agent
// Handles INBOX in the format created by POP65
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// TODO:
// - Purging deleted emails
// - Switching folders
// - Moving & copying emails between folders
// - Email composition (new message, reply and forward)
// - Better error handling (maybe just clear screen before fatal error?)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <conio.h>
#include <string.h>

#define EMAIL_C
#include "email_common.h"

#define MSGS_PER_PAGE 16  // Number of messages shown on summary screen
#define MENU_ROW      22  // Row that the menu appears on
#define PROMPT_ROW    24  // Row that data entry prompt appears on
#define SCROLLBACK 25*80  // How many bytes to go back when paging up

char filename[80];
FILE *fp;
struct emailhdrs *headers;
uint16_t selection, prevselection;
uint16_t num_msgs;    // Number of messages shown in current page
uint16_t total_msgs;  // Total number of message in mailbox
uint16_t total_new;   // Total number of new messages
uint16_t first_msg;   // Message number of first message on current page
char curr_mbox[80] = "INBOX";

/*
 * Keypress before quit
 */
void confirm_exit(void) {
  printf("\nPress any key ");
  cgetc();
  exit(0);
}

/*
 * Called for all non IP65 errors
 */
void error_exit() {
  confirm_exit();
}

/*
 * Read parms from POP65.CFG
 */
void readconfigfile(void) {
    fp = fopen("POP65.CFG", "r");
    if (!fp) {
      puts("Can't open config file POP65.CFG");
      error_exit();
    }
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
 * initialize - if set then total_new and total_msgs are calculated
 */
void read_email_db(uint16_t startnum, uint8_t initialize) {
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
    printf("Can't open %s\n", filename);
    error_exit();
  }
  if (fseek(fp, (startnum - 1) * (sizeof(struct emailhdrs) - 2), SEEK_SET)) {
    printf("Can't seek in %s\n", filename);
    error_exit();
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
      return;
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
        return;
      }
    if (initialize) {
      ++total_msgs;
      if (curr->status == 'N')
        ++total_new;
    }
  }
  fclose(fp);
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
  printf("%cUp/K Prev  | Down/J Next  | SPC/CR Read | D)elete | U)ndel | P)urge%c\n", 0x0f, 0x0e);
  printf("%cC)hng mbox | N)ew mbox    | C)opy       | M)ove   | Q)uit          %c", 0x0f, 0x0e);
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
    printf("Can't open %s\n", filename);
    printf("[Press any key]");
    cgetc();
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
  if (!fp) {
    printf("Can't open %s\n", filename);
    error_exit();
  }
  if (fseek(fp, (pos - 1) * (sizeof(struct emailhdrs) - 2), SEEK_SET)) {
    printf("Can't seek in %s\n", filename);
    error_exit();
  }
  l = fwrite(h, 1, sizeof(struct emailhdrs) - 2, fp);
  if (l != sizeof(struct emailhdrs) - 2) {
    printf("Can't write to %s\n", filename);
    error_exit();
  }
  fclose(fp);
}

/*
 * Create new mailbox
 * Create directory, EMAIL.DB and NEXT.EMAIL files
 */
void new_mailbox(char *mbox) {
  sprintf(filename, "%s/%s", cfg_emaildir, mbox);
  if (mkdir(filename)) {
    printf("Can't create dir %s\n", filename);
    return;
  }
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
  fp = fopen(filename, "wb");
  if (!fp) {
    printf("Can't create EMAIL.DB\n");
    return;
  }
  fclose(fp);
  sprintf(filename, "%s/%s/NEXT.EMAIL", cfg_emaildir, mbox);
  fp = fopen(filename, "wb");
  if (!fp) {
    printf("Can't create NEXT.EMAIL\n");
    return;
  }
  fprintf(fp, "1");
  fclose(fp);
}

/*
 * Change current mailbox
 */
void change_mailbox(char *mbox) {
  strcpy(curr_mbox, mbox);
  first_msg = 1;
  read_email_db(first_msg, 1);
  selection = 1;
  email_summary();
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
    case 0xb: // UP-ARROW
      if (selection > 1) {
        prevselection = selection;
        --selection;
        update_highlighted();
      } else if (first_msg > MSGS_PER_PAGE) {
        first_msg -= MSGS_PER_PAGE;
        read_email_db(first_msg, 0);
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
        read_email_db(first_msg, 0);
        selection = 1;
        email_summary();
      }
      break;
    case 0x0d: // RETURN
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
    case 'p':
    case 'P':
      // TODO: Purge deleted messages
      break;
    case 'n':
    case 'N':
      // TODO: Prompt for mailbox name
      new_mailbox("RECEIVED");
      break;
    case 'c':
    case 'C':
      // TODO: Prompt for mailbox name
      if (!strcmp(curr_mbox, "INBOX"))
        change_mailbox("RECEIVED");
      else
        change_mailbox("INBOX");
      break;
    case 'q':
    case 'Q':
      clrscr();
      exit(0);
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
  read_email_db(first_msg, 1);
  selection = 1;
  email_summary();
  keyboard_hdlr();
}

