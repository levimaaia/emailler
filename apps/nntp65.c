/////////////////////////////////////////////////////////////////
// NNTP65
// Network News Transport Protocol (NNTP) Client for IP65
// https://www.ietf.org/rfc/rfc3977.txt
// (Based on smtp65.c, which in turn is based on IP65's wget65.c)
// Bobbi September 2020
/////////////////////////////////////////////////////////////////

#include <cc65.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
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

#define BELL      7
#define BACKSPACE 8

// Both pragmas are obligatory to have cc65 generate code
// suitable to access the W5100 auto-increment registers.
#pragma optimize      (on)
#pragma static-locals (on)

#define NETBUFSZ  1500+4       // 4 extra bytes for overlap between packets
#define LINEBUFSZ 2000 /*1000*/         // According to RFC2822 Section 2.1.1 (998+CRLF)
#define READSZ    1024         // Must be less than NETBUFSZ to fit in buf[]

static unsigned char buf[NETBUFSZ+1];    // One extra byte for null terminator
static char          linebuf_pad[1];     // One byte of padding make it easier
static char          linebuf[LINEBUFSZ];
static char          newsgroup[80];
static char          mailbox[80];

uint8_t  exec_email_on_exit = 0;
char     filename[80];
int      len;
FILE     *fp, *newsgroupsfp, *newnewsgroupsfp;
uint32_t filesize;
uint16_t nntp_port;

/*
 * Keypress before quit
 */
void confirm_exit(void) {
  fclose(fp);
  fclose(newsgroupsfp);
  fclose(newnewsgroupsfp);
  w5100_disconnect();
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

  if (mode == DATA_MODE) {
    //
    // Handle article body
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
        return false;
      }

      // Copy 4 bytes of overlap
      memcpy(recvbuf, recvbuf + len, 4);

      filesize += len;
      spinner(filesize, 0);
      len = 0;
    }
  } else {
    //
    // Handle short single line ASCII text responses
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
        // 4 bytes of overlap between blocks
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
  fclose(fp);

  colon = strchr(cfg_server, ':');
  if (!colon)
    nntp_port = 110;
  else {
    nntp_port = atoi(colon + 1);    
    *colon = '\0';
  }
}

// Entry on kill-list
struct killent {
  char *address;
  struct killent *next;
};

struct killent *killlist = NULL;

/*
 * Read email addresses from KILL.LIST.CFG
 * Returns 0 if kill-list exists, 1 if it does not
 */
uint8_t readkilllist(void) {
  struct killent *p, *q;
  char *s;
  fp = fopen("KILL.LIST.CFG", "r");
  if (!fp)
    return 1;
  killlist = p = malloc(sizeof(struct killent));
  killlist->address = NULL;
  killlist->next = NULL;
  while (!feof(fp)) {
    fscanf(fp, "%s", linebuf);
    s = malloc(strlen(linebuf) + 1);
    strcpy(s, linebuf);
    q = malloc(sizeof(struct killent));
    q->address = s;
    q->next = NULL;
    p->next = q;
    p = q;
  }
  fclose(fp);
  return 0;
}

/*
 * Read a text file a line at a time
 * Returns number of chars in the line, or 0 if EOF.
 * Expects CRLF line endings (CRLF) and converts to Apple II (CR) convention
 * fp - file to read from
 * writep - Pointer to buffer into which line will be written
 * n - length of buffer. Longer lines will be truncated and terminated with CR.
 */
uint16_t get_line(FILE *fp, char *writep, uint16_t n) {
  static uint16_t rd = 0; // Read
  static uint16_t end = 0; // End of valid data in buf
  uint16_t i = 0;
  // Special case, if fp is NULL then reset the state
  if (!fp) {
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
    writep[i++] = buf[rd++];
    if (i == n - 1) {
      writep[i - 1] = '\r';
      goto done;
    }
    // The following line is safe because of linebuf_pad[]
    if ((writep[i - 1] == '\n') && (writep[i - 2] == '\r')) {
      writep[i - 1] = '\0'; // Remove LF
      return i - 1;
    }
  }
done:
  writep[i] = '\0';
  return i;
}

/*
 * Update EMAIL.DB - quick access database for header info
 */
