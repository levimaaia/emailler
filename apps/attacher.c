/////////////////////////////////////////////////////////////////////////////
// Handle attaching files to outgoing messages
// Bobbi July 2020
/////////////////////////////////////////////////////////////////////////////

#include <conio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "email_common.h"

#define BELL      0x07
#define BACKSPACE 0x08
#define RETURN    0x0d
#define NORMAL    0x0e
#define INVERSE   0x0f
#define DELETE    0x7f

#define NETBUFSZ  1500
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf[LINEBUFSZ];
static char          userentry[80];
static uint8_t       quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit
static char          filename[80];

#define ERR_NONFATAL 0
#define ERR_FATAL    1

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
    va_start(v, fmt);
    vprintf(fmt, v);
    va_end(v);
    printf(" - [Press Any Key]");
    cgetc();
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
 * Read a text file a line at a time leaving the line in linebuf[]
 * Returns number of chars in the line, or -1 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * reset - if 1 then just reset the buffer and return
 */
int16_t get_line(FILE *fp, uint8_t reset) {
  static uint16_t rd = 0; // Read
  static uint16_t wt = 0; // Write
  uint8_t found = 0;
  uint16_t j = 0;
  uint16_t i;
  if (reset) {
    rd = wt = 0;
    return 0;
  }
  while (1) {
    while (rd < wt) {
      linebuf[j++] = buf[rd++];
      if (linebuf[j - 1] == '\r') {
        found = 1;
        break;
      }
    }
    linebuf[j] = '\0';
    if (rd == wt) // Empty buf
      rd = wt = 0;
    if (found) {
      return j;
    }
    if (feof(fp)) {
      return -1;
    }
    i = fread(&buf[wt], 1, READSZ - wt, fp);
    wt += i;
  }
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
 * Prompt for a name, store it in userentry
 * Returns number of chars read.
 * prompt - Message to display before > prompt
 * is_file - if 1, restrict chars to those allowed in ProDOS filename
 * Returns number of chars read
 */
uint8_t prompt_for_name(char *prompt, uint8_t is_file) {
  uint16_t i;
  char c;
  printf("%s>", prompt);
  i = 0;
  while (1) {
    c = cgetc();
    if (is_file && !isalnum(c) &&
        (c != RETURN) && (c != BACKSPACE) && (c != DELETE) &&
        (c != '.') && (c != '/')) {
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
  return i;
}

/*
 * Optionally attach files to outgoing email.
 * filename - Name of file containing email message
 */
void attach(char *fname) {
  FILE *fp, *fp2, *destfp;
  uint16_t chars, i, size;
  videomode(VIDEOMODE_80COL);
  printf("%c%s ATTACHER%c\n\n", 0x0f, PROGNAME, 0x0e);
  fp = fopen(fname, "rb+");
  if (!fp)
    error(ERR_FATAL, "Can't open %s", fname);
  sprintf(filename, "%s/OUTBOX/TMPFILE", cfg_emaildir);
  destfp = fopen(filename, "wb");
  if (!destfp)
    error(ERR_FATAL, "Can't open TMPFILE");

  get_line(fp, 1); // Reset buffer
  printf("Copying email content ...  "); // Space is for spinner to eat
  size = 0;
  while ((chars = get_line(fp, 0)) != -1) {
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
  while ((chars = get_line(fp, 0)) != -1) {
    size += chars;
    fputs(linebuf, destfp);
    spinner(size, 0);
  }
  spinner(size, 1);

  printf("\rEnter the filename or filenames to attach to the email.\n");
  printf("An empty entry means you are done attaching files.\n\n");
  while (prompt_for_name("File to attach", 1)) {
    fp2 = fopen(userentry, "rb");
    if (!fp2) {
      error(ERR_NONFATAL, "Can't open %s", userentry);
      continue;
    }
    fprintf(destfp, "\r--a2forever\r");
    fprintf(destfp, "Content-Type: application/octet-stream\r");
    fprintf(destfp, "Content-Transfer-Encoding: base64\r");
// TODO: filename should be just the basename in the following line
    fprintf(destfp, "Content-Disposition: attachment; filename=%s;\r\r", userentry);
    printf("\r "); // Space is for spinner to eat
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
  }
  fprintf(destfp, "\r--a2forever--\r");
  fclose(fp);
  fclose(destfp);
  if (unlink(fname))
    error(ERR_FATAL, "Can't delete %s", fname);
  if (rename(filename, fname))
    error(ERR_FATAL, "Can't rename %s to %s", filename, fname);
}

/*
 * Load EMAIL.SYSTEM to $2000 and jump to it
 * (This code is in language card space so it can't possibly be trashed)
 */
#pragma code-name (push, "LC")
void load_email(void) {
  revers(0);
  clrscr();
  exec("EMAIL.SYSTEM", NULL); // Assume it is in current directory
}
#pragma code-name (pop)

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


