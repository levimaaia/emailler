////////////////////////////////////////////////////////////////////////////
// Apple //e Enhanced text editor
// Supports Apple //e Auxiliary memory and RamWorks style expansions
// Bobbi July-Sept 2020
/////////////////////////////////////////////////////////////////////////////
//
// See GitHub for open issues!!!
//
/////////////////////////////////////////////////////////////////////////////
// Note: Use my fork of cc65 to get a flashing cursor!!
/////////////////////////////////////////////////////////////////////////////

#define AUXMEM               // No longer builds without AUXMEM

#include <conio.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NCOLS      80        // Width of editing screen
#define NROWS      22        // Height of editing screen
#define CURSORROW  10        // Row cursor initially shown on (if enough text)
#define PROMPT_ROW NROWS + 1 // Row where input prompt is shown
#define IOSZ       1024      // Size of chunks to use when loading/saving file
#define CUTBUFSZ   8192      // Size of cut buffer. Must be >IOSZ.

#define EOL       '\r'       // For ProDOS

#define BELL       0x07
#define BACKSPACE  0x08
#define RETURN     0x0d
#define ESC        0x1b
#define DELETE     0x7f

#ifdef AUXMEM
#define BUFSZ 0xb7fe             // Aux from 0x800 to 0xbfff, minus a pad byte
char     iobuf[CUTBUFSZ];        // Buffer for disk I/O and cut/paste
uint8_t  banktbl[1+8*16];        // Handles up to 8MB. Map of banks.
uint8_t  auxbank = 0;            // Currently selected aux bank (physical)
uint8_t  l_auxbank = 1;          // Currently selected aux bank (logical)
uint16_t cutbuflen = 0;          // Length of data in cut buffer
#else
#define BUFSZ (20480 - 1024)     // 19KB
char     gapbuf[BUFSZ];
char     padding = 0;            // To null terminate for strstr()
#endif

// The following fields, plus the gap buffer, represent the state of
// the current buffer.  These are stashed in each aux page from 0x200 up.
// Total 80 + 3 + 2 + 2 + 1 + 2 + 2 = 92 bytes
#define BUFINFOSZ 92
char     filename[80]  = "";
uint8_t  status[3] = {0, 0, 0};  // status[0] is 1 if file has been modified
                                 // status[1] is 1 if should warn for overwrite
                                 // status[2] is part # for multi-part files
uint16_t gapbegin      = 0;
uint16_t gapend        = BUFSZ - 1;
uint8_t  canundo       = 0;          // For OA-Z "Undo"
uint16_t oldgapbegin   = 0;          // ditto
uint16_t oldgapend     = BUFSZ - 1;  // ditto

char    userentry[82] = "";  // Couple extra chars so we can store 80 col line
char    search[80]    = "";
char    replace[80]   = "";
char    startdir[80]  = "";

uint8_t rowlen[NROWS];       // Number of chars on each row of screen

// Interface to read_char_update_pos()
uint8_t  do_print;
uint16_t pos = 0, startsel = 65535U, endsel = 65535U;
uint8_t  row, col;

uint8_t cursrow, curscol; // Cursor position is kept here by draw_screen()

uint8_t  quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit

enum editmode {SEL_NONE, SEL_SELECT, SRCH1, SRCH2, SRCH3};
enum editmode mode;

// Mousetext Open-Apple
char openapple[] = "\x0f\x1b""A\x18\x0e";

// Mousetext Closed-Apple
char closedapple[] = "\x0f\x1b""@\x18\x0e";

char openfilemsg[] = 
" Select file from tree browser, or [Tab] to enter filename. [Esc] cancels.";

char namefilemsg[] = 
" Enter a new filename to create file. Select an existing file to overwrite it.";

/*
 * Return number of bytes of freespace in gapbuf
 */
#define FREESPACE() (gapend - gapbegin + 1)

/*
 * Return number of bytes of data in gapbuf
 */
#define DATASIZE() (gapbegin + (BUFSZ - 1 - gapend))

/*
 * Obtain current position in gapbuf
 * This is the positon where the next character will be inserted
 */
#define GETPOS()    (gapbegin)

/*
 * Obtain one byte from the gapbuf[] in aux memory
 * Must be in LC
 * i - Index into gapbuf[]
 * Returns value at gapbuf[i]
 */
#pragma code-name (push, "LC")
char get_gapbuf(uint16_t i) {
#ifdef AUXMEM
  *(uint16_t*)(0xfe) = (uint16_t)0x0800 + i; // Stuff address in Zero Page
  __asm__("lda %v", auxbank); 
  __asm__("sta $c073");  // Set aux bank
  __asm__("sta $c003");  // Read aux mem
  __asm__("lda ($fe)");
  __asm__("sta $ff");
  __asm__("sta $c002");  // Read main mem
  __asm__("lda #$00");
  __asm__("sta $c073");  // Set aux bank back to 0
  return *(uint8_t*)(0xff);
#else
  return gapbuf[i];
#endif
}
#pragma code-name (pop)

/*
 * Set one byte in the gapbuf[] in aux memory
 * Must be in LC
 * i - Index into gapbuf
 * c - Byte value to set
 */
#pragma code-name (push, "LC")
void set_gapbuf(uint16_t i, char c) {
#ifdef AUXMEM
  __asm__("lda %v", auxbank); 
  __asm__("sta $c073");  // Set aux bank
  __asm__("sta $c005"); // Write aux mem
  (*(char*)((char*)0x0800 + i)) = c;
  __asm__("sta $c004"); // Write main mem
  __asm__("lda #$00");
  __asm__("sta $c073");  // Set aux bank back to 0
#else
  gapbuf[i] = c;
#endif
}
#pragma code-name (pop)

/*
 * Do a memmove() on aux memory. Uses indices into gapbuf[].
 * Must be in LC
 * dst - Destination index into gapbuf[]
 * src - Source index into gapbuf[]
* n - Length in bytes
 */
#pragma code-name (push, "LC")
void move_in_gapbuf(uint16_t dst, uint16_t src, size_t n) {
#ifdef AUXMEM
  if (dst > src) {
    // Start with highest addr and copy downwards
    *(uint16_t*)(0xfa) = n;                              // Stuff sz in ZP
    *(uint16_t*)(0xfc) = (uint16_t)0x0800 + src - 1; // Stuff src in ZP
    *(uint16_t*)(0xfe) = (uint16_t)0x0800 + dst - 1; // Stuff dst in ZP
#ifdef AUXMEM
    __asm__("lda %v", auxbank); 
    __asm__("sta $c073");  // Set aux bank
    __asm__("sta $c005"); // Write aux mem
    __asm__("sta $c003"); // Read aux mem
#endif
    __asm__("clc");          // Prepare the source pointer
    __asm__("lda $fd");      // MSB of source
    __asm__("adc $fb");      // MSB of n
    __asm__("sta $fd");

    __asm__("clc");          // Prepare the dest pointer
    __asm__("lda $ff");      // MSB of dest
    __asm__("adc $fb");      // MSB of n
    __asm__("sta $ff");

    __asm__("ldy $fa");      // LSB of n
dl1:
    __asm__("lda ($fc),y");
    __asm__("sta ($fe),y");
    __asm__("cpy #$00");     // Copy leftover bytes
    __asm__("beq %g", ds1);
    __asm__("dey");
    __asm__("jmp %g", dl1);
ds1:
    __asm__("ldx #$00");
dl2:
    __asm__("cpx $fb");      // MSB of n
    __asm__("beq %g", ds2);  // No more complete 256 byte blocks, done!
    __asm__("dec $fd");      // MSB of source
    __asm__("dec $ff");      // MSB of dest
    __asm__("ldy #$00");     // Copy one block of 256 bytes
dl3:
    __asm__("dey");
    __asm__("lda ($fc),y");
    __asm__("sta ($fe),y");
    __asm__("cpy #$00");
    __asm__("bne %g", dl3);
    __asm__("inx");
    __asm__("jmp %g", dl2);
ds2:
#ifdef AUXMEM
    __asm__("sta $c002"); // Read main mem
    __asm__("sta $c004"); // Write main mem
    __asm__("lda #$00");
    __asm__("sta $c073"); // Set aux bank back to 0
#endif
  } else {
    // Start with lowest addr and copy upwards
    *(uint16_t*)(0xfa) = n;                              // Stuff sz in ZP
    *(uint16_t*)(0xfc) = (uint16_t)0x0800 + src;         // Stuff src in ZP
    *(uint16_t*)(0xfe) = (uint16_t)0x0800 + dst;         // Stuff dst in ZP
#ifdef AUXMEM
    __asm__("lda %v", auxbank); 
    __asm__("sta $c073");  // Set aux bank
    __asm__("sta $c005"); // Write aux mem
    __asm__("sta $c003"); // Read aux mem
#endif
    __asm__("ldx #$00");
al1:
    __asm__("cpx $fb");      // MSB of n
    __asm__("beq %g", as1);  // No more complete 256 byte blocks

    __asm__("ldy #$00");     // Copy one block of 256 bytes
al2:
    __asm__("lda ($fc),y");
    __asm__("sta ($fe),y");
    __asm__("iny");
    __asm__("bne %g", al2);

    __asm__("inc $fd");     // MSB of source
    __asm__("inc $ff");     // MSB of dest
    __asm__("inx");
    __asm__("jmp %g", al1);
as1:
    __asm__("ldy #$00");    // Copy leftover bytes
al3:
    __asm__("cpy $fa");     // LSB of n
    __asm__("beq %g", as2); // Done!
    __asm__("lda ($fc),y");
    __asm__("sta ($fe),y");
    __asm__("iny");
    __asm__("jmp %g", al3);
as2:
#ifdef AUXMEM
    __asm__("sta $c002"); // Read main mem
    __asm__("sta $c004"); // Write main mem
    __asm__("lda #$00");
    __asm__("sta $c073"); // Set aux bank back to 0
#endif
  }
#else
  memmove(gapbuf + dst, gapbuf + src, n);
#endif
}
#pragma code-name (pop)

