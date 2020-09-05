///////////////////////////////////////////

// https://www.epochconverter.com/timezones

#define NTP_SERVER "pool.ntp.org"

///////////////////////////////////////////

#include <cc65.h>
#include <time.h>
#include <fcntl.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "email_common.h"

#include "../inc/ip65.h"

/*
 * Represents a date and time
 */
struct datetime {
    unsigned int  year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char ispd25format;
    unsigned char nodatetime;
};

unsigned char months[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 30, 31};

char nondst_tz_code[4];
int  nondst_tz_secs;
char dst_tz_code[4];
int  dst_tz_secs;
uint8_t exec_email_on_exit = 0;

/*
 * Add an hour
 */
void add_hour(struct datetime *dt) {
    ++dt->hour;
    if (dt->hour == 24) {
        dt->hour = 0;
        ++dt->day;
        if (dt->day > months[dt->month]) {
            dt->day = 1;
            ++dt->month;
            if (dt->month == 13) {
                dt->month = 1;
                ++dt->year;
            }
        }
    }
}

/*
 * Is year a leap year?
 */
unsigned char isleap(unsigned int year) {
    if ((year % 4) == 0)
        if ((year % 100) == 0)
            if ((year % 400) == 0)
                return 1;
            else
                return 0;
        else
            return 1;
    else
        return 0;
}

/*
 * Return day of year
 */
unsigned int dayofyear(struct datetime *dt) {
    unsigned char m;
    unsigned int count = 0;
    months[2] = isleap(dt->year) ? 29 : 28;
    for (m = 1; m < dt->month; ++m)
        count += months[m];
    count += dt->day;
    return count;
}

/*
 * Is DST in effect?
 * dt is the date and time, dow is day of week (0 = Mon, 6 = Sun)
 * DST starts at 02:00 on the second Sunday in March
 * and sends at 02:00 on the first Sunday in November
 * NOTE: The datetime passed in is in non-DST time.
 */
unsigned char isDST(struct datetime *dt, unsigned char dow) {
    unsigned int today_doy = dayofyear(dt);
    unsigned char dow_jan1 = (dow + 7 - (today_doy - 1) % 7) % 7;
    unsigned char dow_mar1 = (dow_jan1 + months[1] + months[2]) % 7;
    unsigned char date_mar = (6 - dow_mar1) + 8;
    unsigned char dow_nov1 =
      (dow_jan1 + months[1] + months[2] + months[3] + months[4] +
       months[5] + months[6] + months[7] + months[8] + months[9] +
       months[10]) % 7;
    unsigned char date_nov = (6 - dow_nov1) + 1;

#if 0
    printf("today_doy %u\n", today_doy);
    printf("dow_jan1 %u\n", dow_jan1);
    printf("dow_mar1 %u\n", dow_mar1);
    printf("date_mar %u\n", date_mar);
    printf("dow_nov1 %u\n", dow_nov1);
    printf("date_nov %u\n", date_nov);
#endif

    printf("  DST begins on Mar %u, %u\n  and ends on Nov %u, %u\n\n",
           date_mar, dt->year, date_nov, dt->year);

    switch(dt->month) {
    case 12:
    case 1:
    case 2:
        return 0; // No DST in the winter
    case 3:
        if (dt->day < date_mar)
            return 0;
        if (dt->day > date_mar)
            return 1;
        if (dt->day == date_mar)
            return (dt->hour < 2) ? 0 : 1;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
        return 1; // DST in the summer
    case 11:
        if (dt->day < date_nov)
            return 1;
        if (dt->day > date_nov)
            return 0;
        if (dt->day == date_nov)
            return (dt->hour < 3) ? 1 : 0; // 3 because we use non-DST time
    }
}

/*
 * Parse mtime or ctime fields and populate the fields of the datetime struct
 * Supports the legacy ProDOS date/time format as used by ProDOS 1.0->2.4.0
 * and also the new format introduced with ProDOS 2.5.
 */
void readdatetime(unsigned char time[4], struct datetime *dt) {
    unsigned int d = time[0] + 256U * time[1];
    unsigned int t = time[2] + 256U * time[3];
    if ((d == 0) && (t == 0)) {
        dt->nodatetime = 1;
        return;
    }
    dt->nodatetime = 0;
    if (!(t & 0xe000)) {
        /* ProDOS 1.0 to 2.4.2 date format */
        dt->year   = (d & 0xfe00) >> 9;
        dt->month  = (d & 0x01e0) >> 5;
        dt->day    = d & 0x001f;
        dt->hour   = (t & 0x1f00) >> 8;
        dt->minute = t & 0x003f;
        dt->ispd25format = 0;
        if (dt->year < 40) /* See ProDOS-8 Tech Note 48 */
            dt->year += 2000;
        else
            dt->year += 1900;
    } else {
        /* ProDOS 2.5.0+ */
        dt->year   = t & 0x0fff;
        dt->month  = ((t & 0xf000) >> 12) - 1;
        dt->day    = (d & 0xf800) >> 11;
        dt->hour   = (d & 0x07c0) >> 6; 
        dt->minute = d & 0x003f;
        dt->ispd25format = 1;
    }
}

