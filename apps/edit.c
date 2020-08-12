/////////////////////////////////////////////////////////////////////////////
// Simple text editor
// Supports Apple //e Auxiliary memory and RamWorks style expansions
// Bobbi July-Aug 2020
/////////////////////////////////////////////////////////////////////////////

// TODO: More refined multibank support. Either allow non-contiguous ranges or
//       at the very least check buffers are free when allocating them in
//       load_file(). Also need way to extend an existing file by editing it
//       to add an addional buffer. Think about workflow etc.
// TODO: Search options - ignore case, complete word.

// Note: Use my fork of cc65 to get a flashing cursor!!

#define AUXMEM               // Still somewhat experimental
#undef OLD_SELMODE           // Enable/disable Appleworks-style move/copy/del

#include <conio.h>
#include <ctype.h>
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

#define EOL       '\r'       // For ProDOS

#define BELL       0x07
#define BACKSPACE  0x08
#define RETURN     0x0d
#define ESC        0x1b
#define DELETE     0x7f

#ifdef AUXMEM
#define BUFSZ 0xb7fe             // Aux from 0x800 to 0xbfff, minus a pad byte
char     iobuf[IOSZ];            // Buffer for disk I/O
uint8_t  banktbl[1+8*16];        // Handles up to 8MB. Map of banks.
uint8_t  auxbank;                // Currently selected aux bank (physical)
uint8_t  l_auxbank;              // Currently selected aux bank (logical)
#else
#define BUFSZ (20480 - 1024)     // 19KB
char     gapbuf[BUFSZ];
char     padding = 0;            // To null terminate for strstr()
#endif

// The following fields, plus the gap buffer, represent the state of
// the current buffer.  These are stashed in each aux page from 0x200 up.
// Total 80 + 3 + 2 + 2 = 87 bytes
#define BUFINFOSZ 87
char     filename[80]  = "";
uint8_t  status[3] = {0, 0, 0};  // status[0] is 1 if file has been modified
                                 // status[1] is 1 if should warn for overwrite
                                 // status[2] is part # for multi-part files
uint16_t gapbegin      = 0;
uint16_t gapend        = BUFSZ - 1;

char    userentry[82] = "";  // Couple extra chars so we can store 80 col line
char    search[80]    = "";
char    replace[80]   = "";

uint8_t rowlen[NROWS];       // Number of chars on each row of screen

// Interface to read_char_update_pos()
uint8_t  do_print;
uint16_t pos, startsel, endsel;
uint8_t  row, col;

uint8_t cursrow, curscol; // Cursor position is kept here by draw_screen()

uint8_t  quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit

// The order of the cases matters! SRCH1/2/3 are not really a selection modes.
enum selmode {SEL_NONE, SEL_DEL2, SEL_COPY2, SEL_MOVE2,
              SEL_DEL, SEL_MOVE, SEL_COPY, SEL_SEL,
              SRCH1, SRCH2, SRCH3};
enum selmode mode;

// Mousetext Open-Apple
char openapple[] = "\x0f\x1b""A\x18\x0e";

// Mousetext Closed-Apple
char closedapple[] = "\x0f\x1b""@\x18\x0e";

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
    *(uint16_t*)(0xfc) = (uint16_t)0x0800 + src + n - 1; // Stuff src in ZP
    *(uint16_t*)(0xfe) = (uint16_t)0x0800 + dst + n - 1; // Stuff dst in ZP
#ifdef AUXMEM
    __asm__("lda %v", auxbank); 
    __asm__("sta $c073");  // Set aux bank
    __asm__("sta $c005"); // Write aux mem
    __asm__("sta $c003"); // Read aux mem
#endif
dl1:
    __asm__("lda ($fc)"); // *src
    __asm__("sta ($fe)"); // -> *dst

    __asm__("lda $fc");   // LSB of src
    __asm__("bne %g", ds1);
    __asm__("dec $fd");   // MSB of src
ds1:
    __asm__("dec $fc");   // LSB of src

    __asm__("lda $fe");   // LSB of dst
    __asm__("bne %g", ds2);
    __asm__("dec $ff");   // MSB of dst
ds2:
    __asm__("dec $fe");   // LSB of dst

    __asm__("lda $fa");   // LSB of n
    __asm__("bne %g", ds3);
    __asm__("dec $fb");   // MSB of n