#pragma data-name (push, "LC")
char needle[80] = "";               // Must be in LC memory if using AUXMEM
#pragma data-name (pop)

/*
 * Do a strstr() on aux memory. Uses index into gapbuf[].
 * Must be in LC
 * i - Index into gapbuf[]
 * srch - String to search for
 * loc - Location where string found (index into gapbuf[]) returned here
 * Returns 1 if found, 0 otherwise
 */
#pragma code-name (push, "LC")
#pragma optimize (off)
uint8_t search_in_gapbuf(uint16_t i, char *srch, uint16_t *loc) {

  __asm__("lda $c083"); // Read and write LC RAM bank 2
  __asm__("lda $c083"); // Read and write LC RAM bank 2
  memcpy(needle, srch, 80);

  // ZP usage
  // $f7:     TMP1
  // $f8-$f9: PTR4 in original strstr.s code.
  // $fa-$fb: PTR1 in original strstr.s code.
  // $fc-$fd: PTR2 in original strstr.s code.
  // $fe-$ff: PTR3 in original strstr.s code.
  *(uint16_t*)(0xfc) = (uint16_t)needle;
  *(uint16_t*)(0xf8) = (uint16_t)needle; // Another copy of needle ptr

#ifdef AUXMEM
  *(uint16_t*)(0xfa) = (uint16_t)0x0800 + i; // Haystack pointer
  __asm__("lda %v", auxbank); 
  __asm__("sta $c073"); // Set aux bank
  __asm__("sta $c005"); // Write aux mem
  __asm__("sta $c003"); // Read aux mem
#else
  *(uint16_t*)(0xfa) = (uint16_t)gapbuf + i; // Haystack pointer
#endif

  __asm__("ldy #$00");
  __asm__("lda ($fc),y");      // Get first byte of needle
  __asm__("beq %g", found);    // Needle is empty --> we're done

// Search for beginning of string
  __asm__("sta $f7");          // Save start of needle
l1:
  __asm__("lda ($fa),y");      // Get next char from haystack
  __asm__("beq %g", notfound); // Jump if end
  __asm__("cmp $f7");          // Start of needle found?
  __asm__("beq %g", l2);       // Jump if so
  __asm__("iny");              // Next char
  __asm__("bne %g", l1);
  __asm__("inc $fb");          // Bump high byte of haystack
  __asm__("bne %g", l1);       // Branch always

// We found the start of needle in haystack
l2:
  __asm__("tya");              // Get offset
  __asm__("clc");
  __asm__("adc $fa");
  __asm__("sta $fa");          // Make ptr1 point to start
  __asm__("bcc %g", l3);
  __asm__("inc $fb");

// ptr1 points to the start of needle now. Setup temporary pointers for the
// search. The low byte of ptr4 is already set.
l3:
  __asm__("sta $fe");             // LSB PTR3
  __asm__("lda $fb");             // MSB PTR1
  __asm__("sta $ff");             // MSB PTR3
  __asm__("lda $fd");             // MSB PTR2
  __asm__("sta $f9");             // MSB PTR4
  __asm__("ldy #1");              // First char is identical, so start on second

// Do the compare
l4:
  __asm__("lda ($f8),y");         // Get char from needle
  __asm__("beq %g", found);       // Jump if end of needle (-> found)
  __asm__("cmp ($fe),y");         // Compare with haystack
  __asm__("bne %g", l5);          // Jump if not equal
  __asm__("iny");                 // Next char
  __asm__("bne %g", l4);
  __asm__("inc $ff");
  __asm__("inc $f9");             // Bump hi byte of pointers
  __asm__("bne %g", l4);          // Next char (branch always)

// The strings did not compare equal, search next start of needle
l5:
  __asm__("ldy #1");              // Start after this char
  __asm__("bne %g", l1);          // Branch always

// We found the needle
found:
#ifdef AUXMEM
  __asm__("sta $c002"); // Read main mem
  __asm__("sta $c004"); // Write main mem
  __asm__("lda #$00");
  __asm__("sta $c073"); // Set aux bank back to 0
  *loc = *(uint16_t*)0xfa - 0x0800;
#else
  *loc = *(uint16_t*)0xfa - (uint16_t)gapbuf;
#endif
  return 1;

// We reached end of haystack without finding needle
notfound:
#ifdef AUXMEM
  __asm__("sta $c002"); // Read main mem
  __asm__("sta $c004"); // Write main mem
  __asm__("lda #$00");
  __asm__("sta $c073"); // Set aux bank back to 0
#endif
  return 0;

//#else
//  char *p = strstr(gapbuf + i, needle);
//  if (p) {
//    *loc = p - gapbuf;
//    return 1;
//  } else
//    return 0;
//#endif
}
#pragma optimize (on)
#pragma code-name (pop)

/*
 * Annoying beep
 */
void beep(void) {
  uint8_t *p = (uint8_t*)0xc030; // Speaker
  uint8_t junk;
  uint16_t i;
  for (i = 0; i < 200; ++i) {
    junk = *p;
    for (junk = 0; junk < 50; ++junk); // Reduce pitch
  }
}

/*
 * Clear to EOL
 */
void clreol(void) {
  uint8_t x = wherex(), y = wherey();
  cclear(80 - x);
  gotoxy(x, y);
}

/*
 * Clear to EOL, wrap to next line
 */
void clreol_wrap(void) {
  uint8_t x = wherex();
  cclear(80 - x);
  gotox(x);
}

/*
 * Clear line
 */
void clrline(void) {
  uint8_t x = wherex();
  gotox(0);
  cclear(80);
  gotox(x);
}

/*
 * Put cursor at beginning of PROMPT_ROW
 */
void goto_prompt_row(void) {
  gotoxy(0, PROMPT_ROW);
}

/*
 * Refresh the status line at the bottom of the screen
 */
void update_status_line(void) {
  uint8_t nofile = 0;
  uint8_t l;

  static char selmsg1[] = ": Go to end of selection, then [Return]";
  static char selmsg2[] = ": Go to target, then [Return] to ";

  goto_prompt_row();

  if (strlen(filename) == 0) {
    strcpy(filename, "<scratch>");
    nofile = 1;
  }
  revers(1);
  switch (mode) {
  case SEL_NONE:
    if (status[2] == 0) {
      cprintf("OA-? Help | [%03u] %c File:%s  %2uKB free",
              l_auxbank, status[0] ? '*' : ' ', filename,
              (FREESPACE() + 512) / 1024);
      l = 44 - strlen(filename);
    } else {
      snprintf(userentry, 80, "%s Part:%u", filename, status[2]);
      cprintf("OA-? Help | [%03u] %c File:%s %2uKB free",
              l_auxbank, status[0] ? '*' : ' ', userentry,
              (FREESPACE() + 512) / 1024);
      l = 45 - strlen(userentry);
    } 
    break;
  case SEL_SELECT:
    cprintf("Select: OA-[Space] to end");
    l = 80 - 25;
    break;
  case SRCH1:
    cprintf("Searching ...");
    l = 80 - 13;
    break;
  case SRCH2:
    cprintf("Searching - wrapped ...");
    l = 80 - 23;
    break;
  case SRCH3:
    if (status[2] == 0) {
      cprintf("OA-? Help | [%03u] %c File:%s  %2uKB free | Not Found",
              l_auxbank, status[0] ? '*' : ' ', filename, (FREESPACE() + 512) / 1024);
      l = 44 - 12 - strlen(filename);
    } else {
      snprintf(userentry, 80, "%s Part:%u", filename, status[2]);
      cprintf("OA-? Help | [%03u] %c File:%s %2uKB free | Not Found",
              l_auxbank, status[0] ? '*' : ' ', userentry,
              (FREESPACE() + 512) / 1024);
      l = 45 - 12 - strlen(userentry);
    }
    break;
  }
  cclear(l);
  revers(0);

  if (nofile)
    strcpy(filename, "");

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Set modified status
 */
void set_modified(uint8_t mod) {
  if (status[0] == mod)
    return;
  status[0] = mod;
  update_status_line();
}

/*
 * Prompt for a name in the bottom line of the screen
 * Returns number of chars read.
 * prompt - Message to display before > prompt
 * is_file - if 1, restrict chars to those allowed in ProDOS filename
 * Returns number of chars read, or 255 if ESC pressed
 */
uint8_t prompt_for_name(char *prompt, uint8_t is_file) {
  uint16_t i;
  char c;
  cursor(0);
  goto_prompt_row();
  clreol();
  revers(1);
  cprintf("%s>", prompt);
  revers(0);
  gotox(2 + strlen(prompt));
  i = 0;
  while (1) {
    c = cgetc();
    if (is_file && !isalnum(c) && (c != RETURN) && (c != BACKSPACE) &&
        (c != DELETE) && (c != ESC) && (c != '.') && (c != '/')) {
      beep();
      continue;
    }
    switch (c) {
    case RETURN:
      goto done;
    case BACKSPACE:
    case DELETE:
      if (i > 0) {
        gotox(wherex() - 1);
        cputc(' ');
        gotox(wherex() - 1);
        --i;
      } else
        beep();
      break;
    case ESC:
      userentry[0] = '\0';
      i = 255;
      goto esc_pressed;
    default:
      cputc(c);
      userentry[i++] = c;
    }
    if (i == 79)
      goto done;
  }
done:
  userentry[i] = '\0';
esc_pressed:
  clrline();
  gotoxy(curscol, cursrow);
  cursor(1);
  return i;
}

/*
 * Prompt ok?
 * Returns 0 for yes
 *         1 for no
 *         2 for ESC
 */
char prompt_okay(char *msg) {
  char c;
  cursor(0);
  goto_prompt_row();
  clreol();
  revers(1);
  cprintf("%s (y/n/ESC)", msg);
  revers(0);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N') || c == ESC)
      break;
    beep();
  }
  if ((c == 'y') || (c == 'Y'))
    c = 0;
  else if (c == ESC)
    c = 2;
  else
    c = 1;
  update_status_line();
  return c;
}

