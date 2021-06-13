/////////////////////////////////////////////////////////////////
// PRINT65
// Print files over the network to an HP Jetdirect printer
// Bobbi June 2021
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

static uint8_t  exec_email_on_exit = 0;
static char     filename[256];
static FILE     *fp;
static uint32_t filesize;
static uint16_t jetdirect_port;

/*
 * Keypress before quit
 */
void confirm_exit(void) {
  printf("\n[Press Any Key]");
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

// Read file handle fp and send message over TCP
bool w5100_tcp_send() {

  //
  // Handle sending of email body
  //
  uint16_t pos = 0;
  uint8_t  cont = 1;
  uint16_t snd;
  uint16_t len;

  filesize = 0;
  len = get_line(fp, 1, linebuf, LINEBUFSZ); // Reset buffer

  while (cont) { 

    len = get_line(fp, 0, linebuf, LINEBUFSZ - 1);
    pos = 0;

    if (len == 0) {
      strcpy(linebuf, "\r\n");
      len = 2;
      cont = 0;
    } else {
      linebuf[len++] = '\n'; // CR -> CRLF
      linebuf[len] = '\0';
      filesize += len;
    }

    while (len) {
      if (input_check_for_abort_key()) {
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
  return true;
}

/*
 * Read parms from PRINT.CFG
 */
void readconfigfile(void) {
  char *colon;
  fp = fopen("PRINT.CFG", "r");
  if (!fp) {
    puts("Can't open config file PRINT.CFG");
    error_exit();
  }
  fscanf(fp, "%s", cfg_server);
  fclose(fp);

  colon = strchr(cfg_server, ':');
  if (!colon)
    jetdirect_port = 9100;
  else {
    jetdirect_port = atoi(colon + 1);
    *colon = '\0';
  }
}

void main(int argc, char *argv[]) {
  uint8_t eth_init = ETH_INIT_DEFAULT, connected = 0;

  videomode(VIDEOMODE_80COL);
  printf("%c%s PRINT%c\n", 0x0f, PROGNAME, 0x0e);

  puts("\nThis utility allows printing to a network-connected printer");
  puts("using the HP Jetdirect protocol.\n");
  if (argc == 2) {
    strcpy(filename, argv[1]);
  } else {
    printf("\nFilename to print >");
    scanf("%s", filename);
    puts("");
  }

  readconfigfile();

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

  fp = fopen(filename, "rb");
  if (!fp) {
      printf("Can't open %s\n", filename);
      error_exit();
  }

  if (!connected) {
    printf("\nConnecting to %s:%d - ", cfg_server, jetdirect_port);

    if (!w5100_connect(parse_dotted_quad(cfg_server), jetdirect_port)) {
      printf("Fail\n");
      error_exit();
    }

    printf("Ok\n\n");

  }
  if (!w5100_tcp_send()) {
      error_exit();
  }
  fclose(fp);
  printf("Disconnecting\n");
  w5100_disconnect();

  confirm_exit();
}

