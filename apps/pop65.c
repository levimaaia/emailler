///////////////////////////////////////////
//
// POP3 Client
// Bobbi June 2020
//
// https://www.ietf.org/rfc/rfc1939.txt
//
///////////////////////////////////////////

#include <cc65.h>
#include <fcntl.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "../inc/ip65.h"

#define FILENAME "/H1/DOCUMENTS/EMAIL.TXT"

char expected[80] = "";
volatile uint8_t await_response = 0;
volatile uint8_t got_expected = 0;
volatile uint8_t data_mode = 0;  // Set to 1 when receiving body of email

unsigned char buf[1500];
int len;
FILE *fp;

// Configuration params from POP65.CFG
char cfg_server[80];
char cfg_user[80];
char cfg_pass[80];

void error_exit(void)
{
  printf("- %s\n", ip65_strerror(ip65_error));
  exit(EXIT_FAILURE);
}

void confirm_exit(void)
{
  printf("\nPress any key ");
  cgetc();
}

/*
 * Handle an incoming TCP message
 */
void __fastcall__ tcpcb(const uint8_t *tcp_buf, int16_t tcp_len) {
  int idx;
  await_response = 0;
  len = tcp_len;
  if (len != -1) {
    if (data_mode) {
//    printf("%d ", len);
      memcpy(buf, tcp_buf, len);
      buf[len] = 0;
      for (idx = 0; idx < len; ++idx)
        if (buf[idx] != '\r')
          putchar(buf[idx]);
      fwrite(buf, 1, len, fp);
      idx = strstr(buf, "\r\n.\r\n"); // CRLF.CRLF
      if (idx) {
        puts("Found end");
        data_mode = 0; // If found, turn off data_mode
        fclose(fp);
      }
      ip65_process();
      return;
    }
    memcpy(buf, tcp_buf, len);
    buf[len-1] = 0; // HACK TO REMOVE DUPLICATE CR/LF
    printf("<%s", buf);
    if (strlen(expected) > 0) {
#ifdef DEBUG
      printf("Expected:%s", expected);
#endif
      if (strncmp(buf, expected, strlen(expected)) == 0) {
        got_expected = 1;
#ifdef DEBUG
        printf(" match\n");
#endif
      } else {
        got_expected = 0;
#ifdef DEBUG
        printf(" NO match\n");
#endif
      }
    } else
      got_expected = 1;
  }
}

/*
 * Check gotexpected flag and quit if false
 */
void checkgotexpected(void) {
  int i;
  while (await_response == 1) {
#ifdef DEBUG
    putchar('.');
#endif
    ip65_process();
    for (i = 0; i < 20000; ++i);
  }
  if (!got_expected) {
    printf("Didn't get expected response\n");
    error_exit();
  } else
    putchar('.');
}

/*
 * Send a TCP message
 * msg - string to send
 * expect - sting to expect (or empty string)
 */
void sendmessage(char *msg, char *expect) {
  strcpy(expected, expect);
  if (strncmp(msg, "PASS", 4) == 0)
    printf(">PASS ****\n");
  else
    printf(">%s", msg);
  if (tcp_send(msg, strlen(msg))) {
    error_exit();
  }
  await_response = 1;
  ip65_process();
}

void readconfigfile(void) {
    fp = fopen("POP65.CFG", "r");
    if (!fp) {
      puts("Can't open config file POP65.CFG");
      error_exit();
    }
    fscanf(fp, "%s", cfg_server);
    fscanf(fp, "%s", cfg_user);
    fscanf(fp, "%s", cfg_pass);
    fclose(fp);
}

int main(void)
{
  uint8_t eth_init = ETH_INIT_DEFAULT;
  char sendbuf[80];
  uint16_t msg, nummsgs;
  uint32_t bytes;

  if (doesclrscrafterexit())
  {
    atexit(confirm_exit);
  }

  videomode(VIDEOMODE_80COL);
  printf("\nReading POP65.CFG            -");
  readconfigfile();
  printf(" Ok");

#ifdef __APPLE2__
  {
    int file;

    printf("\nSetting slot                 -");
    file = open("ethernet.slot", O_RDONLY);
    if (file != -1)
    {
      read(file, &eth_init, 1);
      close(file);
      eth_init &= ~'0';
    }
    printf(" %d\n", eth_init);
  }
#endif

  printf("Initializing                 -");
  if (ip65_init(eth_init))
  {
    error_exit();
  }

  printf(" Ok\nObtaining IP address         -");
  if (dhcp_init())
  {
    error_exit();
  }

  printf(" Ok\nConnecting to %s   -", cfg_server);
  if (tcp_connect(parse_dotted_quad(cfg_server), 110, tcpcb)) {
    error_exit();
  }

  printf(" Ok\n\n");
  strcpy(expected, "+OK");
  ip65_process();
  await_response = 1;
  checkgotexpected();
  sprintf(sendbuf, "USER %s\n", cfg_user);
  sendmessage(sendbuf, "+OK");
  ip65_process();
  checkgotexpected();
  sprintf(sendbuf, "PASS %s\n", cfg_pass);
  sendmessage(sendbuf, "+OK Logged in.");
  ip65_process();
  checkgotexpected();
  sendmessage("STAT\n", "+OK");
  ip65_process();
  checkgotexpected();
  sscanf(buf, "+OK %u %lu", &nummsgs, &bytes);
  printf(" %u message(s), %lu total bytes\n", nummsgs, bytes);
  for (msg = 1; msg <= nummsgs; ++msg) {
    data_mode = 1;
    remove(FILENAME);
    fp = fopen(FILENAME, "wb");
    if (!fp) {
      printf("Can't open %s\n", FILENAME);
      error_exit();
    }
    sprintf(sendbuf, "RETR %d\n", msg);
    sendmessage(sendbuf, "");
    do {
      ip65_process();
    } while (data_mode == 1);
  }
  sendmessage("QUIT\n", "+OK Logging out.");
  ip65_process();
  checkgotexpected();

  printf(" Ok\nClosing connection           -");
  if (tcp_close()) {
    error_exit();
  }
  printf(" Ok\n\n");

  return EXIT_SUCCESS;
}
