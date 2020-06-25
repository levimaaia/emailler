//
// Simple Email User Agent
// Bobbi June 2020
// Handles INBOX in the format created by POP65
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <conio.h>
#include <string.h>

#define SCROLLBACK 25*80

char filename[80];
FILE *fp;
struct emailhdrs *headers;
uint16_t selection, prevselection;
uint16_t num_msgs;

// Configuration params from POP65.CFG
char cfg_server[80];         // IP of POP3 server
char cfg_user[80];           // Username
char cfg_pass[80];           // Password
char cfg_spooldir[80];       // ProDOS directory to spool email to
char cfg_inboxdir[80];       // ProDOS directory for email inbox

// Represents the email headers for one message
struct emailhdrs {
  char date[80];
  char from[80];
  char to[80];
  char cc[80];
  char subject[80];
  struct emailhdrs *next;
};

/*
 * Keypress before quit
 */
void confirm_exit(void)
{
  printf("\nPress any key ");
  cgetc();
  exit(0);
}

/*
 * Called for all non IP65 errors
 */
void error_exit()
{
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
    fscanf(fp, "%s", cfg_spooldir);
    fscanf(fp, "%s", cfg_inboxdir);
    fclose(fp);
}

/*
 * Read EMAIL.DB
 */
void read_email_db(void) {
  struct emailhdrs *curr = NULL, *prev = NULL;
  uint16_t l;
  headers = NULL;
  sprintf(filename, "%s/EMAIL.DB", cfg_inboxdir);
  fp = fopen(filename, "rb");
  if (!fp) {
    printf("Can't open %s\n", filename);
    error_exit();
  }
  num_msgs = 0;
  while (1) {
    curr = (struct emailhdrs*)malloc(sizeof(struct emailhdrs));
    curr->next = NULL;
    l = fread(curr, 1, sizeof(struct emailhdrs) - 2, fp);
    if (l != sizeof(struct emailhdrs) - 2) {
      free(curr);
      fclose(fp);
      return;
    }
    if (!prev)
      headers = curr;
    else
      prev->next = curr;
    prev = curr;
    ++num_msgs;
  }
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
void print_one_email_summary(uint16_t num, struct emailhdrs *h, uint8_t inverse) {
  putchar(inverse ? 0xf : 0xe); // INVERSE or NORMAL
  printf("%02d|", num);
  printfield(h->date, 0, 16);
  putchar('|');
  printfield(h->from, 0, 20);
  putchar('|');
  printfield(h->subject, 0, 39);
  //putchar('\r');
  putchar(0xe); // NORMAL
}

/*
 * Get emailhdrs for nth email
 */
struct emailhdrs *get_headers(uint16_t n) {
  uint16_t i = 1;
  struct emailhdrs *h = headers;
  while (i < n) {
    ++i;
    h = h->next;
  }
  return h;
}

/*
 * Show email summary
 */
void email_summary(void) {
  uint16_t i = 1;
  struct emailhdrs *h = headers;
  clrscr();
  while (h) {
    print_one_email_summary(i, h, (i == selection));
    ++i;
    h = h->next;
  }
}

/*
 * Show email summary for nth email message
 */
void email_summary_for(uint16_t n) {
  struct emailhdrs *h = headers;
  uint16_t j;
  h = get_headers(n);
  putchar(0x19);   // HOME
  for (j = 0; j < n - 1; ++j)
    putchar(0x0a); // CURSOR DOWN
  print_one_email_summary(n, h, (n == selection));
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
  sprintf(filename, "%s/EMAIL.%u", cfg_inboxdir, selection);
  fp = fopen(filename, "rb");
  if (!fp) {
    printf("Can't open %s\n", filename);
    printf("[Press any key]");
    cgetc();
    return;
  }
restart:
  clrscr();
  line = 6;
  fputs("Date:    ", stdout);
  printfield(h->date, 0, 70);
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
        printf("[%05lu] SPACE continue reading | B)ack | T)op | Q)uit", pos);
        putchar(0x0e); // NORMAL
retry1:
        c = cgetc();
        switch (c) {
        case ' ':
          break;
        case 'B':
        case 'b':
          if (pos < (uint32_t)(SCROLLBACK)) {
            pos = 0;
            fseek(fp, pos, SEEK_SET);
            goto restart;
          } else {
            pos -= (uint32_t)(SCROLLBACK);
            fseek(fp, pos, SEEK_SET);
          }
          break;
        case 'T':
        case 't':
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
      printf("[%05lu]      *** END ***       | B)ack | T)op | Q)uit", pos);
      putchar(0x0e); // NORMAL
retry2:
      c = cgetc();
      switch (c) {
      case 'B':
      case 'b':
        if (pos < (uint32_t)(SCROLLBACK)) {
          pos = 0;
          fseek(fp, pos, SEEK_SET);
          goto restart;
        } else {
          pos -= (uint32_t)(SCROLLBACK);
          fseek(fp, pos, SEEK_SET);
        }
        break;
      case 'T':
      case 't':
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
 * Keyboard handler
 */
void keyboard_hdlr(void) {
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
      }
      break;
    case 'j':
    case 'J':
    case 0xa: // DOWN-ARROW
      if (selection < num_msgs) {
        prevselection = selection;
        ++selection;
        update_highlighted();
      }
      break;
    case 0x0d: // RETURN
    case ' ':
      email_pager();
      email_summary();
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
  read_email_db();
  selection = 1;
  email_summary();
  keyboard_hdlr();
}

