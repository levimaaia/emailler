/////////////////////////////////////////////////////////////////
// EMAIL_COMMON.H
// Definitions shared between pop65.c and email.c
// Bobbi June 2020
/////////////////////////////////////////////////////////////////

#include <stdint.h>

#define PROGNAME "emai//er v2.1.6"

// Configuration params from EMAIL.CFG
char cfg_server[40];         // IP of POP3 server
char cfg_user[40];           // POP3 username
char cfg_pass[40];           // POP3 password
char cfg_pop_delete[40];     // If 'DELETE', delete message from POP3
char cfg_smtp_server[40];    // IP of SMTP server
char cfg_smtp_domain[40];    // Our domain
char cfg_instdir[80];        // ProDOS directory where apps are installed
char cfg_emaildir[80];       // ProDOS directory at root of email tree
char cfg_emailaddr[80];      // Our email address

// Represents the email headers for one message
struct emailhdrs {
  uint16_t emailnum;         // Name of file is EMAIL.n (n=emailnum)
  char     status;           // 'N' new, 'R' read, 'D' deleted
  char     tag;              // Used to maintain tag status in email.c
  uint16_t skipbytes;        // How many bytes to skip over the headers
  char     date[40];
  char     from[80];
  char     to[80];
  char     cc[80];
  char     subject[80];
#ifdef EMAIL_C
  // The following fields are present in memory in email.c only, not on disk
  struct emailhdrs *next;    // Used in email.c only for linked list
#endif
};

#ifdef EMAIL_C
#define EMAILHDRS_SZ_ON_DISK (sizeof(struct emailhdrs) - 2)
#endif

