/////////////////////////////////////////////////////////////////
// NNTP65.UP
// Network News Transport Protocol (NNTP) Client for IP65
// https://www.ietf.org/rfc/rfc3977.txt
// (Based on smtp65.c, which in turn is based on IP65's wget65.c)
// Bobbi September 2020
/////////////////////////////////////////////////////////////////

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

#define BELL      7
#define BACKSPACE 8
#define NORMAL    0x0e
#define INVERSE   0x0f
#define CLRLINE   0x1a

// Both pragmas are obligatory to have cc65 generate code
// suitable to access the W5100 auto-increment registers.
#pragma optimize      (on)
#pragma static-locals (on)

#define NETBUFSZ  1500
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf[LINEBUFSZ];

uint8_t  exec_email_on_exit = 0;
char     filename[80];
int      len;
FILE     *fp;
uint32_t filesize;
uint16_t nntp_port;

/*
 * Keypress before quit
 */
void confirm_exit(void) {
  printf("\n[Press Any Key]");
  cgetc();
  if (exec_email_on_exit) {
    sprintf(filename, "%s/EMAIL.SYSTEM", cfg_instdir);
    exec(filename, NULL);
  }
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

/*
 * Read a text file a line at a time
 * Returns number of chars in the line, or 0 if EOF.
 * Expects Apple ][ style line endings (CR) and does no conversion
 * fp - file to read from
 * reset - if 1 then just reset the buffer and return
 * writep - Pointer to buffer into which line will be written
 * n - length of buffer. Longer lines will be truncated and terminated with CR.
 */
uint16_t get_line(FILE *fp, uint8_t reset, char *writep, uint16_t n) {
  static uint16_t rd = 0; // Read
  static uint16_t end = 0; // End of valid data in buf
  uint16_t i = 0;
  if (reset) {
    rd = end = 0;
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
    if (writep[i - 1] == '\r')
      goto done;
  }
done:
  writep[i] = '\0';
  return i;
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
      uint16_t pos = 0;
      uint8_t  cont = 1;
      uint16_t snd;
      int16_t len;

      filesize = 0;
      len = get_line(fp, 1, linebuf, LINEBUFSZ); // Reset buffer

      while (cont) { 

        len = get_line(fp, 0, linebuf, LINEBUFSZ - 1);
        pos = 0;

        if (len == 0) {
          strcpy(linebuf, "\r\n.\r\n");
          len = 5;
          cont = 0;
        } else {
          linebuf[len++] = '\n'; // CR -> CRLF
          linebuf[len] = '\0';
          filesize += len;
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
            const char *dataptr = linebuf + pos - 1;
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
      // Handle short ASCII text transmissions
      //
      uint16_t snd;
      uint16_t pos = 0;
      uint16_t len = strlen(sendbuf);

      if (strncmp(sendbuf, "AUTHINFO PASS", 13) == 0)
        printf(">AUTHINFO PASS ****\n");
      else {
        putchar('>');
        print_strip_crlf(sendbuf);
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
    // Must fit in recvbuf[]
    //
    uint16_t rcv;
    uint16_t len = 0;
    uint8_t cont = 1;

    --length; // Leave space for NULL at end in case of buffer overrun

    while (cont) {
      if (input_check_for_abort_key()) {
        printf("User abort\n");
        w5100_disconnect();
        return false;
      }
    
      rcv = w5100_receive_request();
      if (!rcv) {
        cont = w5100_connected();
        if (cont)
          continue;
      }

      if ((length - len) == 0)
        cont = 0;

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
          if (!memcmp(dataptr - 1, "\r\n", 2))
            cont = 0;
        }
      }
      w5100_receive_commit(rcv);
      len += rcv;
    }
    recvbuf[len + 1] = '\0';
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
    printf("\nExpected '%s' got '%s'\n", s, buf);
    return 1;
  }
  return 0;
}

/*
 * Read parms from NEWS.CFG
 */
void readconfigfile(void) {
  char *colon;
  fp = fopen("NEWS.CFG", "r");
  if (!fp) {
    puts("Can't open config file NEWS.CFG");
    error_exit();
  }
  fscanf(fp, "%s", cfg_server);
  fscanf(fp, "%s", cfg_user);
  fscanf(fp, "%s", cfg_pass);
  fscanf(fp, "%s", cfg_instdir);
  fscanf(fp, "%s", cfg_emaildir);
  fscanf(fp, "%s", cfg_emailaddr);
  fclose(fp);

  colon = strchr(cfg_server, ':');
  if (!colon)
    nntp_port = 110;
  else
    nntp_port = atoi(colon + 1);
}

/*
 * Update EMAIL.DB - quick access database for header info
 */
void update_email_db(struct emailhdrs *h) {
  FILE *fp;
  sprintf(filename, "%s/NEWS.SENT/EMAIL.DB", cfg_emaildir);
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
  sprintf(filename, "%s/NEWS.SENT/NEXT.EMAIL", cfg_emaildir);
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
 * Update NEWS.SENT when a message has been sent
 * Copy a messages from OUTBOX to SENT and find headers of interest
 * (Date, From, To, BCC, Subject)
 * filename - Filename in OUTBOX for message just sent
 */
void update_sent_mbox(char *name) {
  static struct emailhdrs hdrs;
  uint16_t nextemail, chars, headerchars;
  uint8_t headers;
  FILE *destfp;
  sprintf(filename, "%s/NEWS.SENT/NEXT.EMAIL", cfg_emaildir);
  fp = fopen(filename, "r");
  if (!fp) {
    nextemail = 1;
    write_next_email(nextemail);
  } else {
    fscanf(fp, "%u", &nextemail);
    fclose(fp);
  }
  strcpy(linebuf, "");
  sprintf(filename, "%s/NEWS.OUTBOX/%s", cfg_emaildir, name);
  fp = fopen(filename, "r");
  if (!fp) {
    printf("Can't open %s\n", filename);
    error_exit();
  }
  hdrs.emailnum = nextemail;
  sprintf(filename, "%s/NEWS.SENT/EMAIL.%u", cfg_emaildir, nextemail++);
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
  get_line(fp, 1, linebuf, LINEBUFSZ); // Reset buffer
  while ((chars = get_line(fp, 0, linebuf, LINEBUFSZ)) != 0) {
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
    fputs(linebuf, destfp);
  }
  fclose(fp);
  fclose(destfp);
  update_email_db(&hdrs);
  write_next_email(nextemail);
}

void main(int argc, char *argv[]) {
  static char sendbuf[80];
  uint8_t linecount;
  DIR *dp;
  struct dirent *d;
  char c;
  uint8_t eth_init = ETH_INIT_DEFAULT, connected = 0;

  if ((argc == 2) && (strcmp(argv[1], "EMAIL") == 0))
    exec_email_on_exit = 1;

  videomode(VIDEOMODE_80COL);
  printf("%c%s NNTP Post News Article(s)%c\n", 0x0f, PROGNAME, 0x0e);

  printf("\nReading NEWS.CFG            -");
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
  printf("Ok\n");

  // Copy IP config from IP65 to W5100
  w5100_config(eth_init);

  sprintf(filename, "%s/NEWS.OUTBOX", cfg_emaildir);
  dp = opendir(filename);
  if (!dp) {
    printf("Can't open dir %s\n", filename);
    error_exit();
  }

  while (d = readdir(dp)) {

    sprintf(filename, "%s/NEWS.OUTBOX/%s", cfg_emaildir, d->d_name);
    fp = fopen(filename, "rb");
    if (!fp) {
        printf("Can't open %s\n", d->d_name);
        continue;
    }

    // Skip special files
    if (!strncmp(d->d_name, "EMAIL.DB", 8))
      goto skiptonext;
    if (!strncmp(d->d_name, "NEXT.EMAIL", 10))
      goto skiptonext;

    printf("\n** Processing file %s ...\n", d->d_name);

    linecount = 0;

    while (1) {
      if ((get_line(fp, 0, linebuf, LINEBUFSZ) == 0) || (linecount == 20))
        break;
      ++linecount;
      if (!strncmp(linebuf, "Newsgroups: ", 12))
        printf("%s", linebuf);
      if (!strncmp(linebuf, "Subject: ", 9))
        printf("%s", linebuf);
    }

    printf("\n%cS)end message | H)old message in NEWS.OUTBOX | D)elete message from NEWS.OUTBOX %c",
           INVERSE, NORMAL);
    while (1) {
      c = cgetc();
      switch (c) {
      case 'S':
      case 's':
        goto sendmessage;
      case 'H':
      case 'h':
        printf("\n  Holding message\n");
        fclose(fp);
        goto skiptonext;
      case 'D':
      case 'd':
        printf("Sure? (y/n)");
        while (1) {
          c = cgetc();
          switch (c) {
          case 'Y':
          case 'y':
            putchar(CLRLINE);
            printf("\n  Deleting message\n");
            fclose(fp);
            goto unlink;
          case 'N':
          case 'n':
            putchar(CLRLINE);
            printf("\n  Holding message\n");
            fclose(fp);
            goto skiptonext;
          default:
            putchar(BELL);
          }
        }
        break;
      default:
        putchar(BELL);
      }
    }

sendmessage:

    if (!connected) {
      printf("Connecting to %s (%u)  - ", cfg_server, nntp_port);

      if (!w5100_connect(parse_dotted_quad(cfg_server), nntp_port)) {
        printf("Fail\n");
        error_exit();
      }

      printf("Ok\n\n");

      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DONT_SEND, CMD_MODE)) {
        error_exit();
      }
      if (expect(buf, "200 ")) // "200" if posting is allowed
        error_exit();

      sprintf(sendbuf, "AUTHINFO USER %s\r\n", cfg_user);
      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      if (expect(buf, "381")) // Username accepted
        error_exit();

      sprintf(sendbuf, "AUTHINFO PASS %s\r\n", cfg_pass);
      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      if (expect(buf, "281")) // Authentication successful
        error_exit();

      connected = 1;
    }

    if (!w5100_tcp_send_recv("POST\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    if (expect(buf, "340"))
      error_exit();

    fseek(fp, 0, SEEK_SET);

    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, DATA_MODE)) {
      error_exit();
    }
    expect(buf, "240");

    fclose(fp);

    printf("Updating NEWS.SENT mailbox ...\n");
    update_sent_mbox(d->d_name);

    printf("Removing from NEWS.OUTBOX ...\n");
    sprintf(filename, "%s/NEWS.OUTBOX/%s", cfg_emaildir, d->d_name);

unlink:
    if (unlink(filename))
      printf("Can't remove %s\n", filename);

skiptonext:
    if (fp)
      fclose(fp);
  }
  closedir(dp);

  // Ignore any error - can be a race condition where other side
  // disconnects too fast and we get an error
  if (connected) {
    w5100_tcp_send_recv("QUIT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE);
    printf("Disconnecting\n");
    w5100_disconnect();
  } else
    printf("\n** No messages were sent **\n");

  confirm_exit();
}