/*
 * Error message
 */
void show_error(char *msg) {
  cursor(0);
  goto_prompt_row();
  clreol();
  beep();
  revers(1);
  cprintf("%s  [Press Any Key]", msg);
  revers(0);
  cgetc();
  update_status_line();
}

/*
 * Insert a character into gapbuf at current position
 * c - character to insert
 */
void insert_char(char c) {
  if (FREESPACE()) {
    set_gapbuf(gapbegin++, c);
    return;
  }
  beep();
}

/*
 * Delete the character to the left of the current position
 */
void delete_char(void) {
  if (gapbegin == 0) {
    beep();
    return;
  }
  --gapbegin;
}

/*
 * Delete the character to the right of the current position
 */
void delete_char_right(void) {
  if (gapend == BUFSZ - 1) {
    beep();
    return;
  }
  ++gapend;
}

/*
 * Move the current position
 * pos - position to which to move
 */
void jump_pos(uint16_t pos) {
  uint16_t l;
  if (pos > BUFSZ - 1) {
    beep();
    return;
  }
  if (pos == GETPOS())
    return;
  if (pos > GETPOS()) {
    l = pos - gapbegin;
    move_in_gapbuf(gapbegin, gapend + 1, l);
    gapbegin += l;
    gapend += l;
  } else {
    l = gapbegin - pos;
    move_in_gapbuf(gapend - l + 1, gapbegin - l, l);
    gapbegin -= l;
    gapend -= l;
  }
  if (mode == SEL_SELECT)
    endsel = gapbegin;
  return;
}

/*
 * Go to next tabstop
 */
uint8_t next_tabstop(uint8_t col) {
  return (col / 8) * 8 + 8;
}

#ifdef AUXMEM
#define FROMAUX 0
#define TOAUX   1
/*
 * Copy to/from aux memory using AUXMOVE
 */
void copyaux(char *src, char *dst, uint16_t len, uint8_t dir) {
    char **a1 = (char**)0x3c;
    char **a2 = (char**)0x3e;
    char **a4 = (char**)0x42;
    *a1 = src;
    *a2 = src + len - 1; // AUXMOVE moves length+1 bytes!!
    *a4 = dst;
    if (dir == TOAUX) {
        __asm__("sec");       // Copy main->aux
        __asm__("jsr $c311"); // AUXMOVE
    } else {
        __asm__("clc");       // Copy aux->main
        __asm__("jsr $c311"); // AUXMOVE
    }
}
#endif

/*
 * Translate logic aux bank number to physical.
 * This is necessary because there may be 'holes' in the banks
 * Logical banks are numbered 1..maxbank contiguously.
 * Physical banks start at 0 and may be non-contiguous.
 */
uint8_t bank_log_to_phys(uint8_t l) {
  return banktbl[l];
}

/*
 * Change the active bank of aux memory
 * Current physical bank is in auxbank
 * logbank - desired logical bank
 */
#ifdef AUXMEM
#pragma code-name (push, "LC")
void change_aux_bank(uint8_t logbank) {
  if (logbank > banktbl[0]) {
    show_error("Nonexistent bank");
    return;
  }
  l_auxbank = logbank;
  // Saves filename[], status, gapbegin, gapend (BUFINFOSZ bytes)
  __asm__("lda %v", auxbank); 
  __asm__("sta $c073");  // Set aux bank
  copyaux(filename, (void*)0x0200, BUFINFOSZ, TOAUX);
  // Load new filename[], status, gapbegin, gapend (BUFINFOSZ bytes)
  auxbank = bank_log_to_phys(l_auxbank);
  __asm__("lda %v", auxbank); 
  __asm__("sta $c073");  // Set aux bank
  copyaux((void*)0x0200, filename, BUFINFOSZ, FROMAUX);
  __asm__("lda #$00");
  __asm__("sta $c073");  // Set aux bank back to 0
}
#pragma code-name (pop)
#endif

/*
 * Used by load_file() when opening a new bank for a large file
 * and also by CA-E "Extend file"
 * Returns 0 if all is well, 1 if we run into an already occupied bank
 */
uint8_t open_new_aux_bank(uint8_t partnum) {
  strcpy(userentry, filename);
  status[2] = partnum;
  if (l_auxbank >= banktbl[0])
    return 1;
  change_aux_bank(++l_auxbank);
  if (DATASIZE() > 0) {
    change_aux_bank(--l_auxbank);
    return 1;
  }
  strcpy(filename, userentry);
  return 0;
}

/*
 * Spinner while loading / saving
 * saving - 1 if we are saving, 0 if we are loading
 * copymode - 1 if this is cut/paste, 0 otherwise
 */
void spinner(uint32_t sz, uint8_t saving, uint8_t copymode) {
  static char chars[] = "|/-\\";
  static char buf[80] = "";
  static uint8_t i = 0;
  gotoxy(0, PROMPT_ROW);
  if (copymode)
    snprintf(buf, 80, "%s clipboard: %c [%lu]",
             (saving ? "Copying to" : "Pasting from"), chars[(i++) % 4], sz);
  else
    snprintf(buf, 80, "%s '%s': %c [%lu]",
             (saving ? "Saving" : "Opening"), filename, chars[(i++) % 4], sz);
  revers(1);
  cprintf("%s", buf);
  cclear(79 - strlen(buf));
  revers(0);
}

// Forward declaration
void draw_screen(void);

/*
 * Load a file from disk into the gapbuf
 * filename - name of file to load
 * replace - if 1, replace old file.
 * copymode - if 1, then load from CLIPBOARD
 * Returns 0 on success
 *         1 if file can't be opened
 */
#pragma code-name (push, "LC")
uint8_t load_file(char *fname, uint8_t replace, uint8_t copymode) {
  uint8_t partctr = 0;
  uint8_t col;
  char *p;
  uint16_t i, j, s;
  uint8_t c, cont;
  FILE *fp = fopen(fname, "r");
  if (!fp)
    return 1;
  if (!replace)
    col = curscol;
  goto_prompt_row();
  if (replace) {
    gapbegin = 0;
    gapend = BUFSZ - 1;
    col = 0;
  }
#ifdef AUXMEM
  cutbuflen = 0;
  p = iobuf;
#else
  p = gapbuf + gapend - IOSZ; // Read to mem just before gapend
#endif
  do {
#ifndef AUXMEM
    if (FREESPACE() < IOSZ * 2) {
      show_error("File truncated");
      goto done;
    }
#endif
    spinner(DATASIZE(), 0, copymode);
    s = fread(p, 1, IOSZ, fp);
    cont = (s == IOSZ ? 1 : 0);
    for (i = 0; i < s; ++i) {
      switch (p[i]) {
      case '\r': // Native Apple2 files
      case '\n': // UNIX files
        set_gapbuf(gapbegin++, '\r');
        col = 0;
#ifdef AUXMEM
        if (replace && (FREESPACE() < 15000) && (banktbl[0] > 1)) {
          draw_screen();
          if (open_new_aux_bank(++partctr) == 1) {
            snprintf(userentry, 80,
                    "Buffer [%03u] not avail. Truncating file.", l_auxbank + 1);
            show_error(userentry);
            if (partctr == 1) // If truncated to one part ...
              status[2] = 0; // Make it a singleton
            partctr = 0; // Prevent status[2] increment below
            goto done;
          }
        }
#endif
        break;
      case '\t':
        c = next_tabstop(col) - col;
        for (j = 0; j < c; ++j)
          set_gapbuf(gapbegin++, ' ');
        col += c;
        break;
      default:
        set_gapbuf(gapbegin++, p[i]);
        ++col;
      }
      if (FREESPACE() < IOSZ * 2) {
        show_error("File truncated");
        goto done;
      }
    }
  } while (cont);
done:
  fclose(fp);
  if (replace) {
#ifdef AUXMEM
    if (partctr > 0)
      status[2] = ++partctr;
#endif
    jump_pos(0);
    pos = 0;
    set_modified(0);
    status[1] = 0; // No need to prompt for overwrite on save
    startsel = endsel = 65535U;
    mode = SEL_NONE;
  }
  return 0;
}
#pragma code-name (pop)

/*
 * Save gapbuf to file
 * If copymode is 0, use filename[] otherwise "CLIPBOARD"
 * copymode - if 1, copy test from startsel to endsel only
 * append - if 1, append to file instead of overwriting
 * Returns 0 on success
 *         1 if file can't be opened
 */
#ifndef AUXMEM
#pragma code-name (push, "LC")
#endif
uint8_t save_file(uint8_t copymode, uint8_t append) {
  uint16_t pos = gapbegin;
  uint16_t sz;
  uint8_t retval = 1;
  uint8_t i;
#ifdef AUXMEM
  uint16_t p, j;
#else
  char *p;
#endif
  FILE *fp;
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen((copymode ? "CLIPBOARD" : filename), (append ? "a" : "w"));
  if (!fp)
    goto done;
  jump_pos(copymode == 1 ? startsel : 0);
  goto_prompt_row();
#ifdef AUXMEM
  cutbuflen = 0;
  p = gapend + 1;
#else
  p = gapbuf + gapend + 1;
#endif
  if (copymode)
    sz = endsel - startsel;
  else
    sz = DATASIZE();
  for (i = 0; i < sz / IOSZ; ++i) {
    spinner(i * IOSZ, 1, copymode);
#ifdef AUXMEM
    for (j = 0; j < IOSZ; ++j)
      iobuf[j] = get_gapbuf(p++);
    if (fwrite(iobuf, IOSZ, 1, fp) != 1)
      goto done;
#else
    if (fwrite(p, IOSZ, 1, fp) != 1)
      goto done;
    p += IOSZ;
#endif
  }
  spinner(i * IOSZ, 1, copymode);
#ifdef AUXMEM
  for (j = 0; j < sz - (IOSZ * i); ++j)
    iobuf[j] = get_gapbuf(p++);
  if (fwrite(iobuf, sz - (IOSZ * i), 1, fp) != 1)
    goto done;
#else
  if (fwrite(p, sz - (IOSZ * i), 1, fp) != 1)
    goto done;
#endif
  retval = 0;
  status[0] = 0; // No longer marked modified
done:
  fclose(fp);
  jump_pos(pos);
  return retval;
}
#ifndef AUXMEM
#pragma code-name (pop)
#endif

