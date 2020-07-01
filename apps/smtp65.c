/////////////////////////////////////////////////////////////////
// SMTP65
// Simple Mail Transport Protocol (SMTP) Client for IP65
// https://www.ietf.org/rfc/rfc821
// (Based on IP65's wget65.c)
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// TODO: Clean up EMAIL.DB in OUTBOX after sending

#include <cc65.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <apple2_filetype.h>

#include "../inc/ip65.h"
#include "w5100.h"

#include "email_common.h"

#define BACKSPACE 8

// Both pragmas are obligatory to have cc65 generate code
// suitable to access the W5100 auto-increment registers.
#pragma optimize      (on)
#pragma static-locals (on)

#define NETBUFSZ  1500
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf[LINEBUFSZ];

char     filename[80];
int      len;
FILE     *fp;
uint32_t filesize;

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
 * Called if IP65 call fails
 */
void ip65_error_exit(void) {
  printf("%s\n", ip65_strerror(ip65_error));
  confirm_exit();
}

/*
 * Print message to the console, stripping extraneous CRLF stuff
 * from the end.
 */
void print_strip_crlf(char *s) {
  uint8_t i = 0;
  while ((s[i] != '\0') && (s[i] != '\r') && (s[i] != '\n'))
    putchar(s[i++]);
  putchar('\n');
}

/*
 * Spinner while uploading files
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

#define DO_SEND   1  // For do_send param
#define DONT_SEND 0  // For do_send param
#define CMD_MODE  0  // For mode param
#define DATA_MODE 1  // For mode param

// Modified verson of w5100_http_open from w5100_http.c
// Sends a TCP message and receives a the first packet of the response.
// sendbuf is the buffer to send (null terminated)
// recvbuf is the buffer into which the received message will be written
// length is the length of recvbuf[]
// do_send Do the sending if true, otherwise skip
// mode Binary mode for sent message, pump data from disk file fp
bool w5100_tcp_send_recv(char* sendbuf, char* recvbuf, size_t length,
                         uint8_t do_send, uint8_t mode) {

  if (do_send == DO_SEND) {

    if (mode == DATA_MODE) {
      //
      // Handle sending of email body
      //
      uint16_t snd;
      uint16_t pos = 0;
      uint16_t len;
      uint8_t  cont = 1;

      filesize = 0;

      while (cont) { 

        len = fread(buf, 1, READSZ, fp);
        filesize += len;

        if (len == 0) {
          strcpy(buf, "\r\n.\r\n");
          pos = 0;
          len = 5;
          cont = 0;
        }

        while (len) {
          if (input_check_for_abort_key())
          {
            printf("User abort\n");
            w5100_disconnect();
            return false;
          }

          snd = w5100_send_request();
          if (!snd) {
            if (!w5100_connected()) {
              printf("Connection lost\n");
              return false;
            }
            continue;
          }

          if (len < snd)
            snd = len;

          {
            // One less to allow for faster pre-increment below
            const char *dataptr = buf + pos - 1;
            uint16_t i;
            for (i = 0; i < snd; ++i) {
              // The variable is necessary to have cc65 generate code
              // suitable to access the W5100 auto-increment register.
              char data = *++dataptr;
              *w5100_data = data;
            }
          }

          w5100_send_commit(snd);
          len -= snd;
          pos += snd;
        }
        spinner(filesize, 0);
      }
      spinner(filesize, 1);

    } else {
      //
      // Handle short single packet ASCII text transmissions
      //
      uint16_t snd;
      uint16_t pos = 0;
      uint16_t len = strlen(sendbuf);

      putchar('>');
      print_strip_crlf(sendbuf);

      while (len) {
        if (input_check_for_abort_key())
        {
          printf("User abort\n");
          w5100_disconnect();
          return false;
        }

        snd = w5100_send_request();
        if (!snd) {
          if (!w5100_connected()) {
            printf("Connection lost\n");
            return false;
          }
          continue;
        }

        if (len < snd)
          snd = len;

        {
          // One less to allow for faster pre-increment below
          const char *dataptr = sendbuf + pos - 1;
          uint16_t i;
          for (i = 0; i < snd; ++i) {
            // The variable is necessary to have cc65 generate code
            // suitable to access the W5100 auto-increment register.
            char data = *++dataptr;
            *w5100_data = data;
          }
        }

        w5100_send_commit(snd);
        len -= snd;
        pos += snd;
      }
    }
  }

  {
    //
    // Handle short single packet ASCII text responses
    //
    uint16_t rcv;
    uint16_t len = 0;

    while(1) {
      if (input_check_for_abort_key()) {
        printf("User abort\n");
        w5100_disconnect();
        return false;
      }
    
      rcv = w5100_receive_request();
      if (rcv)
        break;
      if (!w5100_connected()) {
        printf("Connection lost\n");
        return false;
      }
    }

    if (rcv > length - len)
      rcv = length - len;

    {
      // One less to allow for faster pre-increment below
      char *dataptr = recvbuf + len - 1;
      uint16_t i;
      for (i = 0; i < rcv; ++i) {
        // The variable is necessary to have cc65 generate code
        // suitable to access the W5100 auto-increment register.
        char data = *w5100_data;
        *++dataptr = data;
      }
      w5100_receive_commit(rcv);
      len += rcv;
    }
    putchar('<');
    print_strip_crlf(recvbuf);
  }
  return true;
}

/*
 * Check expected string from server
 * Returns 0 if expected, 1 otherwise
 */
