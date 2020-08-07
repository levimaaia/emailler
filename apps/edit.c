/////////////////////////////////////////////////////////////////////////////
// Simple text editor
// Bobbi July 2020
/////////////////////////////////////////////////////////////////////////////

// Note: Use my fork of cc65 to get a flashing cursor!!

// TODO: Finish off auxmem support. See TODOs below

#define AUXMEM               // HIGHLY EXPERIMENTAL

#include <conio.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NCOLS      80        // Width of editing screen
#define NROWS      22        // Height of editing screen
#define CURSORROW  10        // Row cursor is initially shown on (if enough text)
#define PROMPT_ROW NROWS + 1 // Row where input prompt is shown
#define IOSZ       1024      // Size of chunks to use when loading/saving file

#define EOL       '\r'       // For ProDOS

#define BELL       0x07
#define BACKSPACE  0x08
#define RETURN     0x0d
#define ESC        0x1b
#define DELETE     0x7f

#ifdef AUXMEM
#define BUFSZ 0xb7fe             // All of aux from 0x800 to 0xbfff, minus a byte
char     iobuf[IOSZ];            // Buffer for disk I/O
#else
#define BUFSZ (20480 - 1024)     // 19KB
char     gapbuf[BUFSZ];
char     padding = 0;            // To null terminate for strstr()
#endif

uint16_t gapbegin = 0;
uint16_t gapend = BUFSZ - 1;

uint8_t rowlen[NROWS];       // Number of chars on each row of screen

char    filename[80]  = "";
char    userentry[82] = "";  // Couple extra chars so we can store 80 col line
char    search[80]    = "";
char    replace[80]   = "";

// Interface to read_char_update_pos()
uint8_t  do_print, modified;
uint16_t pos, startsel, endsel;
uint8_t  row, col;

uint8_t cursrow, curscol; // Cursor position is kept here by draw_screen()

uint8_t  quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit
uint8_t  modified;        // If 1, file contents have been modified

// The order of the cases matters! SRCH1/2/3 are not really a selection modes.
enum selmode {SEL_NONE, SEL_DEL2, SEL_COPY2, SEL_MOVE2, SEL_DEL, SEL_MOVE, SEL_COPY,
              SRCH1, SRCH2, SRCH3};
enum selmode mode;

// Mousetext Open-Apple
char openapple[] = "\x0f\x1b""A\x18\x0e";

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
 */
#pragma code-name (push, "LC")
char get_gapbuf(uint16_t i) {
#ifdef AUXMEM
  *(uint16_t*)(0xfe) = (uint16_t)0x0800 + i; // Stuff address in Zero Page
  __asm__("sta $c003"); // Read aux mem
  __asm__("lda ($fe)");
  __asm__("sta $ff");
  __asm__("sta $c002"); // Read main mem
  return *(uint8_t*)(0xff);
#else
  return gapbuf[i];
#endif
}
#pragma code-name (pop)

/*
 * Set one byte in the gapbuf[] in aux memory
 * Must be in LC
 */
#pragma code-name (push, "LC")
void set_gapbuf(uint16_t i, char c) {
#ifdef AUXMEM
  __asm__("sta $c005"); // Write aux mem
  (*(char*)((char*)0x0800 + i)) = c;
  __asm__("sta $c004"); // Write main mem
#else
  gapbuf[i] = c;
#endif
}
#pragma code-name (pop)

/*
 * Do a memmove() on aux memory. Uses indices into gapbuf[].
 * Must be in LC
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
#endif
  } else {
    // Start with highest addr and copy upwards
    *(uint16_t*)(0xfa) = n;                              // Stuff sz in ZP
    *(uint16_t*)(0xfc) = (uint16_t)0x0800 + src;         // Stuff src in ZP
    *(uint16_t*)(0xfe) = (uint16_t)0x0800 + dst;         // Stuff dst in ZP
    __asm__("sta $c005"); // Write aux mem
    __asm__("sta $c003"); // Read aux mem
al1:
    __asm__("lda ($fc)"); // *src           // d517
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
#endif
  }
#else
  memmove(gapbuf + dst, gapbuf + src, n);
#endif
}
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
    cprintf("OA-? Help | %c File:%s  %2uKB free",
            modified ? '*' : ' ', filename, (FREESPACE() + 512) / 1024);
    l = 50 - strlen(filename);
    break;
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
  case SRCH1:
    cprintf("Searching ...");
    l = 80 - 13;
    break;
  case SRCH2:
    cprintf("Searching - wrapped ...");
    l = 80 - 23;
    break;
  case SRCH3:
    cprintf("OA-? Help | %c File:%s  %2uKB free | Not Found",
            modified ? '*' : ' ', filename, (FREESPACE() + 512) / 1024);
    l = 50 - 12 - strlen(filename);
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
  if (modified == mod)
    return;
  modified = mod;
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
 */