/*
 * Write the date and time stored in struct datetime in ProDOS on disk format,
 * storing the bytes in array time[].  Supports both legacy format
 * (ProDOS 1.0-2.4.2) and the new date and time format introduced
 * with ProDOS 2.5
 */
void writedatetime(struct datetime *dt, unsigned char time[4]) {
    unsigned int d, t;
    if (dt->nodatetime == 1) {
        time[0] = time[1] = time[2] = time[3] = 0;
        return;
    }
    if (dt->ispd25format == 0) {
        /* ProDOS 1.0 to 2.4.2 date format */
        unsigned int year = dt->year;
        if (year > 2039)        /* 2039 is last year */
            year = 2039;
        if (year < 1940)        /* 1940 is first year */
            year = 1940;
        if (year >= 2000)
            year -= 2000;
        if (year >= 1900)
            year -= 1900;
        d = (year << 9) | (dt->month << 5) | dt->day; 
        t = (dt->hour << 8) | dt->minute;
    } else {
        /* ProDOS 2.5.0+ */
        t = ((dt->month + 1) << 12) | dt->year;
        d = (dt->day << 11) | (dt->hour << 6) | dt->minute; 
    }
    time[0] = d & 0xff;
    time[1] = (d >> 8) & 0xff;
    time[2] = t & 0xff;
    time[3] = (t >> 8) & 0xff;
}

/*
 * Print date/time value for directory listing
 */
void printdatetime(struct datetime *dt) {
    if (dt->nodatetime)
        fputs("---------- --:--", stderr);
    else {
        if (dt->ispd25format)
            revers(1);
        printf("%04d-%02d-%02d %02d:%02d",
               dt->year, dt->month, dt->day, dt->hour, dt->minute);
        revers(0);
    }
}

void error_exit(void)
{
  printf("- %s\n", ip65_strerror(ip65_error));
  exit(EXIT_FAILURE);
}

void confirm_exit(void)
{
  printf("\n[Press Any Key]");
  cgetc();
  if (exec_email_on_exit) {
    exec("EMAIL.SYSTEM", NULL); // Assuming it is in current working dir
  }
}

void printsystemdate(void)
{
  unsigned char *p;
  unsigned char timebytes[4];
  struct datetime dt;
  p = (unsigned char*)0xbf90;
  timebytes[0] = *p;
  p = (unsigned char*)0xbf91;
  timebytes[1] = *p;
  p = (unsigned char*)0xbf92;
  timebytes[2] = *p;
  p = (unsigned char*)0xbf93;
  timebytes[3] = *p;

  readdatetime(timebytes, &dt);
  printf("ProDOS date & time: ");
  printdatetime(&dt);
}

void readtimezonefile(void) {
    FILE *fp = fopen("TZONE.TXT", "r");
    if (!fp) {
        strcpy(nondst_tz_code, "EST");
        nondst_tz_secs = -18000;
        strcpy(dst_tz_code, "EDT");
        dst_tz_secs = -14400;
    }
    fscanf(fp, "%d,%s", &nondst_tz_secs, nondst_tz_code);
    fscanf(fp, "%d,%s", &dst_tz_secs, dst_tz_code);
    nondst_tz_code[3] = dst_tz_code[3] = 0;
    fclose(fp); 
}