uint8_t expect(char *buf, char *s) {
  if (strncmp(buf, s, strlen(s)) != 0) {
    printf("\nExpected '%s' got '%s\n", s, buf);
    return 1;
  }
  return 0;
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
    fscanf(fp, "%s", cfg_pop_delete);
    fscanf(fp, "%s", cfg_smtp_server);
    fscanf(fp, "%s", cfg_smtp_domain);
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
  static uint16_t rd = 0;
  static uint16_t buflen = 0;
  uint8_t found = 0;
  uint16_t j = 0;
  uint16_t i;
  if (reset) {
    rd = buflen = 0;
    return 0;
  }
  while (1) {
    for (i = rd; i < buflen; ++i) {
      linebuf[j++] = buf[i];
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
 * Update EMAIL.DB - quick access database for header info
 */
void update_email_db(struct emailhdrs *h) {
  FILE *fp;
  sprintf(filename, "%s/SENT/EMAIL.DB", cfg_emaildir);
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
 * Write NEXT.EMAIL file with number of next EMAIL.n file to be created
 */
void write_next_email(uint16_t num) {
  sprintf(filename, "%s/SENT/NEXT.EMAIL", cfg_emaildir);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "wb");
  if (!fp) {
    printf("Can't open %s\n", filename);
    fclose(fp);
    error_exit();
  }
  fprintf(fp, "%u", num);
  fclose(fp);
}

/*
 * Update SENT when a message has been sent
 * Copy a messages from OUTBOX to SENT and find headers of interest
 * (Date, From, To, BCC, Subject)
 * filename - Filename in OUTBOX for message just sent
 */
void update_sent_mbox(char *name) {
  static struct emailhdrs hdrs;
  uint16_t nextemail, chars, headerchars;
  uint8_t headers;
  FILE *destfp;
  sprintf(filename, "%s/SENT/NEXT.EMAIL", cfg_emaildir);
  fp = fopen(filename, "r");
  if (!fp) {
    nextemail = 1;
    write_next_email(nextemail);
  } else {
    fscanf(fp, "%u", &nextemail);
    fclose(fp);
  }
  strcpy(linebuf, "");
  sprintf(filename, "%s/OUTBOX/%s", cfg_emaildir, name);
  fp = fopen(filename, "r");
  if (!fp) {
    printf("Can't open %s\n", filename);
    error_exit();
  }
  hdrs.emailnum = nextemail;
  sprintf(filename, "%s/SENT/EMAIL.%u", cfg_emaildir, nextemail++);
  puts(filename);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  destfp = fopen(filename, "wb");
  if (!destfp) {
    printf("Can't open %s\n", filename);
    fclose(fp);
    error_exit();
  }
  headers = 1;
  headerchars = 0;
  hdrs.skipbytes = 0; // Just in case it doesn't get set
  hdrs.status = 'N';
  hdrs.tag = ' ';
  get_line(fp, 1); // Reset buffer
  while ((chars = get_line(fp, 0)) != -1) {
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
      //if (linebuf[0] == '\r') {
      if (strlen(linebuf) < 10) {
        headers = 0;
        hdrs.skipbytes = headerchars;
      }
    }
    fputs(linebuf, destfp);
  }
  fclose(fp);
  fclose(destfp);
  update_email_db(&hdrs);
  write_next_email(nextemail);
}

void main(void) {
  static char sendbuf[80], recipients[160];
  uint8_t eth_init = ETH_INIT_DEFAULT;
  uint8_t linecount;
  DIR *dp;
  struct dirent *d;
  char *p, *q;

  videomode(VIDEOMODE_80COL);
  printf("%c%s SMTP%c\n", 0x0f, PROGNAME, 0x0e);

  printf("\nReading POP65.CFG            -");
  readconfigfile();
  printf(" Ok");

  {
    int file;

    printf("\nSetting slot                 - ");
    file = open("ethernet.slot", O_RDONLY);
    if (file != -1) {
      read(file, &eth_init, 1);
      close(file);
      eth_init &= ~'0';
    }
  }

  printf("%d\nInitializing %s     - ", eth_init, eth_name);
  if (ip65_init(eth_init)) {
    ip65_error_exit();
  }

  // Abort on Ctrl-C to be consistent with Linenoise
  abort_key = 0x83;

  printf("Ok\nObtaining IP address         - ");
  if (dhcp_init()) {
    ip65_error_exit();
  }

  // Copy IP config from IP65 to W5100
  w5100_config(eth_init);

  printf("Ok\nConnecting to %s   - ", cfg_smtp_server);

  if (!w5100_connect(parse_dotted_quad(cfg_smtp_server), 25)) {
    printf("Fail\n");
    error_exit();
  }

  printf("Ok\n\n");

  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DONT_SEND, CMD_MODE)) {
    error_exit();
  }
  if (expect(buf, "220 "))
    error_exit();

  sprintf(sendbuf, "HELO %s\r\n", cfg_smtp_domain);
  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
    error_exit();
  }
  if (expect(buf, "250 "))
    error_exit();

  sprintf(filename, "%s/OUTBOX", cfg_emaildir);
  dp = opendir(filename);
  if (!dp) {
    printf("Can't open dir %s\n", filename);
    error_exit();
  }

  while (d = readdir(dp)) {

skiptonext:

    sprintf(filename, "%s/OUTBOX/%s", cfg_emaildir, d->d_name);
    fp = fopen(filename, "rb");
    if (!fp) {
        printf("Can't open %s\n", d->d_name);
        continue;
    }

    // Skip special files
    if (!strncmp(d->d_name, "EMAIL.DB", 8))
      continue;
    if (!strncmp(d->d_name, "NEXT.EMAIL", 10))
      continue;

    printf("\n** Processing file %s ...\n", d->d_name);

    linecount = 0;
    strcpy(recipients, "");

    while (1) {
      if ((get_line(fp, 0) == -1) || (linecount == 20)) {
        if (strlen(recipients) == 0) {
          printf("No recipients (To or Cc) in %s. Skipping msg.\n", d->d_name);
          fclose(fp);
          goto skiptonext;
        }
        break;
      }
      ++linecount;
      if (!strncmp(linebuf, "To: ", 4) || (!strncmp(linebuf, "cc: ",4))) {
        linebuf[strlen(linebuf) - 1] = '\0'; // Chop off \r
        if (strlen(linebuf + 4) > 0) {
          if (strlen(recipients) > 0)
            strcat(recipients, ",");
          strcat(recipients, linebuf + 4);
        }
      }
    }

    sprintf(sendbuf, "MAIL FROM:<%s>\r\n", cfg_emailaddr);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    if (expect(buf, "250 ")) {
      printf("Skipping msg\n");
      continue;
    }

    // Handle multiple comma-separated recipients
    p = recipients;
    while (q = strchr(p, ',')) {
      *q = '\0';
      while (*p == ' ')
        ++p;
      sprintf(sendbuf, "RCPT TO:<%s>\r\n", p);
      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      if (expect(buf, "250 ")) {
        printf("Skipping msg\n");
        fclose(fp);
        goto skiptonext;
      }
      p = q + 1;
    }
    while (*p == ' ')
      ++p;
    sprintf(sendbuf, "RCPT TO:<%s>\r\n", p);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    if (expect(buf, "250 ")) {
      printf("Skipping msg\n");
      fclose(fp);
      continue;
    }

    sprintf(sendbuf, "DATA\r\n");
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    if (expect(buf, "354 ")) {
      printf("Skipping msg\n");
      fclose(fp);
      continue;
    }

    fseek(fp, 0, SEEK_SET);

    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, DATA_MODE)) {
      error_exit();
    }
    expect(buf, "250 ");

    fclose(fp);

    printf("Updating SENT mailbox ...\n");
    update_sent_mbox(d->d_name);

    printf("Removing from OUTBOX ...\n");
    sprintf(filename, "%s/OUTBOX/%s", cfg_emaildir, d->d_name);
    if (unlink(filename))
      printf("Can't remove %s\n", filename);
  }
  closedir(dp);

  // Ignore any error - can be a race condition where other side
  // disconnects too fast and we get an error
  w5100_tcp_send_recv("QUIT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE);

  printf("Disconnecting\n");
  w5100_disconnect();

  confirm_exit();
}