ds3:
    __asm__("dec $fa");   // LSB of n

    __asm__("bne %g", dl1);    // Loop
    __asm__("lda $fb");   // MSB of n
    __asm__("bne %g", dl1);    // Loop
#ifdef AUXMEM
    __asm__("sta $c002"); // Read main mem
    __asm__("sta $c004"); // Write main mem
    __asm__("lda #$00");
    __asm__("sta $c073"); // Set aux bank back to 0
#endif
  } else {
    // Start with highest addr and copy upwards
    *(uint16_t*)(0xfa) = n;                              // Stuff sz in ZP
    *(uint16_t*)(0xfc) = (uint16_t)0x0800 + src;         // Stuff src in ZP
    *(uint16_t*)(0xfe) = (uint16_t)0x0800 + dst;         // Stuff dst in ZP
#ifdef AUXMEM
    __asm__("lda %v", auxbank); 
    __asm__("sta $c073");  // Set aux bank
    __asm__("sta $c005"); // Write aux mem
    __asm__("sta $c003"); // Read aux mem
#endif
al1:
    __asm__("lda ($fc)"); // *src
    __asm__("sta ($fe)"); // -> *dst

    __asm__("inc $fc");   // LSB of src
    __asm__("bne %g", as1);
    __asm__("inc $fd");   // MSB of src
as1:

    __asm__("inc $fe");   // LSB of dst
    __asm__("bne %g", as2);
    __asm__("inc $ff");   // MSB of dst
as2:

    __asm__("lda $fa");   // LSB of n
    __asm__("bne %g", as3);
    __asm__("dec $fb");   // MSB of n
as3:
    __asm__("dec $fa");   // LSB of n

    __asm__("bne %g", al1);    // Loop
    __asm__("lda $fb");   // MSB of n
    __asm__("bne %g", al1);    // Loop
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
  uint8_t i, l;

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
    cprintf("OA-? Help | [%03u] %c File:%s  %2uKB free",
            l_auxbank, status[0] ? '*' : ' ', filename, (FREESPACE() + 512) / 1024);
    l = 44 - strlen(filename);
    break;
#ifdef OLD_SELMODE
  case SEL_DEL:
    cprintf("Del%s", selmsg1);
    l = 80 - 42;
    break;
  case SEL_COPY:
    cprintf("Copy%s", selmsg1);
    l = 80 - 43;
    break;
  case SEL_MOVE:
    cprintf("Move%s", selmsg1);
    l = 80 - 43;
    break;
  case SEL_COPY2:
    cprintf("Copy%scopy", selmsg2);
    l = 80 - 41;
    break;
  case SEL_MOVE2:
    cprintf("Move%smove", selmsg2);
    l = 80 - 41;
    break;
#endif
  case SEL_SEL:
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
    cprintf("OA-? Help | [%03u] %c File:%s  %2uKB free | Not Found",
            l_auxbank, status[0] ? '*' : ' ', filename, (FREESPACE() + 512) / 1024);
    l = 44 - 12 - strlen(filename);
    break;
  }
  for (i = 0; i < l; ++i)
    cputc(' ');
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
  if (mode > SEL_MOVE2)
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
  uint8_t i;
  for (i = 1; i < 1+8*16; ++i)
    if (banktbl[i] == l - 1)
      return i - 1;
  show_error("Bad bank"); // Should never happen
}

/*
 * Change the active bank of aux memory
 * Current physical bank is in auxbank
 * logbank - desired logical bank
 */