void update_email_db(char *mbox, struct emailhdrs *h) {
  FILE *fp;
  sprintf(filename, "%s/%s/EMAIL.DB", cfg_emaildir, mbox);
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
#if 0
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
#endif

/*
 * Check if a sender is on the kill-list.
 * sender - email address of sender
 * Returns 1 if on kill-list, false otherwise.
 */
uint8_t sender_is_on_killlist(char *sender) {
  struct killent *p = killlist;
  while (p && p->next) {
    if (strstr(sender, p->address))
      return 1;
    p = p->next;
  }
  return 0;
}

/*
 * Update mailbox
 * Copy messages from spool dir to mailbox and find headers of interest
 * (Date, From, Subject)
 */
void update_mailbox(char *mbox) {
  static struct emailhdrs hdrs;
  struct dirent *d;
  uint16_t msg, chars, headerchars;
  uint8_t headers, onkilllist;
  FILE *destfp;
  DIR *dp;
  sprintf(filename, "%s/NEWS.SPOOL", cfg_emaildir);
  dp = opendir(filename);
  while (d = readdir(dp)) {
    strcpy(linebuf, "");
    sprintf(filename, "%s/NEWS.SPOOL/%s", cfg_emaildir, d->d_name);
    get_line(NULL, NULL, 0); // Reset get_line() state
    fp = fopen(filename, "r");
    if (!fp) {
      printf("Can't open %s\n", filename);
      closedir(dp);
      error_exit();
    }
    hdrs.emailnum = msg = atoi(&(d->d_name[5]));
    sprintf(filename, "%s/%s/EMAIL.%u", cfg_emaildir, mbox, msg);
    fputs(filename, stdout);
    _filetype = PRODOS_T_TXT;
    _auxtype = 0;
    destfp = fopen(filename, "wb");
    if (!destfp) {
      printf("Can't open %s\n", filename);
      closedir(dp);
      error_exit();
    }
    onkilllist = 0;
    headers = 1;
    headerchars = 0;
    hdrs.skipbytes = 0; // Just in case it doesn't get set
    hdrs.status = 'N';
    hdrs.tag = ' ';
    hdrs.date[0] = hdrs.from[0] = hdrs.cc[0] = hdrs.subject[0] = '\0';
    // Store News:newsgroup in TO field
    strcpy(linebuf, "News:");
    strcat(linebuf, newsgroup);
    copyheader(hdrs.to, linebuf, 79);
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
          if (sender_is_on_killlist(hdrs.from)) {
            fputs(" - KILLED!", stdout);
            onkilllist = 1;
            break;
          }
        }
        // Store Organization in CC field
        if (!strncmp(linebuf, "Organization: ", 14)) {
          copyheader(hdrs.cc, linebuf + 14, 79);
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
    if (onkilllist == 1)
      unlink(filename);
    else
      update_email_db(mbox, &hdrs);
    puts("");

    sprintf(filename, "%s/NEWS.SPOOL/NEWS.%u", cfg_emaildir, msg);
    if (unlink(filename))
      printf("Can't delete %s\n", filename);
  }
  closedir(dp);
}

void main(int argc, char *argv[]) {
  uint32_t nummsgs, lownum, highnum, msgnum, msg;
  char sendbuf[80];
  uint8_t eth_init = ETH_INIT_DEFAULT;

  if ((argc == 2) && (strcmp(argv[1], "EMAIL") == 0))
    exec_email_on_exit = 1;

  linebuf_pad[0] = 0;

  videomode(VIDEOMODE_80COL);
  printf("%c%s NNTP - Receive News Articles%c\n", 0x0f, PROGNAME, 0x0e);

  printf("\nReading NEWS.CFG             -");
  readconfigfile();
  printf(" Ok");
  
  printf("\nReading KILL.LIST.CFG        -");
  if (readkilllist() == 0)
    printf(" Ok");
  else
    printf(" None");
  
  printf("\nReading NEWSGROUPS.CFG       - ");
  sprintf(filename, "%s/NEWSGROUPS.CFG", cfg_emaildir);
  newsgroupsfp = fopen(filename, "r");
  if (!newsgroupsfp) {
    printf("\nCan't read %s\n", filename);
    error_exit();
  }

  printf("Ok\nCreating NEWSGROUPS.NEW      - ");
  sprintf(filename, "%s/NEWSGROUPS.NEW", cfg_emaildir);
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  newnewsgroupsfp = fopen(filename, "wb");
  if (!newnewsgroupsfp) {
    printf("\nCan't open %s\n", filename);
    error_exit();
  }

  {
    int file;
    printf("Ok\nSetting slot                 - ");
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

  printf("Ok\nConnecting to %s (%u) - ", cfg_server, nntp_port);

  if (!w5100_connect(parse_dotted_quad(cfg_server), nntp_port)) {
    printf("Fail\n");
    error_exit();
  }

  printf("Ok\n\n");

  if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DONT_SEND, CMD_MODE)) {
    error_exit();
  }
  if (expect(buf, "20")) // "200" if posting is allowed / "201" if no posting
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

  while (1) {
    msg = fscanf(newsgroupsfp, "%s %s %ld", newsgroup, mailbox, &msgnum);
    if (strcmp(newsgroup, "0") == 0)
      break;
    if ((msg == 0) || (msg == EOF))
      break;
    printf("*************************************************************\n");
    printf("* NEWSGROUP: %s\n", newsgroup);
    printf("* MAILBOX:   %s\n", mailbox);
    printf("* START MSG: %ld\n", msgnum);
    printf("*************************************************************\n");

    sprintf(sendbuf, "GROUP %s\r\n", newsgroup);
    if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
      error_exit();
    }
    if (strncmp(buf, "411", 3) == 0) {
      putchar(BELL);
      printf("** Non-existent newsgroup %s\n", newsgroup);
      continue;
    }

    sscanf(buf, "211 %lu %lu %lu", &nummsgs, &lownum, &highnum);
    printf(" Approx. %lu messages, numbered from %lu to %lu\n", nummsgs, lownum, highnum);

    if (msgnum == 0)
      msgnum = highnum - 100; // If 0 is specified grab 100 messages to start

    if (msgnum < lownum)
      msgnum = lownum;

    for (msg = msgnum; msg <= highnum; ++msg) {
      sprintf(sendbuf, "STAT %ld\r\n", msgnum);
      if (!w5100_tcp_send_recv(sendbuf, buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      if (strncmp(buf, "220", 3)) // Message number exists
        break;
    }

    while (1) {
      if (!w5100_tcp_send_recv("NEXT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE)) {
        error_exit();
      }
      if (strncmp(buf, "223", 3) != 0)
        break; // No more messages in group
      sscanf(buf, "223 %ld", &msg);
      sprintf(filename, "%s/NEWS.SPOOL/NEWS.%lu", cfg_emaildir, msg);
      _filetype = PRODOS_T_TXT;
      _auxtype = 0;
      fp = fopen(filename, "wb"); 
      if (!fp) {
        printf("Can't create %s\n", filename);
        error_exit();
      }
      printf("\n** Retrieving article %lu/%lu from %s\n", msg, highnum, newsgroup);
      if (!w5100_tcp_send_recv("ARTICLE\r\n", buf, NETBUFSZ, DO_SEND, DATA_MODE)) {
        error_exit();
      }
      spinner(filesize, 1); // Cleanup spinner
      fclose(fp);
    }
    printf("Updating NEWSGROUPS.NEW (%s:%ld) ...\n", newsgroup, msg);
    fprintf(newnewsgroupsfp, "%s %s %ld\n", newsgroup, mailbox, msg);
    printf("Updating mailbox %s ...\n", mailbox);
    update_mailbox(mailbox);
  }

  fclose(newsgroupsfp);
  fclose(newnewsgroupsfp);

  sprintf(filename, "%s/NEWSGROUPS.CFG", cfg_emaildir);
  if (unlink(filename)) {
    printf("Can't delete %s\n", filename);
    error_exit();
  }
  sprintf(linebuf, "%s/NEWSGROUPS.NEW", cfg_emaildir);
  if (rename(linebuf, filename)) {
    printf("Can't rename %s to %s\n", linebuf, filename);
    error_exit();
  }

  // Ignore any error - can be a race condition where other side
  // disconnects too fast and we get an error
  w5100_tcp_send_recv("QUIT\r\n", buf, NETBUFSZ, DO_SEND, CMD_MODE);

  printf("Disconnecting\n");
  w5100_disconnect();

  confirm_exit();
}
