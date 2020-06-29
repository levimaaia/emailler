/////////////////////////////////////////////////////////////////
// SMTP65
// Simple Mail Transport Protocol (SMTO) Client for IP65
// https://www.ietf.org/rfc/rfc821
// (Based on IP65's wget65.c)
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

// TODO See below

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

#include "../inc/ip65.h"
#include "w5100.h"
#include "w5100_http.h"
#include "linenoise.h"

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
// mode Binary mode for received message, maybe first block of long message
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
 */
void expect(char *buf, char *s) {
  if (strncmp(buf, s, strlen(s)) != 0) {
    printf("\nExpected '%s' got '%s\n", s, buf);
    error_exit();
  }
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
    fscanf(fp, "%s", cfg_smtp_server);
    fscanf(fp, "%s", cfg_smtp_domain);
    fscanf(fp, "%s", cfg_emaildir);
    fclose(fp);
}

/*
 * Read a text file a line at a time leaving the line in linebuf[]
 * Returns number of chars in the line, or -1 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 */
int16_t get_line(FILE *fp) {
  static uint16_t rd = 0;
  static uint16_t buflen = 0;
  uint8_t found = 0;
  uint16_t j = 0;
  uint16_t i;
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

void main(void) {
  static char sendbuf[80], sender[80], recipient[80];
  uint8_t eth_init = ETH_INIT_DEFAULT;
  uint8_t have_recipient, have_sender, linecount;
  DIR *dp;
  struct dirent *d;

  videomode(VIDEOMODE_80COL);
  printf("%cemai//er SMTP%c\n", 0x0f, 0x0e);

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
  expect(buf, "220 ");

  sprintf(sendbuf, "HELO %s\r\n", cfg_smtp_domain);
  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
    error_exit();
  }
  expect(buf, "250 ");

  sprintf(filename, "%s/OUTBOX", cfg_emaildir);
  dp = opendir(filename);
  if (!dp) {
    printf("Can't open dir %s\n", filename);
    error_exit();
  }

  while (d = readdir(dp)) {

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

    puts(d->d_name);

    have_sender = have_recipient = linecount = 0;

    while (1) {
      if ((get_line(fp) == -1) || (linecount == 20)) {
        printf("Didn't find to/from headers in %s\n", d->d_name);
        goto skiptonext;
      }
      ++linecount;
      if (!strncmp(linebuf, "To: ", 4)) {
        strcpy(recipient, linebuf + 4);
        have_recipient = 1;
        if (have_sender)
          break;
      } else if (!strncmp(linebuf, "From: ", 6)) {
        strcpy(sender, linebuf + 6);
        have_sender = 1;
        if (have_recipient)
          break;
      }
      // TODO Handle optional cc
    }

    // TODO Handle multiple comma-separated recipients

    // TODO Chop trailing \r off sender & recipient

    sprintf(sendbuf, "MAIL FROM:<%s>\r\n", sender);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    expect(buf, "250 ");

    sprintf(sendbuf, "RCPT TO:<%s>\r\n", recipient);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    expect(buf, "250 ");

    sprintf(sendbuf, "DATA\r\n");
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    expect(buf, "354 ");

    fseek(fp, 0, SEEK_SET);

    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, DATA_MODE)) {
      error_exit();
    }
    expect(buf, "250 ");

skiptonext:
    fclose(fp);

    //TODO: Copy file to SENT & remove from here

  }
  closedir(dp);

  // Ignore any error - can be a race condition where other side
  // disconnects too fast and we get an error
  w5100_tcp_send_recv("QUIT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE);

  printf("Disconnecting\n");
  w5100_disconnect();

  confirm_exit();
}