/*
 * Obtain the first bank number for the current file
 */
#ifdef AUXMEM
uint8_t find_first_bank(void) {
  if (status[2] == 0) // Not a multi-bank file
    return l_auxbank;
  if (status[2] == 1) // First bank of multi-bank file
    return l_auxbank;
  return l_auxbank - status[2] + 1; // Assumes banks allocated in order
}
#endif

/*
 * Save a large file that spans multiple banks
 */
#ifdef AUXMEM
uint8_t save_multibank_file(void) {
  uint8_t bank = find_first_bank();
  uint8_t origbank = l_auxbank;
  uint8_t retval = 0, modified = 0, first = 1;
  uint8_t filebank;
  if (bank != origbank) {
    change_aux_bank(bank);
    draw_screen();
  }
  if (status[2] == 0) // Oh, it's actually just a single bank file
    return save_file(0, 0);
  if (status[2] != 1) {
    show_error("SBF error"); // Should never happen
    retval = 1;
    goto done;
  }
  // Look at all banks and see if any are modified
  do {
    filebank = status[2];
    if (status[1] == 1) {
      modified = 1;
      break;
    }
    change_aux_bank(++l_auxbank);
  } while (status[2] == filebank + 1);
  change_aux_bank(bank);
  do {
    filebank = status[2];
    draw_screen();
    if (save_file(0, (first == 1 ? 0 : 1)) == 1) {
      retval = 1;
      goto done;
    }
    first = 0;
    change_aux_bank(++l_auxbank);
  } while (status[2] == filebank + 1);
done:
  if (origbank != l_auxbank) {
    change_aux_bank(origbank);
    draw_screen();
  }
  return retval;
}
#endif

/*
 * Read next char from gapbuf[] and update state.
 * Returns 1 on EOL, 0 otherwise
 * If do_print is set, print char on the screen.
 * Interface is via the following globals:
 *  do_print  - if 1, update screen
 *  pos       - position in buffer, advanced by one
 *  row, col  - position on screen
 *  rowlen[]  - length of each screen row in chars
 */
uint8_t read_char_update_pos(void) {
  uint16_t delta = gapend - gapbegin;
  uint16_t s = startsel, e = endsel;
  char c;
  if (startsel > endsel) {
    s = endsel;
    e = startsel;
  }
  if ((c = get_gapbuf(pos++)) == EOL) {
    if (do_print) {
      rowlen[row] = col + 1;
      clreol_wrap();
      gotox(0);
    }
    ++row;
    col = 0;
    return 1;
  }
  if (((pos > s) && (pos <= e)) || // Left of cursor 
      ((pos - delta > s) && (pos - delta - 1 <= e)))   // Right of cursor
    revers(1);
  else
    revers(0);
  if (do_print)
    cputc(c);
  revers(0);
  ++col;
  if (do_print)
    rowlen[row] = col;
  if (col == NCOLS) {
    ++row;
    col = 0;
  }
  return 0;
}

/*
 * Draw screenful of text
 */
void draw_screen(void) {
  uint16_t startpos;
  uint8_t rowsabove, cursorrow;

  // Initialize all rows to length 0 to cover those rows that may have
  // no text and would otherwise be unitialized.
  for (rowsabove = 0; rowsabove < NROWS; ++rowsabove)
    rowlen[rowsabove] = 0;

  // First we have to scan back to work out where in the buffer to
  // start drawing on the screen at the top left. This is at most
  // CURSORROW * NCOLS chars.
  startpos = gapbegin;
  if (startpos > (CURSORROW + 2) * NCOLS)
    startpos -= (CURSORROW + 2) * NCOLS;
  else
    startpos = 0;

  // Format the text and work out the number of rows it takes up
  pos = startpos;
  row = col = 0;
  do_print = 0;
  while (pos < gapbegin)
    read_char_update_pos();
  rowsabove = row; // Number of complete rows above cursor

  if (rowsabove <= CURSORROW) {
    pos = 0;
    cursorrow = rowsabove;
  } else {
    cursorrow = CURSORROW;

    // Now repeat the formatting of the text, eating the first
    // (rowsabove - cursorrow) lines
    pos = startpos;
    row = col = 0;
    while (row < (rowsabove - cursorrow))
      read_char_update_pos();
  }

  // Finally actually write the text to the screen!
  videomode(VIDEOMODE_80COL);
  clrscr();
  revers(0);
  row = col = 0;
  do_print = 1;
  while (pos < gapbegin)
    read_char_update_pos();

  curscol = col;
  cursrow = row;

  // Now write the text after the cursor up to the end of the screen
  pos = gapend + 1;
  while ((pos < BUFSZ) && (row < NROWS))
    read_char_update_pos();

  update_status_line();
}

/*
 * Scroll screen up and update rowlen[]
 */
void scroll_up() {
  draw_screen();
}

/*
 * Scroll screen down and update rowlen[]
 */
void scroll_down() {
  draw_screen();
}

/*
 * Returns 1 if current position in gap buffer is on the last line of
 * the file (ie: no following CR), 0 otherwise
 */
uint8_t is_last_line(void) {
  uint16_t p = gapend + 1;
  while (p < BUFSZ) {
    if (get_gapbuf(p++) == '\r')
      return 0;
  }
  return 1;
}

/*
 * Update screen after delete_char_right()
 */