#ifdef AUXMEM
#pragma code-name (push, "LC")
void change_aux_bank(uint8_t logbank) {
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
 */
void do_load_new_bank(uint8_t partnum) {
  strcpy(userentry, filename);
  status[2] = partnum;
  change_aux_bank(++l_auxbank); /// TODO: EXPERIMENT!!!
  strcpy(filename, userentry);
}

/*
 * Load a file from disk into the gapbuf
 * filename - name of file to load
 * replace - if 1, replace old file.
 * initialcol - initial screen column
 * Returns 0 on success
 *         1 if file can't be opened
 */
#pragma code-name (push, "LC")
uint8_t load_file(char *fname, uint8_t replace) {
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
    cputc('.');
    s = fread(p, 1, IOSZ, fp);
    cont = (s == IOSZ ? 1 : 0);
    for (i = 0; i < s; ++i) {
      switch (p[i]) {
      case '\r': // Native Apple2 files
      case '\n': // UNIX files
        set_gapbuf(gapbegin++, '\r');
        col = 0;
        if (replace && (FREESPACE() < 15000))
          do_load_new_bank(++partctr);
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
#ifdef AUXMEM
      // Will never happen in replace mode because
      // we will have already opened a new bank
      if (FREESPACE() < IOSZ * 2) {
        show_error("File truncated");
        goto done;
      }
#else
      if (FREESPACE() < IOSZ * 2) {
        show_error("File truncated");
        goto done;
      }
#endif
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
 * fname - name of file to load
 * copymode - if 1, copy test from startsel to endsel only
 * append - if 1, append to file instead of overwriting
 * Returns 0 on success
 *         1 if file can't be opened
 */
#ifndef AUXMEM
#pragma code-name (push, "LC")
#endif
uint8_t save_file(char *fname, uint8_t copymode, uint8_t append) {
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
  fp = fopen(fname, (append ? "a" : "w"));
  if (!fp)
    goto done;
  jump_pos(copymode == 1 ? startsel : 0);
  goto_prompt_row();
#ifdef AUXMEM
  p = gapend + 1;
#else
  p = gapbuf + gapend + 1;
#endif
  if (copymode)
    sz = endsel - startsel;
  else
    sz = DATASIZE();
  for (i = 0; i < sz / IOSZ; ++i) {
    cputc('.');
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
  cputc('.');
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

// Forward declaration
void draw_screen(void);

/*
 * Save a large file that spans multiple banks
 */
#ifdef AUXMEM
uint8_t save_multibank_file(char *fname) {
  uint8_t bank = find_first_bank();
  if (bank != l_auxbank) {
    change_aux_bank(bank);
    draw_screen();
  }
  bank = status[2];
  if (bank == 0) // Oh, it's actually just a single bank file
    return save_file(fname, 0, 0);
  if (bank != 1) {
    show_error("SBF error"); // Should never happen
    return 1;
  }
  if (save_file(fname, 0, 0) == 1)
    return 1;
  change_aux_bank(++l_auxbank);
  while (status[2] == bank + 1) {
    draw_screen();
    bank = status[2];
    if (save_file(fname, 0, 1) == 1) // Append
      return 1;
    change_aux_bank(++l_auxbank);
  }
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
  char c;
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
  if (((pos > startsel) && (pos <= endsel)) || // Left of cursor 
      ((pos - delta > endsel) && (pos - delta <= startsel + 1)))   // Right of cursor
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
  // NROWS * NCOLS chars.
  startpos = gapbegin;
  if (startpos > NROWS * NCOLS)
    startpos -= NROWS * NCOLS;
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
  if (mode > SEL_MOVE2) {
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
  if (gapend == BUFSZ - 1)
    goto done;
  if (curscol == rowlen[cursrow]) {
    if (cursrow == NROWS - 1)
      scroll_down();
    ++cursrow;
    curscol = 0;
  }
done:
  if (mode > SEL_MOVE2) {
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
    if (mode > SEL_MOVE2) {
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
    if (mode > SEL_MOVE2) {
      gotoxy(i - 1, cursrow);
      revers(gapbegin < startsel ? 1 : 0);
      cputc(get_gapbuf(gapbegin));
    }
  }
  if (mode > SEL_MOVE2) {
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

  for (i = 0; i < rowlen[cursrow] - curscol; ++i) {
    if (gapend < BUFSZ - 1) {
      set_gapbuf(gapbegin++, get_gapbuf(++gapend));
      if (mode > SEL_MOVE2) {
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
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  gotoxy(0, cursrow);
  for (i = 0; i < curscol; ++i) {
    if (gapend < BUFSZ - 1) {
      set_gapbuf(gapbegin++, get_gapbuf(++gapend));
      if (mode > SEL_MOVE2) {
        revers(gapbegin > startsel ? 1 : 0);
        cputc(get_gapbuf(gapbegin - 1));
      }
    }
  }
  if (mode > SEL_MOVE2) {
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
void help(void) {
  FILE *fp = fopen("EDITHELP.TXT", "rb");
  char *p;
  char c;
  uint16_t i, s;
  uint8_t cont;
  revers(0);
  cursor(0);
  clrscr();
  if (!fp) {
    printf("Can't open EDITHELP.TXT\n\n");
    goto done;
  }
#ifdef AUXMEM
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
  cgetc();
  clrscr();
}

/*
 * Load EMAIL.SYSTEM to $2000 and jump to it
 */
void load_email(void) {
  revers(0);
  clrscr();
  exec("EMAIL.SYSTEM", NULL); // Assume it is in current directory
}

/*
 * Load ATTACHER.SYSTEM to $2000 and jump to it
 */
void load_attacher(void) {
  revers(0);
  clrscr();
  exec("ATTACHER.SYSTEM", filename); // Assume it is in current directory
}

/*
 * Save file to disk, handle user interface
 */
void save(void) {
  uint8_t rc;
  FILE *fp;
  if (strlen(filename) == 0) {
    status[1] = 1; // Prompt if save will overwrite existing file
    sprintf(userentry, "[%03u] *UNSAVED CHANGES* File to save", l_auxbank);
    if (prompt_for_name(userentry, 1) == 255)
      return; // If ESC pressed
    if (strlen(userentry) == 0)
      return;
    strcpy(filename, userentry);
  }
  // If status[1] is set, check for overwrite
  if (status[1]) {
    fp = fopen(filename, "r");
    if (fp) {
      fclose(fp);
      sprintf(userentry, "File '%s' exists, overwrite");
      if (prompt_okay(userentry) != 0)
        return; 
    }
    fclose(fp);
  }
#ifdef AUXMEM
  rc = save_multibank_file(filename);
#else
  rc = save_file(filename, 0, 0);
#endif
  switch (rc) {
  case 0: // Success
    status[1] = 0; // No need to prompt for overwrite next time
    set_modified(0);
    break;
  case 1: // Save error
    sprintf(userentry, "Can't save '%s'", filename);
    show_error(userentry);
    draw_screen();
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
  revers(1);
  cprintf("EDIT.SYSTEM                   Bobbi 2020");
  revers(0);
  cprintf("\n\n\n%u x 64KB aux banks -> %uKB\n", banktbl[0], banktbl[0]*64);
  for (i = 1; i < banktbl[0]; ++i) {
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
  for (i = 1; i < banktbl[0]; ++i) {
    if (row == 0) {
      clrscr();
      cprintf(" Buf  Size   Mod  Part  Filename\r");
      gotoxy(0, ++row);
    }
    change_aux_bank(i);
    cprintf("[%03u] %05u | %c | %3u | %s\r",
            i, DATASIZE(), (status[0] ? '*' : ' '), status[2], filename);
    if (DATASIZE() > 0)
      gotoxy(0, ++row);
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
 * Save all modified buffers
 */
#ifdef AUXMEM
void save_all(void) {
  uint8_t o_aux = l_auxbank;
  uint8_t i;
  cursor(0);
  for (i = 1; i < banktbl[0]; ++i) {
    change_aux_bank(i);
    if (status[0]) { // If buffer is modified
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
 * Main editor routine
 */
int edit(char *fname) {
  char c; 
  uint16_t pos, tmp;
  uint8_t i, ask;
  videomode(VIDEOMODE_80COL);
  if (fname) {
    strcpy(filename, fname);
    cprintf("Loading file %s ", filename);
    if (load_file(filename, 1)) {
      sprintf(userentry, "Can't load '%s'", filename);
      show_error(userentry);
      strcpy(filename, "");
    }
  }
  jump_pos(0);
  pos = 0;
  set_modified(0);
  startsel = endsel = 65535U;
  mode = SEL_NONE;
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
        mode = SEL_SEL;
        update_status_line();
      }
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
      if (save_file("CLIPBOARD", 1, 0) == 1) {
        show_error("Can't save CLIPBOARD");
        draw_screen();
        break;
      }
      if (tmp == 0) {
        jump_pos(startsel);
        gapend += (endsel - startsel);
        set_modified(1);
      }
      startsel = endsel = 65535U;
      draw_screen();
      break;
    case 0x80 + 'V': // OA-V "Paste"
    case 0x80 + 'v': // OA-v
    case 0x16:       // ^V "Paste"
      mode = SEL_NONE;
      if (load_file("CLIPBOARD", 0))
        show_error("Can't open CLIPBOARD");
      startsel = endsel = 65535U;
      draw_screen();
      break;
#ifdef OLD_SELMODE
    case 0x80 + 'C': // OA-C "Copy"
    case 0x80 + 'c': // OA-c
      mode = SEL_COPY;
      tmp = (startsel == 65535U ? 0 : 1); // Prev selection active?
      endsel = startsel = gapbegin;
      if (tmp)
        draw_screen();
      else
        update_status_line();
      break;
#endif
#ifdef OLD_SELMODE
    case 0x80 + 'D': // OA-D "Delete"
    case 0x80 + 'd': // OA-d
      mode = SEL_DEL;
      tmp = (startsel == 65535U ? 0 : 1); // Prev selection active?
      endsel = startsel = gapbegin;
      if (tmp)
        draw_screen();
      else
        update_status_line();
      break;
#endif
    case 0x80 + 'R': // OA-R "Replace"
    case 0x80 + 'r': // OA-r
      tmp = 65535U;
    case 0x80 + 'F': // OA-F "Find"
    case 0x80 + 'f': // OA-F "Find"
      ++tmp;
      sprintf(userentry, "Find (%s)", search);
      if (prompt_for_name(userentry, 0) == 255)
        break; // ESC pressed
      if (strlen(userentry) > 0)
        strcpy(search, userentry);
      else if (strlen(search) == 0)
        break;
      cursor_right();
      if (tmp == 0) { // Replace mode
        sprintf(userentry, "Replace (%s)", replace);
        if (prompt_for_name(userentry, 0) == 255)
          break; // ESC pressed
        if (strlen(userentry) > 0)
          strcpy(replace, userentry);
        else if (strlen(replace) == 0)
          break;
        ask = 0;
        if (prompt_okay("Ask for each") == 0)
          ask = 1;
      }
      do_search_replace(tmp, ask);
      update_status_line();
      break;
    case 0x80 + 'I': // OA-I "Insert file"
    case 0x80 + 'i':
      if (prompt_for_name("File to insert", 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      if (load_file(userentry, 0))
        show_error("Can't open");
      draw_screen();
      break;
    case 0x80 + 'L': // OA-L "Load"
    case 0x80 + 'l':
      if (status[0])
        save();
      if (prompt_for_name("File to load", 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      jump_pos(0);
      pos = 0;
      set_modified(0);
      startsel = endsel = 65535U;
      mode = SEL_NONE;
      gapbegin = 0;
      gapend = BUFSZ - 1;
      strcpy(filename, userentry);
      if (load_file(filename, 1)) {
        sprintf(userentry, "Can't load '%s'", filename);
        show_error(userentry);
        strcpy(filename, "");
      }
      draw_screen();
      break;
#ifdef OLD_SELMODE
    case 0x80 + 'M': // OA-M "Move"
    case 0x80 + 'm': // OA-m
      mode = SEL_MOVE;
      tmp = (startsel == 65535U ? 0 : 1); // Prev selection active?
      endsel = startsel = gapbegin;
      if (tmp)
        draw_screen();
      else
        update_status_line();
      break;
#endif
    case 0x80 + 'N': // OA-N "Name"
    case 0x80 + 'n': // OA-n
      if (prompt_for_name("New filename", 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      strcpy(filename, userentry);
      status[1] = 1; // Should prompt if overwriting file on save
      update_status_line();
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
      word_wrap_para(0);
      draw_screen();
      break;
    case 0x80 + 'W': // OA-W "Wrap"
    case 0x80 + 'w': // OA-w
      word_wrap_para(1);
      draw_screen();
      break;
    case 0x80 + 'S': // OA-S "Save"
    case 0x80 + 's': // OA-s
      save();
      draw_screen();
      break;
    case 0x80 + 'A': // OA-A "Save All"
    case 0x80 + 'a': // OA-a
      save_all();
      draw_screen();
      break;
    case 0x80 + DELETE: // OA-Backspace
    case 0x04:  // Ctrl-D "DELETE"
      if (mode == SEL_NONE) {
        delete_char_right();
        update_after_delete_char_right();
        set_modified(1);
      }
      break;
    case 0x80 + '?': // OA-? "Help"
      help();
      draw_screen();
      break;
    case 0x0c:  // Ctrl-L "REFRESH"
      draw_screen();
      break;
    case DELETE:  // DEL "BACKSPACE"
      if (mode == SEL_NONE) {
        delete_char();
        update_after_delete_char();
        set_modified(1);
      } else if (mode == SEL_SEL) {
        tmp = (startsel == 65535U ? 0 : 1); // Selection active?
        if (!tmp) {
          beep();
          break;
        }
        sprintf(userentry, "Delete selection (%d chars)", abs(endsel - startsel));
        if (prompt_okay(userentry) != 0)
          break;
        order_selection();
        mode = SEL_NONE;
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
      if ((mode == SEL_NONE) || (mode == SEL_SEL)) {
        insert_char(c);
        update_after_insert_char();
        set_modified(1);
      }
#ifdef OLD_SELMODE
      else {
        order_selection();
        switch (mode) {
        case SEL_DEL:
          mode = SEL_DEL2;
          if (prompt_okay("Delete selection") == 0) {
            jump_pos(startsel);
            gapend += (endsel - startsel);
          }
          startsel = endsel = 65535U;
          mode = SEL_NONE;
          set_modified(1);
          draw_screen();
          break;
        case SEL_COPY:
          mode = SEL_COPY2;
          update_status_line();
          break;
        case SEL_MOVE:
          mode = SEL_MOVE2;
          update_status_line();
          break;
        case SEL_COPY2:
        case SEL_MOVE2:
          if ((gapbegin > startsel) && (gapbegin < endsel)) {
            show_error("Bad destination");
            goto copymove2_cleanup;
          }
          if ((endsel - startsel) > FREESPACE()) {
            show_error("No space");
            goto copymove2_cleanup;
          }
          sprintf(userentry, "%s selection", mode == SEL_COPY2 ? "Copy" : "Move");
          if (prompt_okay(userentry) == 0) {
            if (gapbegin >= endsel)
              move_in_gapbuf(gapbegin, startsel, endsel - startsel);
            else
              move_in_gapbuf(gapbegin,
                             gapend + startsel - gapbegin + 1,
                             endsel - startsel);
            gapbegin += (endsel - startsel);
            if (mode == SEL_MOVE2) {
              jump_pos((gapbegin >= endsel) ? startsel : endsel);
              gapend += (endsel - startsel);
            }
          }
copymove2_cleanup:
          startsel = endsel = 65535U;
          mode = SEL_NONE;
          set_modified(1);
          draw_screen();
          break;
        }
      }
#endif
      break;
    default:
#ifdef AUXMEM
      if (*(uint8_t*)0xc062 & 0x80) { // Closed Apple depressed
        // CA-number (1-9) - quick jump to first 9 buffers
        if ((c >= '1') && (c <= '9')) {
          change_aux_bank(c - '0');
          startsel = endsel = 65535U;
          draw_screen();
          break;
        } else if ((c == 'B') || (c == 'b')) { // CA-B "Buffer"
          sprintf(userentry, "Buffer # (1-%u)", banktbl[0]);
          if (prompt_for_name(userentry, 0) == 255)
            break;
          if (strlen(userentry) == 0)
            break;
          tmp = atoi(userentry);
          if ((tmp < 1) || (tmp >= banktbl[0])) {
            beep();
            break;
          }
          change_aux_bank(tmp);
          startsel = endsel = 65535U;
          draw_screen();
          break;
        } else if ((c == 'L') || (c == 'l')) {
          buffer_list();
          draw_screen();
          break;
        }
#endif
      }
      //printf("**%02x**", c);
      if ((c >= 0x20) && (c < 0x80) && (mode == SEL_NONE)) {
        insert_char(c);
        update_after_insert_char();
        set_modified(1);
      }
    }
  }
}

void main(int argc, char *argv[]) {
#ifdef AUXMEM
  char *pad = (char*)0xbfff;
  *pad = '\0'; // Null termination for strstr()
  avail_aux_banks();
  init_aux_banks();
#endif
  if (argc == 2) {
    quit_to_email = 1;
    edit(argv[1]);
  } else {
    quit_to_email = 0;
    edit(NULL);
  }
}


