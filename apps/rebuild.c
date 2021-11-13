/////////////////////////////////////////////////////////////////
// Rebuild EMAIL.DB & NEXT.EMAIL files for an existing mailbox
// Bobbi July 2020
/////////////////////////////////////////////////////////////////

#include <cc65.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <apple2_filetype.h>
#include "email_common.h"

#define NETBUFSZ  1500+4       // 4 extra bytes for overlap between packets
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf[LINEBUFSZ];

static char dirname[255];
static char filename[255];

/*
 * Keypress before quit
 */
void confirm_exit(void) {
  printf("\n[Press Any Key]");
  cgetc();
  exit(0);
}

/*
 * Called for all errors
 */
void error_exit() {
  confirm_exit();
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
 * Update EMAIL.DB - quick access database for header info
 */
void update_email_db(struct emailhdrs *h) {
  FILE *fp;
  sprintf(filename, "%s/EMAIL.DB", dirname);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "ab");
  if (!fp) {
    printf("Can't open %s\n", filename);
    error_exit();
  }
  fwrite(h, sizeof(struct emailhdrs), 1, fp);
  fclose(fp);
}

/*
 * Write NEXT.EMAIL file with number of next EMAIL.n file to be created
 */
void write_next_email(uint16_t num) {
  FILE *fp;
  sprintf(filename, "%s/NEXT.EMAIL", dirname);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    printf("2)Can't open %s\n", filename);
    fclose(fp);
    error_exit();
  }
  fprintf(fp, "%u", num);
  fclose(fp);
}

/*
 * Copy one header from source->dest, removing '\r' from end
 */
void copyheader(char *dest, char *source, uint16_t len) {
  uint16_t i;
  memset(dest, ' ', len);
  for (i = 0; i < len; ++i) {
    if ((*source == '\0') || (*source == '\r'))
      return;
    *dest++ = *source++;
  }
}

/*
 * Repair a mailbox by scanning the messages and rebuilding
 * EMAIL.DB and NEXT.EMAIL
 */
void repair_mailbox(void) {
  static struct emailhdrs hdrs;
  uint16_t chars, headerchars, emailnum, minemailnum, maxemailnum;
  uint8_t headers;
  FILE *fp;
  DIR *dp;
  struct dirent *d;

  dp = opendir(dirname);
  if (!dp) {
    printf("Can't open dir %s\n", dirname);
    error_exit();
  }

  sprintf(filename, "%s/EMAIL.DB", dirname);
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    closedir(dp);
    printf("Can't create %s\n", filename);
    error_exit();
  }
  fclose(fp);

  sprintf(filename, "%s/NEXT.EMAIL", dirname);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    closedir(dp);
    printf("Can't create %s\n", filename);
    error_exit();
  }
  fclose(fp);

  maxemailnum = 0;
  minemailnum = 65535;

  printf("** Scanning directory %s\n", dirname);

  while (d = readdir(dp)) {
    if (!strncmp(d->d_name, "EMAIL.DB", 8))
      continue;
    if (!strncmp(d->d_name, "NEXT.EMAIL", 10))
      continue;
    if (strncmp(d->d_name, "EMAIL.", 6))
      continue;
    sscanf(d->d_name, "EMAIL.%u", &emailnum);
    if (emailnum < minemailnum)
      minemailnum = emailnum;
    if (emailnum > maxemailnum)
      maxemailnum = emailnum;
  }
  closedir(dp);

  if (maxemailnum < minemailnum) {
    printf("** No messages in this directory\n");
    error_exit();
  }

  printf("** Will process EMAIL.%u to EMAIL.%u\n", minemailnum, maxemailnum);
  for (emailnum = minemailnum; emailnum <= maxemailnum; ++emailnum) {
    sprintf(filename, "%s/EMAIL.%u", dirname, emailnum);
    fp = fopen(filename, "r");
    if (!fp)
      continue;
    printf("** Processing file %s\n", filename);
    headers = 1;
    headerchars = 0;
    hdrs.emailnum = emailnum;
    hdrs.skipbytes = 0; // Just in case it doesn't get set
    hdrs.status = 'R';
    hdrs.tag = ' ';
    while ((chars = get_line(fp, linebuf, LINEBUFSZ)) != 0) {
      if (headers) {
        headerchars += chars;
        if (!strncmp(linebuf, "Date: ", 6)) {
          copyheader(hdrs.date, linebuf + 6, 39);
          hdrs.date[39] = '\0';
        }
        if (!strncmp(linebuf, "From: ", 6)) {
          copyheader(hdrs.from, linebuf + 6, 79);
          hdrs.from[79] = '\0';
        }
        if (!strncmp(linebuf, "To: ", 4)) {
          copyheader(hdrs.to, linebuf + 4, 79);
          hdrs.to[79] = '\0';
        }
        if (!strncmp(linebuf, "Cc: ", 4)) {
          copyheader(hdrs.cc, linebuf + 4, 79);
          hdrs.cc[79] = '\0';
        }
        if (!strncmp(linebuf, "Subject: ", 9)) {
          copyheader(hdrs.subject, linebuf + 9, 79);
          hdrs.subject[79] = '\0';
        }
        if (linebuf[0] == '\r') {
          headers = 0;
          hdrs.skipbytes = headerchars;
        }
      }
    }
    fclose(fp);
    update_email_db(&hdrs);
  }
  closedir(dp);
  write_next_email(maxemailnum + 1);
  printf("\nRebuilt %s/EMAIL.DB\n", dirname);
  printf("Rebuilt %s/NEXT.EMAIL\n\n", dirname);
}

void main(void) {
  videomode(VIDEOMODE_80COL);
  printf("%c%s Rebuild EMAIL.DB Utility%c\n", 0x0f, PROGNAME, 0x0e);

  printf("\nEnter full path to the mailbox to rebuild> ");
  fgets(dirname, 128, stdin);
  dirname[strlen(dirname) - 1] = '\0'; // Eat '\r'
  if (strlen(dirname) == 0) {
    printf("\nCancelled\n");
    confirm_exit();
  }

  printf("\nUpdating %s\n", dirname);
  repair_mailbox();

  confirm_exit();
}