int main(int argc, char *argv[])
{
  uint8_t eth_init = ETH_INIT_DEFAULT;
  uint32_t server;
  struct timespec time;
  struct datetime dt;
  unsigned char timebytes[4];
  char datestr[30]; // Should be long enough
  unsigned char *p;
  unsigned char dow, dst;

  if ((argc == 2) && (strcmp(argv[1], "EMAIL") == 0))
    exec_email_on_exit = 1;

  if (exec_email_on_exit) {
    videomode(VIDEOMODE_80COL);
    printf("%c%s NTP%c\n\n", 0x0f, PROGNAME, 0x0e);
  }

  printsystemdate();

  if (doesclrscrafterexit())
  {
    atexit(confirm_exit);
  }

  printf("\n\nRead timezone from TZONE.TXT -");
  readtimezonefile();
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

  printf(" Ok\nResolving %s       -", NTP_SERVER);
  server = dns_resolve(NTP_SERVER);
  if (!server)
  {
    error_exit();
  }

  // Assume non-DST TZ initially
  strncpy(_tz.tzname, nondst_tz_code, sizeof(_tz.tzname) - 1);
  _tz.timezone = nondst_tz_secs;

  printf(" Ok\nGetting date and time        -");
  time.tv_sec = sntp_get_time(server);
  if (!time.tv_sec)
  {
    error_exit();
  }
  printf(" Ok\n\n");

  // Convert time from seconds since 1900 to
  // seconds since 1970 according to RFC 868
  time.tv_sec -= 2208988800UL;

  strcpy(datestr, ctime(&time.tv_sec));

  // Format is:
  // 012345678901234567890123456789
  // Sun Jun 21 00:44:54 2020
  dt.year = atoi(datestr+20);
  datestr[16] = 0;
  dt.minute = atoi(datestr+14);
  datestr[13] = 0;
  dt.hour = atoi(datestr+11);
  datestr[10] = 0;
  dt.day = atoi(datestr+7);
  datestr[7] = 0;

  if (!strcmp(datestr+4, "Jan"))
    dt.month = 1;
  else if (!strcmp(datestr+4, "Feb"))
    dt.month = 2;
  else if (!strcmp(datestr+4, "Mar"))
    dt.month = 3;
  else if (!strcmp(datestr+4, "Apr"))
    dt.month = 4;
  else if (!strcmp(datestr+4, "May"))
    dt.month = 5;
  else if (!strcmp(datestr+4, "Jun"))
    dt.month = 6;
  else if (!strcmp(datestr+4, "Jul"))
    dt.month = 7;
  else if (!strcmp(datestr+4, "Aug"))
    dt.month = 8;
  else if (!strcmp(datestr+4, "Sep"))
    dt.month = 9;
  else if (!strcmp(datestr+4, "Oct"))
    dt.month = 10;
  else if (!strcmp(datestr+4, "Nov"))
    dt.month = 11;
  else if (!strcmp(datestr+4, "Dec"))
    dt.month = 12;
  else {
    printf("\nWhat kind of month is %s?\n", datestr+4);
    error_exit();
  }

  datestr[3] = 0;
  if (!strcmp(datestr, "Mon"))
     dow = 0;
  if (!strcmp(datestr, "Tue"))
     dow = 1;
  if (!strcmp(datestr, "Wed"))
     dow = 2;
  if (!strcmp(datestr, "Thu"))
     dow = 3;
  if (!strcmp(datestr, "Fri"))
     dow = 4;
  if (!strcmp(datestr, "Sat"))
     dow = 5;
  if (!strcmp(datestr, "Sun"))
     dow = 6;

  p = (unsigned char*)0xbfff;
  if (*p == 0x25)
      dt.ispd25format = 1;
  else
      dt.ispd25format = 0;
  dt.nodatetime = 0;

  dst = isDST(&dt, dow);

#if 0
  printdatetime(&dt);
#endif

  // Now we know if it is DST or not, we can set the timezone
  strncpy(_tz.tzname,
          dst ? dst_tz_code : nondst_tz_code,
          sizeof(_tz.tzname) - 1);
  _tz.timezone = dst ? dst_tz_secs : nondst_tz_secs;

  strcpy(datestr, ctime(&time.tv_sec));
  datestr[24] = 0; // Remove carriage return
  printf("%s", datestr);
  if (dst) {
      add_hour(&dt); // Spring forward!
      printf(" (%s)", dst ? dst_tz_code : nondst_tz_code);
  }

  p = (unsigned char*)0xbf98;
  if (*p & 0x01) {
     puts("\n\nPlease update your real-time clock.");
     puts("It will overwrite this date/time!!");
  }
  writedatetime(&dt, timebytes);
  // Write the date/time info into $bf90-$bf93
  p = (unsigned char*)0xbf8e;
  *p = 0;
  p = (unsigned char*)0xbf8f;
  *p = 0;
  p = (unsigned char*)0xbf90;
  *p = timebytes[0];
  p = (unsigned char*)0xbf91;
  *p = timebytes[1];
  p = (unsigned char*)0xbf92;
  *p = timebytes[2];
  p = (unsigned char*)0xbf93;
  *p = timebytes[3];
  putchar('\n');
  printsystemdate();
  putchar('\n');

  //time.tv_nsec = 0;
  //clock_settime(CLOCK_REALTIME, &time);

  return EXIT_SUCCESS;
}