void update_after_delete_char_right(void) {
  uint8_t eol = 0;
  uint8_t i;
  col = curscol;
  row = cursrow;
  do_print = 1;

  // Print rest of line up to EOL
  pos = gapend + 1;
  if (pos == BUFSZ)
    --rowlen[row];
  else
    while (!eol && (pos < BUFSZ) && (row < NROWS)) {
      i = col;
      eol = read_char_update_pos();
    }

  if (is_last_line())
    clreol_wrap();

  // If necessary, print rest of screen
  if ((get_gapbuf(gapend) == EOL) || (i == NCOLS - 1)) {
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

    clreol_wrap();

    // Erase the rest of the screen (if any)
    for (i = row + 1; i < NROWS; ++i) {
      gotoxy(0, i);
      clrline();
    }
  }

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Update screen after delete_char()
 */
void update_after_delete_char(void) {
  uint8_t eol = 0;
  uint8_t i;
  col = curscol;
  row = cursrow;

  if (get_gapbuf(gapbegin) == EOL) {
    // Special handling if we deleted an EOL
    if (row > 0)
      col = rowlen[--row] - 1;
    else {
      scroll_up();
      return;
    }
  } else {
    // Erase char to left of cursor & update row, col
    gotox(wherex() - 1);
    cputc(' ');
    gotox(wherex() - 1);
    if (col > 0)
      --col;
    else {
      col = NCOLS - 1;
      if (row > 0)
        --row;
      else {
        scroll_up();
        return;
      }
    }
  }

  curscol = col;
  cursrow = row;
  gotoxy(curscol, cursrow);

  do_print = 1;

  // Print rest of line up to EOL
  pos = gapend + 1;
  if (pos == BUFSZ)
    --rowlen[row];
  else
    while (!eol && (pos < BUFSZ) && (row < NROWS)) {
      i = col;
      eol = read_char_update_pos();
    }

  if (is_last_line())
    clreol_wrap();

  // If necessary, print rest of screen
  if ((get_gapbuf(gapbegin) == EOL) || (i == NCOLS - 1)) {
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

    clreol_wrap();

    // Erase the rest of the screen (if any)
    for (i = row + 1; i < NROWS; ++i) {
      gotoxy(0, i);
      clrline();
    }
  }

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Update screen after insert_char()
 */
void update_after_insert_char(void) {
  uint8_t eol = 0;
  uint8_t prevcol;
  col = curscol;
  row = cursrow;

  // Print character just inserted
  pos = gapbegin - 1;
  do_print = 1;
  read_char_update_pos();

  curscol = col;
  cursrow = row;
  gotoxy(curscol, cursrow);

  if (cursrow == NROWS) {
    scroll_down();
    return;
  }

  // Print rest of line up to EOL
  pos = gapend + 1;
  while (!eol && (pos < BUFSZ) && (row < NROWS)) {
    prevcol = col;
    eol = read_char_update_pos();
  }

  // If necessary, print rest of screen
  if ((get_gapbuf(gapbegin - 1) == EOL) || (prevcol == 0))
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Move the cursor left
 */
#pragma code-name (push, "LC")
void cursor_left(void) {
  if (gapbegin > 0)
    set_gapbuf(gapend--, get_gapbuf(--gapbegin));
  else {
    beep();
    gotoxy(curscol, cursrow);
    return;
  }
  if (curscol == 0) {
    if (cursrow == 0) {
      scroll_up();
      if (cursrow == 0) {
        beep();
        return;
      }
    }
    --cursrow;
    curscol = rowlen[cursrow] - 1;
  } else
    --curscol;
  gotoxy(curscol, cursrow);
  if (mode == SEL_SELECT) {
    endsel = gapbegin;
    revers(gapbegin < startsel ? 1 : 0);
    cputc(get_gapbuf(gapbegin));
  }
  revers(0);
  gotoxy(curscol, cursrow);
}
#pragma code-name (pop)

/*
 * Move the cursor right
 */
#pragma code-name (push, "LC")
void cursor_right(void) {
  if (gapend < BUFSZ - 1)
    set_gapbuf(gapbegin++, get_gapbuf(++gapend));
  else {
    beep();
    goto done;
  }
  ++curscol;
  if ((gapend == BUFSZ - 1) && (get_gapbuf(gapend) != EOL))
    goto done;
  if (curscol == rowlen[cursrow]) {
    if (cursrow == NROWS - 1)
      scroll_down();
    ++cursrow;
    curscol = 0;
  }
done:
  if (mode == SEL_SELECT) {
    endsel = gapbegin;
    revers(gapbegin > startsel ? 1 : 0);
    cputc(get_gapbuf(gapbegin - 1));
  }
  revers(0);
  gotoxy(curscol, cursrow);
}
#pragma code-name (pop)

/*
 * Move the cursor up
 * Returns 1 if at top, 0 otherwise
 */
#pragma code-name (push, "LC")
uint8_t cursor_up(void) {
  uint8_t i;
  if (cursrow == 0) {
    scroll_up();
    if (cursrow == 0) {
      beep();
      gotoxy(curscol, cursrow);
      return 1;
    }
  }
  for (i = curscol; i > 0; --i) {
    set_gapbuf(gapend--, get_gapbuf(--gapbegin));
    if (mode == SEL_SELECT) {
      gotoxy(i - 1, cursrow);
      revers(gapbegin < startsel ? 1 : 0);
      cputc(get_gapbuf(gapbegin));
    }
  }
  --cursrow;
  // Short line ...
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  for (i = rowlen[cursrow]; i > curscol; --i) {
    set_gapbuf(gapend--, get_gapbuf(--gapbegin));
    if (mode == SEL_SELECT) {
      gotoxy(i - 1, cursrow);
      revers(gapbegin < startsel ? 1 : 0);
      cputc(get_gapbuf(gapbegin));
    }
  }
  if (mode == SEL_SELECT) {
    endsel = gapbegin;
  }
  revers(0);
  gotoxy(curscol, cursrow);
  return 0;
}
#pragma code-name (pop)

/*
 * Move the cursor down
 * Returns 1 if at bottom, 0 otherwise
 */
#pragma code-name (push, "LC")
uint8_t cursor_down(void) {
  uint8_t i;
  if (cursrow == NROWS - 1)
    scroll_down();
  if ((get_gapbuf(gapend + rowlen[cursrow] - curscol) != EOL) &&
      (rowlen[cursrow] != NCOLS))
    return 1; // Last line

  if (rowlen[cursrow] == 0) {
    beep();
    return 1;
  }
  for (i = 0; i < rowlen[cursrow] - curscol; ++i) {
    if (gapend < BUFSZ - 1) {
      set_gapbuf(gapbegin++, get_gapbuf(++gapend));
      if (mode == SEL_SELECT) {
        revers(gapbegin > startsel ? 1 : 0);
        cputc(get_gapbuf(gapbegin - 1));
      }
    }
    else { 
      beep();
      return 1;
    }
  }
  ++cursrow;
  // Short line ...
  if (rowlen[cursrow] == 0)
    curscol = 0;
  else if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  gotoxy(0, cursrow);
  for (i = 0; i < curscol; ++i) {
    if (gapend < BUFSZ - 1) {
      set_gapbuf(gapbegin++, get_gapbuf(++gapend));
      if (mode == SEL_SELECT) {
        revers(gapbegin > startsel ? 1 : 0);
        cputc(get_gapbuf(gapbegin - 1));
      }
    }
  }
  if (mode == SEL_SELECT) {
    endsel = gapbegin;
  }
  revers(0);
  gotoxy(curscol, cursrow);
  return 0;
}
#pragma code-name (pop)

/*
 * Goto beginning of line
 */
void goto_bol(void) {
  while (curscol > 0)
    cursor_left();  
}

/*
 * Goto end of line
 */
void goto_eol(void) {
  while (curscol < rowlen[cursrow] - 1)
    cursor_right();  
}

/*
 * Word left
 */
void word_left(void) {
  do {
    cursor_left();
  } while ((get_gapbuf(gapbegin) != ' ') && (get_gapbuf(gapbegin) != EOL) &&
           (gapbegin > 0));
}

/*
 * Word right
 */
void word_right(void) {
  do {
    cursor_right();
  } while ((get_gapbuf(gapbegin) != ' ') && (get_gapbuf(gapbegin) != EOL) &&
           (gapend < BUFSZ - 1));
}

/*
 * Jump forward 15 screen lines
 */
void page_down(void) {
  uint8_t i;
  for (i = 0; i < 15; ++i)
    if (cursor_down() == 1)
      break;
}

/*
 * Jump back 15 screen lines
 */
void page_up(void) {
  uint8_t i;
  for (i = 0; i < 15; ++i)
    if (cursor_up() == 1)
      break;
}

/*
 * Perform word-wrapping on current paragraph
 * addbreaks - if 1, add breaks to do word wrap, otherwise it 'unwraps'
 *             (ie: removes all carriage returns in paragraph)
 */
void word_wrap_para(uint8_t addbreaks) {
  uint16_t i = gapbegin;
  uint8_t rets = 0, col = 0;
  uint16_t startpara = 0;
  // Find start of paragraph ("\r\r" delimiter)
  while (i > 0) {
    --i;
    if (get_gapbuf(i) == '\r')
      ++rets;
    else
      rets = 0;
    if (rets == 2) {
      startpara = i + 2;
      break;
    }
  }
  // Iterate through para, up to the cursor
  i = startpara;
  while (i < gapbegin) {
    if (get_gapbuf(i) == '\r')
      set_gapbuf(i, ' ');
    ++col;
    if (addbreaks && (col == 76)) {
      // Backtrack to find a space
      while ((get_gapbuf(i) != ' ') && (i > startpara))
        --i;
      if (i == startpara) {
        beep();
        return;
      }
      set_gapbuf(i, '\r'); // Break line
      col = 0;
    }
    ++i;
  }
}

/*
 * Help screen
 * EDITHELP.TXT is expected to contain lines of exactly 80 chars
 */
void help(uint8_t num) {
  FILE *fp;
  char *p;
  char c;
  uint16_t i, s;
  uint8_t cont;
  revers(0);
  cursor(0);
  clrscr();
  snprintf(iobuf, 80, "%s/EDITHELP%u.TXT", startdir, num);
  fp = fopen(iobuf, "rb");
  if (!fp) {
    printf("Can't open help file\n\n");
    goto done;
  }
#ifdef AUXMEM
  cutbuflen = 0;
  p = iobuf;
#else
  p = gapbuf + gapend - IOSZ; // Read to just before gapend
#endif
  do {
    if (FREESPACE() < IOSZ) {
      beep();
      goto done;
    }
    s = fread(p, 1, IOSZ, fp);
    cont = (s == IOSZ ? 1 : 0);
    for (i = 0; i < s; ++i) {
      c = p[i];
      if (c == '{')
        printf("%s", openapple);
      else if (c == '}')
        printf("%s", closedapple);
      else if ((c != '\r') && (c != '\n'))
        putchar(c);
    }
  } while (cont);
done:
  fclose(fp);
}

/*
 * Load EMAIL.SYSTEM to $2000 and jump to it
 */
void load_email(void) {
  revers(0);
  clrscr();
  sprintf(userentry, "%s/EMAIL.SYSTEM", startdir);
  exec(userentry, NULL);
}

/*
 * Load ATTACHER.SYSTEM to $2000 and jump to it
 */
void load_attacher(void) {
  revers(0);
  clrscr();
  sprintf(userentry, "%s/ATTACHER.SYSTEM", startdir);
  exec(userentry, filename);
}

void file_ui(char *, char *, char *); // Forward declaration
void name_file(void);                 // Forward declaration

/*
 * Save file to disk, handle user interface
 */
void save(void) {
  uint8_t rc;
  FILE *fp;
  if (strlen(filename) == 0) {
    status[1] = 1; // Prompt if save will overwrite existing file
    name_file();
    if (strlen(filename) == 0)
      return;
  }
  // If status[1] is set, check for overwrite
  if (status[1]) {
    fp = fopen(filename, "r");
    if (fp) {
      fclose(fp);
      beep();
      snprintf(userentry, 80, "File '%s' exists, overwrite", filename);
      if (prompt_okay(userentry) != 0)
        return; 
    }
    fclose(fp);
  }
#ifdef AUXMEM
  rc = save_multibank_file();
#else
  rc = save_file(0, 0);
#endif
  switch (rc) {
  case 0: // Success
    status[1] = 0; // No need to prompt for overwrite next time
    set_modified(0);
    break;
  case 1: // Save error
    snprintf(userentry, 80, "Can't save '%s'", filename);
    show_error(userentry);
    break;
  }
}

/*
 * Perform replace part of search/replace
 * pos - position where search term was found
 * r - if 0 then perform replace operation
 * ask - if 1 then prompt for each replacement
 * Returns 1 if search should continue, 0 otherwise
 */
uint8_t finish_search_replace(uint16_t pos, uint8_t r, uint8_t ask) {
  uint8_t i;
  mode = SEL_NONE;
  jump_pos(pos);
  startsel = gapend + 1;
  endsel = gapend + 1 + strlen(search);
  draw_screen();
  if (r == 0) { // Replace mode
    if (ask) {
      i = prompt_okay("Replace");
      if (i == 1) { // 'n'
        cursor_right();
        return 1; // Continue
      }
      if (i == 2) { // ESC
        startsel = endsel = 65535U;
        return 0; // Abort
      }
    }
    for (i = 0; i < strlen(search); ++i)
      delete_char_right();
    for (i = 0; i < strlen(replace); ++i)
      insert_char(replace[i]);
    set_modified(1);
    draw_screen();
    cursor_right();
    return 1; // Continue
  }
  startsel = endsel = 65535U;
  return 0; // Do not continue
}

/*
 * Perform search/replace operation
 * r - if 0 then perform replace operation
 * ask - if 1 then prompt for each replacement
 */
void do_search_replace(uint8_t r, uint8_t ask) {
  uint8_t wrapcount = 0;
  uint16_t foundpos;
search:
  mode = SRCH1; // Searching ..
  update_status_line();
  set_gapbuf(BUFSZ + 1, '\0'); // NULL term for search_in_gapbuf()
  if (search_in_gapbuf(gapend + 1, search, &foundpos) == 0) {
    if (wrapcount > 0) {
      mode = SEL_NONE;
      return; // Wrapped already. Give up.
    }
    ++wrapcount;
    mode = SRCH2; // Wrapped .. 
    update_status_line();
    set_gapbuf(gapbegin, '\0'); // NULL term for search_in_gapbuf()
    if (search_in_gapbuf(0, search, &foundpos) == 0) {
      mode = SRCH3; // Not found ..
      return;
    }
    if (finish_search_replace(foundpos, r, ask) == 1)
      goto search;
    mode = SEL_NONE;
    return;
  }
  if (finish_search_replace(gapbegin + foundpos - (gapend + 1), r, ask) == 1)
    goto search;
  mode = SEL_NONE;
}

/*
 * Disconnect RAM disk /RAM
 */
void disconnect_ramdisk(void) {
  uint8_t i, j;
  uint8_t *devcnt = (uint8_t*)0xbf31; // Number of devices
  uint8_t *devlst = (uint8_t*)0xbf32; // Disk device numbers
  uint16_t *s0d1 = (uint16_t*)0xbf10; // s0d1 driver vector
  uint16_t *s3d2 = (uint16_t*)0xbf26; // s3d2 driver vector
  if (*s0d1 == *s3d2)
    return;               // No /RAM connected
  for (i = *devcnt; i > 0; --i) {
    if ((devlst[i] == 0xbf) || (devlst[i] == 0xbb) ||
        (devlst[i] == 0xb7) || (devlst[i] == 0xb3))
      break;
  }
  if (i > 0) {
    for (j = i; j < *devcnt; ++j) {
      devlst[j] = devlst[j + 1];
    }
  }
  *s3d2 = *s0d1;
  --(*devcnt);  
}

/*
 * Available aux banks.  Taken from RamWorks III Manual p47
 * banktbl is populated as follows:
 * First byte is number of banks
 * Following bytes are the logical bank numbers in physical bank order
 * Final byte is $ff terminator
 */
void avail_aux_banks(void) {
  __asm__("sta $c009"); // Store in ALTZP
  __asm__("ldy #$7f");  // Maximum valid bank
findbanks:
  __asm__("sty $c073"); // Select bank
  __asm__("sty $00");   // Store bank num in ALTZP
  __asm__("tya");
  __asm__("eor #$ff");
  __asm__("sta $01");   // Store the inverse in ALTZP too
  __asm__("dey");
  __asm__("bpl %g", findbanks);
// Read back the bytes we wrote to find valid banks
  __asm__("lda #$00");
  __asm__("tay");
  __asm__("tax");
findthem:
  __asm__("sty $c073"); // Select bank
  __asm__("sta $c076"); // Why?
  __asm__("cpy $00");
  __asm__("bne %g", notone);
  __asm__("tya");
  __asm__("eor #$ff");
  __asm__("cmp $01");
  __asm__("bne %g", notone);
  __asm__("inx");
  __asm__("tya");
  __asm__("sta %v,x", banktbl);
  __asm__("cpx #128"); // 8MB max
  __asm__("bcs %g", done);
notone: // 'not one', not 'no tone' !
  __asm__("iny");
  __asm__("bpl %g", findthem);
done:
  __asm__("lda #$00");
  __asm__("sta $c073"); // Back to aux bank 0
  __asm__("sta $c008"); // Turn off ALTZP
  __asm__("stx %v", banktbl); // Number of banks
  __asm__("lda #$ff");
  __asm__("inx");
  __asm__("sta %v,x", banktbl); // Terminator
}

/*
 * Initialize the data at 0x200 in each aux bank so that all buffers
 * are empty, unmodified and without a filename
 */
void init_aux_banks(void) {
  uint8_t i;
  uint16_t count;
  revers(1);
  cprintf("EDIT.SYSTEM                   Bobbi 2020");
  revers(0);
  cprintf("\n\n\n  %u x 64KB aux banks -> %uKB\n", banktbl[0], banktbl[0]*64);
  for (i = 1; i <= banktbl[0]; ++i) {
    auxbank = bank_log_to_phys(i);
    // Saves filename[], gapbegin, gapend and modified (BUFINFOSZ bytes)
    __asm__("lda %v", auxbank); 
    __asm__("sta $c073");  // Set aux bank
    copyaux(filename, (void*)0x0200, BUFINFOSZ, TOAUX);
  }
  auxbank = 0;
  l_auxbank = 1;
  __asm__("lda #$00");
  __asm__("sta $c073");  // Set aux bank back to 0
  for (count = 0; count < 10000; ++count); // Delay so user can read message
}

/*
 * Generate buffer list
 */
#ifdef AUXMEM
#pragma code-name (push, "LC")
void buffer_list(void) {
  uint8_t o_aux = l_auxbank, row = 0;
  uint8_t i;
  cursor(0);
  for (i = 1; i <= banktbl[0]; ++i) {
    if (row == 0) {
      clrscr();
      revers(1);
      cprintf("Active Buffer List (Total number of buffers: %u)", banktbl[0]);
      revers(0);
      gotoxy(0, row += 2);
      cprintf(" Buf  Size   Mod  Part  Filename\r");
      gotoxy(0, ++row);
    }
    change_aux_bank(i);
    if (DATASIZE() > 0) {
      cprintf("[%03u] %05u | %c | %3u | %s\r",
              i, DATASIZE(), (status[0] ? '*' : ' '), status[2], filename);
      gotoxy(0, ++row);
    }
    if (row == 22) {
      cprintf("[Press Any Key]");
      cgetc();
      row = 0;
    }
  }
  change_aux_bank(o_aux);
  gotoxy(0, 23);
  cprintf("[Press Any Key]");
  cgetc();
}
#pragma code-name (pop)
#endif

/*
 * Rename a file, taking care of multi-bank files
 */
void name_file(void) {
  uint8_t bank = find_first_bank();
  uint8_t origbank = l_auxbank;
  uint8_t retval = 0, modified = 0, first = 1;
  uint8_t filebank;
  file_ui("Set Filename", openfilemsg, namefilemsg);
  if (strlen(userentry) == 0) {
    draw_screen();
    return;
  }
  if (bank != origbank)
    change_aux_bank(bank);
  if (status[2] == 0) { // Oh, it's actually just a single bank file
    strcpy(filename, userentry);
    status[1] = 1; // Should prompt if overwriting file on save
    draw_screen();
    return;
  }
  if (status[2] != 1) {
    draw_screen();
    show_error("SBF error"); // Should never happen
    return;
  }
  do {
    filebank = status[2];
    strcpy(filename, userentry);
    status[1] = 1; // Should prompt if overwriting file on save
    change_aux_bank(++l_auxbank);
  } while (status[2] == filebank + 1);
  change_aux_bank(origbank);
  draw_screen();
}

/*
 * Save all modified buffers
 */
#ifdef AUXMEM
void save_all(void) {
  uint8_t o_aux = l_auxbank;
  uint8_t i;
  cursor(0);
  for (i = 1; i <= banktbl[0]; ++i) {
    change_aux_bank(i);
    if (status[0]) { // If buffer is modified
      if (strlen(filename) > 0)
        snprintf(userentry, 80, "Save '%s'", filename);
      else
        strcpy(userentry, "Save <scratch> buffer");
      if (prompt_okay(userentry) != 0)
        continue; 
      draw_screen();
      save();
    }
  }
  change_aux_bank(o_aux);
}
#endif

/*
 * Sort startsel / endsel in ascending order to allow backwards selection
 */
void order_selection(void) {
  uint16_t tmp;
  if (startsel > endsel) {
    tmp = endsel;
    endsel = startsel;
    startsel = tmp;
  }
}

/*
 * Remember current gapbuf settings to support undo
 */
void mark_undo(void) {
  oldgapbegin = gapbegin;
  oldgapend   = gapend;
  canundo     = 1;
}

struct tabent {
  char     name[16];
  uint8_t  type;
  uint32_t size;
} *entry;

#define FILELINES 16

/*
 * Draw one line in file chooser UI
 * i - index of file to draw
 * first - index of first file on screen
 * selected - index of currently selected file
 * entries - total number of file entries in directory
 */
void file_ui_draw(uint16_t i, uint16_t first, uint16_t selected, uint16_t entries) {
  struct tabent *entry;
  gotoxy(5, i - first + 6);
  if (i < entries) {
    entry = (struct tabent*)iobuf + i;
    if (entry->type == 0x0f) {
      sprintf(userentry, "[ %s ]                               ", entry->name);
      userentry[34] = '\0';
    } else {
      sprintf(userentry, "  %s                   ", entry->name);
      sprintf(&userentry[18], "  %8lu  ", entry->size);
      switch (entry->type) {
      case 0x04:
        sprintf(&userentry[30], "TXT ");
        break;
      case 0x06:
        sprintf(&userentry[30], "BIN ");
        break;
      case 0x19:
        sprintf(&userentry[30], "ADB ");
        break;
      case 0x1a:
        sprintf(&userentry[30], "AWP ");
        break;
      case 0x1b:
        sprintf(&userentry[30], "ASP ");
        break;
      case 0xfc:
        sprintf(&userentry[30], "BAS ");
        break;
      case 0xff:
        sprintf(&userentry[30], "SYS ");
        break;
      default:
        sprintf(&userentry[30], "$%02x ", entry->type);
      }
    }
    if (i == selected)
      revers(1);
    cputs(userentry);
    if (i == selected)
      revers(0);
  } else {
    strcpy(userentry, "                                  ");
    cputs(userentry);
  }
}

/*
 * File chooser UI
 * first - index of first file on screen
 * selected - index of currently selected file
 * entries - total number of file entries in directory
 */
void file_ui_draw_all(uint16_t first, uint16_t selected, uint16_t entries) {
  uint16_t i;
  uint16_t last = first + FILELINES;
  for (i = first; i < last; ++i)
    file_ui_draw(i, first, selected, entries);
}

/*
 * Perform ProDOS MLI ON_LINE call to
 * write all online volume names into iobuf[]
 * Return the number of entries
 */
uint16_t online(void) {
  uint16_t entries = 0;
  struct tabent *entry;
  uint8_t i, j, len;
  __asm__("lda #$00"); // All devices
  __asm__("sta mliparam + 1");
  __asm__("lda #<%v", iobuf); // iobuf LSB
  __asm__("sta mliparam + 2");
  __asm__("lda #>%v", iobuf); // iobuf MSB
  __asm__("ina");             // Bump up 256 bytes into iobuf
  __asm__("sta mliparam + 3");
  __asm__("lda #$c5"); // ON_LINE
  __asm__("ldx #$02"); // Two parms
  __asm__("jsr callmli");
  entry = (struct tabent*)iobuf;
  for (i = 0; i < 16; ++i) {
    len = iobuf[256 + i * 16] & 0x0f;
    if (len > 0) {
      entry->type = 0x0f;
      entry->name[0] = '/';
      for (j = 0; j < len; ++j)
        entry->name[j + 1] = iobuf[256 + i * 16 + j + 1];
      entry->name[j + 1] = '\0';
      ++entry;
      ++entries;
    }
  }
  return entries;
}

/*
 * File chooser UI
 * Leaves file name in userentry[], or empty string if error/cancel
 * msg1 - Message for top line
 * msg2 - Message for second line
 * msg3 - Message for third line
 */
void file_ui(char *msg1, char *msg2, char *msg3) {
  struct tabent *entry;
  DIR *dp;
  struct dirent *ent;
  char c;
  uint16_t entries, current, first;
  uint8_t toplevel = 0;
restart:
  clrscr();
  gotoxy(0,0);
  revers(1);
  cprintf("%s", msg1);
  revers(0);
  gotoxy(0,1);
  cprintf("%s", msg2);
  gotoxy(0,2);
  cprintf("%s", msg3);
  getcwd(userentry, 80);
  gotoxy(0,4);
  revers(1);
  cprintf("%s", (toplevel ? "Volumes" : userentry));
  revers(0);
  entries = current = first = 0;
  cutbuflen = 0;
  if (toplevel) {
    entries = online();
  } else {
    entry = (struct tabent*)iobuf;
    strcpy(entry->name, ".."); // Add fake '..' entry
    entry->type = 0x0f;
    ++entry;
    ++entries;
    cursor(0);
    dp = opendir(".");
    while (1) {
      ent = readdir(dp);
      if (!ent)
        break;
      memcpy(entry->name, ent->d_name, 16);
      entry->type = ent->d_type;
      entry->size = ent->d_size;
      ++entry;
      ++entries;
      if ((char*)entry > (char*)iobuf + CUTBUFSZ - 100) {
        beep();
        break;
      }
    }
    closedir(dp);
  }
redraw:
  file_ui_draw_all(first, current, entries);
  while (1) {
    c = cgetc();
    switch (c) {
    case 0x0b:  // Up
      if (current > 0)
        --current;
      if (current < first) {
        if (first > FILELINES)
          first -= FILELINES;
        else
          first = 0;
        goto redraw;
      }
      file_ui_draw(current, first, current, entries);
      file_ui_draw(current + 1, first, current, entries);
      break;
    case 0x0a:  // Down
      if (current < entries - 1)
        ++current;
      if (current >= first + FILELINES) {
        first += FILELINES;
        goto redraw;
      }
      file_ui_draw(current - 1, first, current, entries);
      file_ui_draw(current, first, current, entries);
      break;
    case EOL:
      entry = (struct tabent*)iobuf + current;
      switch (entry->type) {
      case 0x0f: // Directory
        getcwd(userentry, 80);
        if (strcmp(entry->name, "..") == 0) {
          for (c = strlen(userentry); c > 0; --c)
            if (userentry[c] == '/') {
              userentry[c] = '\0';
              break;
            }
          if (c <= 1)
            toplevel = 1;
          else
            chdir(userentry);
          goto restart;
        } else {
          if (toplevel) {
            strcpy(userentry, entry->name);
            chdir(userentry);
          } else {
            getcwd(userentry, 80);
            strcat(userentry, "/");
            strcat(userentry, entry->name);
            chdir(userentry);
          }
          toplevel = 0;
          goto restart;
        }
        break;
      case 0x04: // ASCII text
        strcpy(userentry, entry->name);
        goto done;
        break;
      default:
        if (prompt_okay("Not a text file, open anyhow") != 0) {
          strcpy(userentry, "");
          goto done;
        }
        strcpy(userentry, entry->name);
        goto done;
      }
      break;
    case ESC:
      strcpy(userentry, "");
      goto done;
      break;
    case 0x09: // Tab
      if (prompt_for_name("Enter filename", 0) == 255)
        goto restart; // ESC pressed
      if (userentry[0] == '/')    // Absolute path
        goto done;
      getcwd(replace, 80);       // Otherwise relative path
      strcat(replace, "/");
      strcat(replace, userentry);
      strcpy(userentry, replace);
      strcpy(replace, "");
      goto done;
    }
  }
done:
  clrscr();
  cursor(1);
}

/*
 * Main editor routine
 * fname - filename to open or ""
 */
int edit(char *fname) {
  char c; 
  uint16_t position, tmp;
  uint8_t i, ask;
  videomode(VIDEOMODE_80COL);
  if (fname) {
    strcpy(filename, fname);
    cprintf("Loading file %s ", filename);
    if (load_file(filename, 1, 0)) {
      snprintf(userentry, 80, "Can't load '%s'", filename);
      show_error(userentry);
      strcpy(filename, "");
    }
  }
  draw_screen();
  while (1) {
    cursor(1);
    c = cgetc();
    if (mode == SRCH3)  // Reset 'Not found'
      mode = SEL_NONE;
    switch (c) {
    case ESC: // Escape from block mode operations
      startsel = endsel = 65535U;
      mode = SEL_NONE;
      draw_screen();
      break;
    case 0x80 + '1': // Top
      jump_pos(0);
      draw_screen();
      break;
    case 0x80 + '2':
      jump_pos(DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '3':
      jump_pos(2L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '4':
      jump_pos(3L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '5':
      jump_pos(4L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '6':
      jump_pos(5L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '7':
      jump_pos(6L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '8':
      jump_pos(7L * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '9': // Bottom
      jump_pos(DATASIZE());
      draw_screen();
      break;
    case 0x80 + 0x08:  // OA-Left "Word left"
      word_left();
      break;
    case 0x80 + 0x15:  // OA-Right "Word right"
      word_right();
      break;
    case 0x80 + ',':  // OA-< "Home"
    case 0x80 + '<':
    case 0x01:        // Ctrl-A (Emacs style)
      goto_bol();
      break;
    case 0x80 + '.':  // OA-> "End"
    case 0x80 + '>':
    case 0x05:        // Ctrl-E (Emacs style)
      goto_eol();
      break;
    case 0x8b:  // OA-Up "Page Up"
      page_up();
      break;
    case 0x8a:  // OA-Down "Page Down"
      page_down();
      break;
    case 0x80 + ' ': // OA-SPACE start/end selection
       tmp = (startsel == 65535U ? 0 : 1); // Prev selection active?
       if (tmp) {
        endsel = gapbegin;
        mode = SEL_NONE;
        draw_screen();
      } else {
        startsel = endsel = gapbegin;
        mode = SEL_SELECT;
        update_status_line();
      }
      break;
    case 0x80 + 'A': // OA-A "Select All"
    case 0x80 + 'a': // OA-a
      startsel = 0;
      endsel = DATASIZE();
      mode = SEL_NONE;
      draw_screen();
      break;
    case 0x80 + 'X': // OA-X "Cut"
    case 0x80 + 'x': // OA-x
    case 0x18:       // ^X "Cut"
      tmp = 65535U;
    case 0x80 + 'C': // OA-C "Copy"
    case 0x80 + 'c': // OA-c
    case 0x03:       // ^C "Copy"
      ++tmp;
      mode = SEL_NONE;
      if (startsel == 65535U) { // No selection
        beep();
        break;
      }
      order_selection();
      position = gapbegin;
#ifdef AUXMEM
      if (endsel - startsel <= CUTBUFSZ) {
        cutbuflen = endsel - startsel;
        jump_pos(startsel);
        __asm__("lda %v", auxbank); 
        __asm__("sta $c073");  // Set aux bank
        copyaux((char*)0x0800 + gapend + 1, iobuf, cutbuflen, FROMAUX);
        __asm__("lda #$00");
        __asm__("sta $c073");  // Set aux bank back to 0
      } else {
        cutbuflen = 0;
#endif
        if (save_file(1, 0) == 1) {
          show_error("Can't save CLIPBOARD");
          draw_screen();
          break;
        }
#ifdef AUXMEM
      }
#endif
      if (tmp == 0) { // Cut
        mark_undo();
        jump_pos(startsel);
        gapend += (endsel - startsel);
        set_modified(1);
      } else // Copy
        jump_pos(position);
      startsel = endsel = 65535U;
      draw_screen();
      break;
    case 0x80 + 'V': // OA-V "Paste"
    case 0x80 + 'v': // OA-v
    case 0x16:       // ^V "Paste"
      mode = SEL_NONE;
      mark_undo();
      if (cutbuflen > 0) {
        __asm__("lda %v", auxbank); 
        __asm__("sta $c073");  // Set aux bank
        copyaux(iobuf, (char*)0x0800 + gapbegin, cutbuflen, TOAUX);
        __asm__("lda #$00");
        __asm__("sta $c073");  // Set aux bank back to 0
        gapbegin += cutbuflen;
      } else {
        if (load_file("CLIPBOARD", 0, 1))
          show_error("Can't open CLIPBOARD");
      }
      startsel = endsel = 65535U;
      draw_screen();
      break;
    case 0x80 + 'R': // OA-R "Replace"
    case 0x80 + 'r': // OA-r
      tmp = 65535U;
    case 0x80 + 'F': // OA-F "Find"
    case 0x80 + 'f': // OA-F "Find"
      ++tmp;
      snprintf(userentry, 80, "Find (%s)", search);
      if (prompt_for_name(userentry, 0) == 255)
        break; // ESC pressed
      if (strlen(userentry) > 0)
        strcpy(search, userentry);
      else if (strlen(search) == 0)
        break;
      cursor_right();
      if (tmp == 0) { // Replace mode
        snprintf(userentry, 80, "Replace (%s)", replace);
        if (prompt_for_name(userentry, 0) == 255)
          break; // ESC pressed
        if (strlen(userentry) > 0)
          strcpy(replace, userentry);
        else if (strlen(replace) == 0)
          break;
        ask = 0;
        if (prompt_okay("Ask for each") == 0)
          ask = 1;
        canundo = 0;
      }
      do_search_replace(tmp, ask);
      update_status_line();
      break;
    case 0x80 + 'I': // OA-I "Insert file"
    case 0x80 + 'i':
      file_ui("Insert File at Cursor", "", openfilemsg);
      if (strlen(userentry) == 0) {
        draw_screen();
        break;
      }
      mark_undo();
      if (load_file(userentry, 0, 0)) {
        snprintf(iobuf, 80, "Can't open '%s'", userentry);
        show_error(iobuf);
      }
      draw_screen();
      break;
    case 0x80 + 'O': // OA-O "Open"
    case 0x80 + 'o':
      if (status[0])
        save();
      file_ui("Open File", "", openfilemsg);
      if (strlen(userentry) == 0) {
        draw_screen();
        break;
      }
      strcpy(filename, userentry);
      if (load_file(filename, 1, 0)) {
        snprintf(userentry, 80, "Can't open '%s'", filename);
        show_error(userentry);
        strcpy(filename, "");
      }
      canundo = 0;
      draw_screen();
      break;
    case 0x80 + 'N': // OA-N "Name"
    case 0x80 + 'n': // OA-n
      name_file();
      break;
    case 0x80 + 'Q': // OA-Q "Quit"
    case 0x80 + 'q': // OA-q
      save_all();
      if (quit_to_email) {
        if (prompt_okay("Add attachments") == 0)
          load_attacher();
        if (prompt_okay("Quit to EMAIL") == 0)
          load_email();
      } else {
        if (prompt_okay("Quit to ProDOS") == 0) {
          revers(0);
          clrscr();
          exit(0);
        }
      }
      break;
    case 0x80 + 'U': // OA-U "Unwrap"
    case 0x80 + 'u': // OA-w
      canundo = 0;
      word_wrap_para(0);
      draw_screen();
      break;
    case 0x80 + 'W': // OA-W "Wrap"
    case 0x80 + 'w': // OA-w
      canundo = 0;
      word_wrap_para(1);
      draw_screen();
      break;
    case 0x80 + 'S': // OA-S "Save"
    case 0x80 + 's': // OA-s
      save();
      draw_screen();
      break;
    case 0x80 + 'Z': // OA-Z "Undo"
    case 0x80 + 'z': // OA-z
    case 0x1a:       // Ctrl-Z
      if (canundo) {
        tmp         = gapbegin;
        gapbegin    = oldgapbegin;
        oldgapbegin = tmp;
        tmp         = gapend;
        gapend      = oldgapend;
        oldgapend   = tmp;
        draw_screen();
      } else
        beep();
      break;
    case 0x80 + DELETE: // OA-Backspace
    case 0x04:  // Ctrl-D "DELETE"
      if (mode == SEL_NONE) {
        mark_undo();
        delete_char_right();
        update_after_delete_char_right();
        set_modified(1);
      }
      break;
    case 0x80 + '?': // OA-? "Help"
help1:
      help(1);
      c = cgetc();
      if (c == ESC)
        goto donehelp;
      help(2);
      c = cgetc();
      switch (c) {
      case 0x0b: // Up
      case 'p':
      case 'P':
        goto help1;
      }
donehelp:
      draw_screen();
      break;
    case 0x0c:  // Ctrl-L "REFRESH"
      draw_screen();
      break;
    case DELETE:  // DEL "BACKSPACE"
      if (startsel == 65535U) { // No selection
        if (mode == SEL_NONE) {
          mark_undo();
          delete_char();
          update_after_delete_char();
          set_modified(1);
        }
      } else {
        snprintf(userentry, 80,
                 "Delete selection (%d chars)", abs(endsel - startsel));
        if (prompt_okay(userentry) != 0)
          break;
        order_selection();
        mode = SEL_NONE;
        mark_undo();
        jump_pos(startsel);
        gapend += (endsel - startsel);
        set_modified(1);
        startsel = endsel = 65535U;
        draw_screen();
        break;
      }
      break;
    case 0x09:  // Tab
      if (mode == SEL_NONE) {
        mark_undo();
        c = next_tabstop(curscol) - curscol;
        for (i = 0; i < c; ++i) {
          insert_char(' ');
          update_after_insert_char();
        }
        set_modified(1);
      }
      break;
    case 0x08:  // Left
      cursor_left();
      break;
    case 0x15:  // Right
      cursor_right();
      break;
    case 0x0b:  // Up
      cursor_up();
      break;
    case 0x0a:  // Down
      cursor_down();
      break;
    case EOL:   // Return
      if (mode == SEL_NONE) {
        mark_undo();
        insert_char(c);
        update_after_insert_char();
        set_modified(1);
      }
      break;
    default:
#ifdef AUXMEM
      if (*(uint8_t*)0xc062 & 0x80) { // Closed Apple depressed
        // CA-number (1-9) - quick jump to first 9 buffers
        if ((c >= '1') && (c <= '9')) {
          change_aux_bank(c - '0');
          startsel = endsel = 65535U;
          draw_screen();
        } else if ((c == 'B') || (c == 'b')) { // CA-B "Buffer"
          snprintf(userentry, 80, "Buffer # (1 - %u)", banktbl[0]);
          if (prompt_for_name(userentry, 0) == 255) {
            update_status_line();
            break;
          }
          if (strlen(userentry) == 0) {
            update_status_line();
            break;
          }
          tmp = atoi(userentry);
          if ((tmp < 1) || (tmp > banktbl[0])) {
            beep();
            update_status_line();
            break;
          }
          change_aux_bank(tmp);
          startsel = endsel = 65535U;
          draw_screen();
        } else if ((c == '-') || (c == '_')) { // CA-minus
          tmp = (l_auxbank - 2) % banktbl[0] + 1;
          change_aux_bank(tmp);
          startsel = endsel = 65535U;
          draw_screen();
        } else if ((c == '+') || (c == '=')) { // CA-plus
          tmp = l_auxbank % banktbl[0] + 1;
          change_aux_bank(tmp);
          startsel = endsel = 65535U;
          draw_screen();
        } else if ((c == 'E') || (c == 'e')) { // CA-E "Extend file"
          i = status[2];
          if (i == 0) // If this was single buf file, make it part 1
            i = 1;
          if (open_new_aux_bank(i) == 1) {
            snprintf(userentry, 80,
                    "Buffer [%03u] not avail. Can't extend.", l_auxbank + 1);
            show_error(userentry);
            break;
          }
          status[2] = i + 1;
          draw_screen();
        } else if ((c == 'L') || (c == 'l')) {
          buffer_list();
          draw_screen();
        } else if ((c == 'T') || (c == 't')) { // CA-T "Truncate file"
          if (prompt_okay("Truncate file here") == 0) {
            tmp = l_auxbank;
            gapend = BUFSZ - 1;
            draw_screen();
            if (status[2] > 0) { // If multipart
              if (status[2] == 1) // Truncating in first bank
                status[2] = 0;    // Make it a singleton
              change_aux_bank(++l_auxbank);
              do {
                gapbegin = 0;
                gapend = BUFSZ - 1;
                filename[0] = '\0';
                i = status[2];
                status[0] = status[1] = status[2] = 0;
                change_aux_bank(++l_auxbank);
              } while (status[2] == i + 1);
              change_aux_bank(tmp);
            }
          }
          set_modified(1);
          canundo = 0;
        } else if ((c =='S') || (c == 's')) { // CA-S "Save all"
          save_all();
          draw_screen();
        }
      }
      else if ((c >= 0x20) && (c < 0x80) && (mode == SEL_NONE)) {
#else
      if ((c >= 0x20) && (c < 0x80) && (mode == SEL_NONE)) {
#endif
        mark_undo();
        insert_char(c);
        update_after_insert_char();
        set_modified(1);
      }
    }
  }
}

void main(int argc, char *argv[]) {
#ifdef AUXMEM
  uint8_t *pp = (uint8_t*)0xbf98;
  if (!(*pp & 0x02)) {
    printf("Need 80 cols");
    return;
  }
  if ((*pp & 0x30) != 0x30) {
    printf("Need 128K");
    return;
  }
  disconnect_ramdisk();
  avail_aux_banks();
  init_aux_banks();
  getcwd(startdir, 80);
#endif
  if (argc == 2) {
    quit_to_email = 1;
    edit(argv[1]);
  } else {
    quit_to_email = 0;
    edit(NULL);
  }
}