char prompt_okay(char *msg) {
  char c;
  cursor(0);
  goto_prompt_row();
  clreol();
  revers(1);
  cprintf("%sSure? (y/n)", msg);
  revers(0);
  while (1) {
    c = cgetc();
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N') || c == ESC)
      break;
    beep();
  }
  if ((c == 'y') || (c == 'Y'))
    c = 1;
  else
    c = 0;
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
#pragma code-name (push, "LC")
void delete_char(void) {
  if (gapbegin == 0) {
    beep();
    return;
  }
  --gapbegin;
}
#pragma code-name (pop)

/*
 * Delete the character to the right of the current position
 */
#pragma code-name (push, "LC")
void delete_char_right(void) {
  if (gapend == BUFSZ - 1) {
    beep();
    return;
  }
  ++gapend;
}
#pragma code-name (pop)

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
#pragma code-name (push, "LC")
uint8_t next_tabstop(uint8_t col) {
  return (col / 8) * 8 + 8;
}
#pragma code-name (pop)

/*
 * Load a file from disk into the gapbuf
 * filename - name of file to load
 * replace - if 1, replace old file.
 * initialcol - initial screen column
 * Returns 0 on success
 *         1 if file can't be opened
 */
#pragma code-name (push, "LC")
uint8_t load_file(char *filename, uint8_t replace) {
  uint8_t col;
  char *p;
  uint16_t i, j, s;
  uint8_t c, cont;
  FILE *fp = fopen(filename, "r");
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
    if (FREESPACE() < IOSZ * 2) {
      show_error("File truncated");
      goto done;
    }
    cputc('.');
    s = fread(p, 1, IOSZ, fp);
    cont = (s == IOSZ ? 1 : 0);
    for (i = 0; i < s; ++i) {
      switch (p[i]) {
      case '\r': // Native Apple2 files
      case '\n': // UNIX files
        set_gapbuf(gapbegin++, '\r');
        col = 0;
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
  --gapbegin; // Eat EOF character
done:
  fclose(fp);
  if (replace) {
    jump_pos(0);
    pos = 0;
    set_modified(0);
    startsel = endsel = 65535U;
    mode = SEL_NONE;
  }
  return 0;
}
#pragma code-name (pop)

/*
 * Save gapbuf to file
 * filename - name of file to load
 * Returns 0 on success
 *         1 if file can't be opened
 */
#ifndef AUXMEM
#pragma code-name (push, "LC")
#endif
uint8_t save_file(char *filename) {
  uint16_t pos = gapbegin;
  uint8_t retval = 1;
  char *p;
  uint8_t i;
  uint16_t j;
  FILE *fp;
  _filetype = PRODOS_T_TXT;
  _auxtype = 0;
  fp = fopen(filename, "w");
  if (!fp)
    goto done;
  jump_pos(0);
  goto_prompt_row();
#ifdef AUXMEM
  p = gapend + 1;
#else
  p = gapbuf + gapend + 1;
#endif
  for (i = 0; i < DATASIZE() / IOSZ; ++i) {
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
  for (j = 0; j < DATASIZE() - (IOSZ * i); ++j)
    iobuf[j] = get_gapbuf(p++);
  if (fwrite(iobuf, DATASIZE() - (IOSZ * i), 1, fp) != 1)
    goto done;
#else
  if (fwrite(p, DATASIZE() - (IOSZ * i), 1, fp) != 1)
    goto done;
#endif
  retval = 0;
done:
  fclose(fp);
  jump_pos(pos);
  return 0;
}
#ifndef AUXMEM
#pragma code-name (pop)
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
#pragma code-name (push, "LC")
void goto_bol(void) {
  while (curscol > 0)
    cursor_left();  
}
#pragma code-name (pop)

/*
 * Goto end of line
 */
#pragma code-name (push, "LC")
void goto_eol(void) {
  while (curscol < rowlen[cursrow] - 1)
    cursor_right();  
}
#pragma code-name (pop)

/*
 * Word left
 */
#pragma code-name (push, "LC")
void word_left(void) {
  do {
    cursor_left();
  } while ((get_gapbuf(gapbegin) != ' ') && (get_gapbuf(gapbegin) != EOL) &&
           (gapbegin > 0));
}
#pragma code-name (pop)

/*
 * Word right
 */
#pragma code-name (push, "LC")
void word_right(void) {
  do {
    cursor_right();
  } while ((get_gapbuf(gapbegin) != ' ') && (get_gapbuf(gapbegin) != EOL) &&
           (gapend < BUFSZ - 1));
}
#pragma code-name (pop)

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
#pragma code-name (push, "LC")
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
      if (c == '@')
        printf("%s", openapple);
      else if ((c != '\r') && (c != '\n'))
        putchar(c);
    }
  } while (cont);
done:
  fclose(fp);
  printf("[Press Any Key]");
  cgetc();
  clrscr();
}
#pragma code-name (pop)

/*
 * Load EMAIL.SYSTEM to $2000 and jump to it
 * (This code is in language card space so it can't possibly be trashed)
 */
#pragma code-name (push, "LC")
void load_email(void) {
  revers(0);
  clrscr();
  exec("EMAIL.SYSTEM", NULL); // Assume it is in current directory
}
#pragma code-name (pop)

/*
 * Load ATTACHER.SYSTEM to $2000 and jump to it
 * (This code is in language card space so it can't possibly be trashed)
 */
#pragma code-name (push, "LC")
void load_attacher(void) {
  revers(0);
  clrscr();
  exec("ATTACHER.SYSTEM", filename); // Assume it is in current directory
}
#pragma code-name (pop)

/*
 * Save file to disk, handle user interface
 */
void save(void) {
  if (strlen(filename) == 0) {
    if (prompt_for_name("File to save", 1) == 255)
      return; // If ESC pressed
    if (strlen(userentry) == 0)
      return;
    strcpy(filename, userentry);
  }
  sprintf(userentry, "Save to %s - ", filename);
  if (prompt_okay(userentry)) {
    if (save_file(filename)) {
      sprintf(userentry, "%cCan't save %s", filename);
      show_error(userentry);
      draw_screen();
    } else {
      set_modified(0);
    }
  }
}

/*
 * Main editor routine
 */
int edit(char *fname) {
  char c, *p;
  uint16_t pos, tmp;
  uint8_t i;
  videomode(VIDEOMODE_80COL);
  if (fname) {
    strcpy(filename, fname);
    cprintf("Loading file %s ", filename);
    if (load_file(filename, 1)) {
      sprintf(userentry, "Can't load %s", filename);
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
      goto_bol();
      break;
    case 0x80 + '.':  // OA-> "End"
    case 0x80 + '>':
      goto_eol();
      break;
    case 0x8b:  // OA-Up "Page Up"
      page_up();
      break;
    case 0x8a:  // OA-Down "Page Down"
      page_down();
      break;
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
      else {
        if (strlen(search) == 0)
          break;
        sprintf(userentry, "Search for '%s' - ", search);
        if (!prompt_okay(userentry))
          break;
        cursor_right();
      }
      if (tmp == 0) { // Replace mode
        sprintf(userentry, "Replace (%s)", replace);
        if (prompt_for_name(userentry, 0) == 255)
          break; // ESC pressed
        if (strlen(userentry) > 0)
          strcpy(replace, userentry);
        else {
          if (strlen(replace) == 0)
            break;
          sprintf(userentry, "Replace with '%s' - ", replace);
          if (!prompt_okay(userentry))
            break;
        }
      }
      mode = SRCH1;
      update_status_line();
#ifdef AUXMEM
      // TODO
#else
      p = strstr(gapbuf + gapend + 1, search);
#endif
      if (!p) {
        mode = SRCH2;
        update_status_line();
        set_gapbuf(gapbegin, '\0');
#ifdef AUXMEM
        // TODO
#else
        p = strstr(gapbuf, search);
#endif
        mode = SEL_NONE;
        if (!p) {
          mode = SRCH3;
          update_status_line();
          break;
        }
#ifdef AUXMEM
        // TODO
#else
        jump_pos(p - gapbuf);
#endif
        if (tmp == 0) { // Replace mode
          for (i = 0; i < strlen(search); ++i)
            delete_char_right();
#ifdef AUXMEM
          // TODO
#else
          memcpy(gapbuf + gapbegin, replace, strlen(userentry));
          gapbegin += strlen(replace);
#endif
        }
        draw_screen();
        break;
      }
      mode = SEL_NONE;
#ifdef AUXMEM
      // TODO
#else
      jump_pos(gapbegin + p - (gapbuf + gapend + 1));
#endif
      if (tmp == 0) { // Replace mode
        for (i = 0; i < strlen(search); ++i)
          delete_char_right();
#ifdef AUXMEM
#else
        memcpy(gapbuf + gapbegin, replace, strlen(userentry));
        gapbegin += strlen(replace);
#endif
      }
      draw_screen();
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
        sprintf(userentry, "Can't load %s", filename);
        show_error(userentry);
        strcpy(filename, "");
      }
      draw_screen();
      break;
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
    case 0x80 + 'N': // OA-N "Name"
    case 0x80 + 'n': // OA-n
      if (prompt_for_name("New filename", 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      strcpy(filename, userentry);
      update_status_line();
      break;
    case 0x80 + 'Q': // OA-Q "Quit"
    case 0x80 + 'q': // OA-q
      if (modified)
        save();
      if (quit_to_email) {
        if (prompt_okay("Add attachments - "))
          load_attacher();
        if (prompt_okay("Quit to EMAIL - "))
          load_email();
      } else {
        if (prompt_okay("Quit to ProDOS - ")) {
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
      if (mode == SEL_NONE) {
        insert_char(c);
        update_after_insert_char();
        set_modified(1);
      } else {
        if (startsel > endsel) {
          tmp = endsel;
          endsel = startsel;
          startsel = tmp;
        }
        switch (mode) {
        case SEL_DEL:
          mode = SEL_DEL2;
          if (prompt_okay("Delete selection - ")) {
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
          sprintf(userentry, "%s selection - ", mode == SEL_COPY2 ? "Copy" : "Move");
          if (prompt_okay(userentry)) {
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
      break;
    default:
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
  char *pad = 0xbfff;
  *pad = '\0'; // Null termination for strstr()
#endif
  if (argc == 2) {
    quit_to_email = 1;
    edit(argv[1]);
  } else {
    quit_to_email = 0;
    edit(NULL);
  }
}


