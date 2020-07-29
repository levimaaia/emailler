/////////////////////////////////////////////////////////////////
// POP65
// Post Office Protocol v3 (POP3) Client for IP65
// https://www.ietf.org/rfc/rfc1939.txt
// (Based on IP65's wget65.c)
// Bobbi June 2020
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
#include <apple2_filetype.h>

#include "../inc/ip65.h"
#include "w5100.h"

#include "email_common.h"

#define BACKSPACE 8

// Both pragmas are obligatory to have cc65 generate code
// suitable to access the W5100 auto-increment registers.
#pragma optimize      (on)
#pragma static-locals (on)

#define NETBUFSZ  1500+4       // 4 extra bytes for overlap between packets
#define LINEBUFSZ 1000         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf_pad[1];     // One byte of padding make it easier
static char          linebuf[LINEBUFSZ];

uint8_t  exec_email_on_exit = 0;
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
 * Spinner while downloading files
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
    uint16_t snd;
    uint16_t pos = 0;
    uint16_t len = strlen(sendbuf);

    if (strncmp(sendbuf, "PASS", 4) == 0)
      printf(">PASS ****\n");
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

  if (mode == DATA_MODE) {
    //
    // Handle email body
    //
    uint16_t rcv, written;
    uint16_t len = 0;
    uint8_t cont = 1;

    // Backspace to put spinner on same line as RETR
    for (rcv = 0; rcv < 15; ++rcv)
      putchar(BACKSPACE);

    filesize = 0;

    // Initialize 4 byte overlap to zero
    bzero(recvbuf, 4);

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

      if (rcv > length - len)
        rcv = length - len;

      {
        // One less to allow for faster pre-increment below
        // 4 bytes of overlap between blocks
        char *dataptr = recvbuf + len + 4 - 1;
        uint16_t i;
        for (i = 0; i < rcv; ++i) {
          // The variable is necessary to have cc65 generate code
          // suitable to access the W5100 auto-increment register.
          char data = *w5100_data;
          *++dataptr = data;
          if (!memcmp(dataptr - 4, "\r\n.\r\n", 5))
            cont = 0;
        }
      }
      w5100_receive_commit(rcv);
      len += rcv;

      // Skip 4 byte overlap
      written = fwrite(recvbuf + 4, 1, len, fp);
      if (written != len) {
        printf("Write error");
        fclose(fp);
        error_exit();
      }

      // Copy 4 bytes of overlap
      memcpy(recvbuf, recvbuf + len, 4);

      filesize += len;
      spinner(filesize, 0);
      len = 0;
    }
  } else {
    //
    // Handle short single packet ASCII text responses
    //
    uint16_t rcv;
    uint16_t len = 0;

    while (1) {
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
 * Read parms from EMAIL.CFG
 */
void readconfigfile(void) {
    fp = fopen("EMAIL.CFG", "r");
    if (!fp) {
      puts("Can't open config file EMAIL.CFG");
      error_exit();
    }
    fscanf(fp, "%s", cfg_server);
    fscanf(fp, "%s", cfg_user);
    fscanf(fp, "%s", cfg_pass);
    fscanf(fp, "%s", cfg_pop_delete);
    fscanf(fp, "%s", cfg_smtp_server);
    fscanf(fp, "%s", cfg_smtp_domain);
    fscanf(fp, "%s", cfg_instdir);
    fscanf(fp, "%s", cfg_emaildir);
    fclose(fp);
}

/*
 * Read a text file a line at a time
 * Returns number of chars in the line, or -1 if EOF.
 * Expects CRLF line endings (CRLF) and converts to Apple II (CR) convention
 * fp - file to read from
 * writep - Pointer to buffer into which line will be written
 */
int16_t get_line(FILE *fp, char *writep) {
  static uint16_t rd = 0; // Read
  static uint16_t end = 0; // End of valid data in buf
  uint16_t i = 0;
  while (1) {
    if (rd == end) {
      end = fread(buf, 1, READSZ, fp);
      rd = 0;
    }
    if (end == 0)
      return -1; // EOF
    writep[i++] = buf[rd++];
    // The following line is safe because of linebuf_pad[]
    if ((writep[i - 1] == '\n') && (writep[i - 2] == '\r')) {
      writep[i - 1] = '\0'; // Remove LF
      return i - 1;
    }
  }
}

/*
 * Update EMAIL.DB - quick access database for header info
 */
void update_email_db(struct emailhdrs *h) {
  FILE *fp;
  sprintf(filename, "%s/INBOX/EMAIL.DB", cfg_emaildir);
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
  sprintf(filename, "%s/INBOX/NEXT.EMAIL", cfg_emaildir);
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
 * Update INBOX
 * Copy messages from spool dir to inbox and find headers of interest
 * (Date, From, To, BCC, Subject)
 */
void update_inbox(uint16_t nummsgs) {
  static struct emailhdrs hdrs;
  uint16_t nextemail, msg, chars, headerchars;
  uint8_t headers;
  FILE *destfp;
  sprintf(filename, "%s/INBOX/NEXT.EMAIL", cfg_emaildir);
  fp = fopen(filename, "r");
  if (!fp) {
    nextemail = 1;
    write_next_email(nextemail);
  } else {
    fscanf(fp, "%u", &nextemail);
    fclose(fp);
  }
  for (msg = 1; msg <= nummsgs; ++msg) {
    strcpy(linebuf, "");
    sprintf(filename, "%s/SPOOL/EMAIL.%u", cfg_emaildir, msg);
    fp = fopen(filename, "r");
    if (!fp) {
      printf("Can't open %s\n", filename);
      error_exit();
    }
    hdrs.emailnum = nextemail;
    sprintf(filename, "%s/INBOX/EMAIL.%u", cfg_emaildir, nextemail++);
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
    while ((chars = get_line(fp, linebuf)) != -1) {
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

    sprintf(filename, "%s/SPOOL/EMAIL.%u", cfg_emaildir, msg);
    if (unlink(filename))
      printf("Can't delete %s\n", filename);
  }
  write_next_email(nextemail);
}

void main(int argc, char *argv[]) {
  uint8_t eth_init = ETH_INIT_DEFAULT;
  char sendbuf[80];
  uint16_t msg, nummsgs;
  uint32_t bytes;

  if ((argc == 2) && (strcmp(argv[1], "EMAIL") == 0))
    exec_email_on_exit = 1;

  linebuf_pad[0] = 0;

  videomode(VIDEOMODE_80COL);
  printf("%c%s POP3%c\n", 0x0f, PROGNAME, 0x0e);

  printf("\nReading EMAIL.CFG            -");
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

  printf("Ok\nConnecting to %s   - ", cfg_server);

  if (!w5100_connect(parse_dotted_quad(cfg_server), 110)) {
    printf("Fail\n");
    error_exit();
  }

  printf("Ok\n\n");

  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DONT_SEND, CMD_MODE)) {
    error_exit();
  }
  expect(buf, "+OK");

  sprintf(sendbuf, "USER %s\r\n", cfg_user);
  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
    error_exit();
  }
  expect(buf, "+OK");

  sprintf(sendbuf, "PASS %s\r\n", cfg_pass);
  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
    error_exit();
  }
  expect(buf, "+OK Logged in.");

  if (!w5100_tcp_send_recv("STAT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
    error_exit();
  }
  sscanf(buf, "+OK %u %lu", &nummsgs, &bytes);
  printf(" %u message(s), %lu total bytes\n", nummsgs, bytes);

  for (msg = 1; msg <= nummsgs; ++msg) {
    sprintf(filename, "%s/SPOOL/EMAIL.%u", cfg_emaildir, msg);
    _filetype = PRODOS_T_TXT;
    _auxtype = 0;
    fp = fopen(filename, "wb"); 
    if (!fp) {
      printf("Can't create %s\n", filename);
      error_exit();
    }
    sprintf(sendbuf, "RETR %u\r\n", msg);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, DATA_MODE)) {
      error_exit();
    }
    spinner(filesize, 1); // Cleanup spinner
    if (strcmp(cfg_pop_delete, "DELETE") == 0) {
      sprintf(sendbuf, "DELE %u\r\n", msg);
      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      expect(buf, "+OK");
    }
    fclose(fp);
  }

  // Ignore any error - can be a race condition where other side
  // disconnects too fast and we get an error
  w5100_tcp_send_recv("QUIT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE);

  printf("Disconnecting\n");
  w5100_disconnect();

  printf("Updating INBOX ...\n");
  update_inbox(nummsgs);

  confirm_exit();
}
