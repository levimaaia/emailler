/////////////////////////////////////////////////////////////////////////////
// Prototype of text editor
// Bobbi July 2020
/////////////////////////////////////////////////////////////////////////////

// Note: Use my fork of cc65 to get a flashing cursor!!

// TODO: There is a bug whereby the rowlen[] values seem to get corrupted
// TODO: Add word wrap feature (OA-W ?)
// TODO: Improve insert file feature to not change filename
// TODO: Improve status line, refresh it properly
// TODO: Minor bug - can delete too many chars from status line
// TODO: Should be smarter about redrawing when updating selection!!!
// TODO: Doesn't check for error cases when calling gap buffer functions
// TODO: Make use of aux mem

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <conio.h>
#include <ctype.h>
#include <peekpoke.h>

#define NCOLS      80        // Width of editing screen
#define NROWS      22        // Height of editing screen
#define CURSORROW  10        // Row cursor is initially shown on (if enough text)
#define PROMPT_ROW NROWS + 1 // Row where input prompt is shown

#define EOL       '\r'       // For ProDOS

#define BELL       0x07
#define BACKSPACE  0x08
#define CURDOWN    0x0a
#define RETURN     0x0d
#define NOMOUSETXT 0x1b
#define HOME       0x19
#define CLRLINE    0x1a
#define ESC        0x1b
#define MOUSETXT   0x1b
#define CLREOL     0x1d
#define DELETE     0x7f

#define BUFSZ (41 * 512)     // 20.5KB
char     gapbuf[BUFSZ];
char     padding = 0;        // To null terminate for strstr()
uint16_t gapbegin = 0;
uint16_t gapend = BUFSZ - 1;

uint8_t rowlen[NROWS];       // Number of chars on each row of screen

char    filename[80]  = "";
char    userentry[80] = "";
char    search[80]    = "";
char    replace[80]   = "";

// Interface to read_char_update_pos()
uint8_t  do_print, modified;
uint16_t pos, startsel, endsel;
uint8_t  row, col;

uint8_t cursrow, curscol; // Cursor position is kept here by draw_screen()

uint8_t  quit_to_email;   // If 1, launch EMAIL.SYSTEM on quit
uint8_t  modified;        // If 1, file contents have been modified

// The order of the cases matters!
enum selmode {SEL_NONE, SEL_COPY2, SEL_MOVE2, SEL_DEL, SEL_MOVE, SEL_COPY}; 
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
 * Annoying beep
 */
#pragma code-name (push, "LC")
void beep(void) {
  putchar(BELL);
}
#pragma code-name (pop)

/*
 * Clear to EOL
 */
#pragma code-name (push, "LC")
void clreol(void) {
  uint8_t x = wherex(), y = wherey();
  cclear(80 - x);
  gotoxy(x, y);
}

#pragma code-name (pop)
/*
 * Clear to EOL, wrap to next line
 */
#pragma code-name (push, "LC")
void clreol_wrap(void) {
  uint8_t x = wherex();
  cclear(80 - x);
  gotox(x);
}
#pragma code-name (pop)

/*
 * Clear line
 */
#pragma code-name (push, "LC")
void clrline(void) {
  uint8_t x = wherex();
  gotox(0);
  cclear(80);
  gotox(x);
}
#pragma code-name (pop)

/*
 * Put cursor at beginning of PROMPT_ROW
 */
#pragma code-name (push, "LC")
void goto_prompt_row(void) {
  gotoxy(0, PROMPT_ROW);
}
#pragma code-name (pop)

/*
 * Prompt for a name in the bottom line of the screen
 * Returns number of chars read.
 * prompt - Message to display before > prompt
 * is_file - if 1, restrict chars to those allowed in ProDOS filename
 * Returns number of chars read, or 255 if ESC pressed
 */
#pragma code-name (push, "LC")
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
#pragma code-name (pop)

/*
 * Prompt ok?
 */
#pragma code-name (push, "LC")
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
    if ((c == 'y') || (c == 'Y') || (c == 'n') || (c == 'N'))
      break;
    beep();
  }
  if ((c == 'y') || (c == 'Y'))
    c = 1;
  else
    c = 0;
  clrline();
  gotoxy(curscol, cursrow);
  cursor(1);
  return c;
}
#pragma code-name (pop)

/*
 * Error message
 */
#pragma code-name (push, "LC")
void show_error(char *msg) {
  cursor(0);
  goto_prompt_row();
  beep();
  revers(1);
  cprintf("%s [Press Any Key]", msg);
  revers(0);
  cgetc();
  clrline();
  gotoxy(curscol, cursrow);
  cursor(1);
}
#pragma code-name (pop)

/*
 * Info message
 */
#pragma code-name (push, "LC")
void show_info(char *msg) {
  cursor(0);
  goto_prompt_row();
  revers(1);
  cprintf("%s", msg);
  revers(0);
  gotoxy(curscol, cursrow);
  cursor(1);
}
#pragma code-name (pop)

/*
 * Insert a character into gapbuf at current position
 * c - character to insert
 * Returns 0 on success, 1 on failure (insufficient space)
 */
#pragma code-name (push, "LC")
uint8_t insert_char(char c) {
  if (FREESPACE()) {
    gapbuf[gapbegin++] = c;
    return 0;
  }
  return 1;
}
#pragma code-name (pop)

/*
 * Delete the character to the left of the current position
 * Returns 0 on success, 1 on failure (nothing to delete)
 */
#pragma code-name (push, "LC")
uint8_t delete_char(void) {
  if (gapbegin == 0)
    return 1;
  --gapbegin;
}
#pragma code-name (pop)

/*
 * Delete the character to the right of the current position
 * Returns 0 on success, 1 on failure (nothing to delete)
 */
#pragma code-name (push, "LC")
uint8_t delete_char_right(void) {
  if (gapend == BUFSZ - 1)
    return 1;
  ++gapend;
}
#pragma code-name (pop)

/*
 * Obtain the next character (to the right of the current position)
 * and advance the position.
 * c - character is returned throught this pointer
 * Returns 0 on success, 1 if at end of the buffer.
 */
#pragma code-name (push, "LC")
uint8_t get_char(char *c) {
  if (gapend == BUFSZ - 1)
    return 1;
  *c = gapbuf[gapbegin++] = gapbuf[++gapend];
  return 0;
}
#pragma code-name (pop)

/*
 * Move the current position
 * pos - position to which to move
 * Returns 0 on success, 1 if pos is invalid
 */
#pragma code-name (push, "LC")
uint8_t jump_pos(uint16_t pos) {
  if (pos > BUFSZ - 1)
    return 1;
  if (pos == GETPOS())
    return 0;
  if (pos > GETPOS())
    do {
      gapbuf[gapbegin++] = gapbuf[++gapend];
    } while (pos > GETPOS());
  else
    do {
      gapbuf[gapend--] = gapbuf[--gapbegin];
    } while (pos < GETPOS());
  return 0;
}
#pragma code-name (pop)

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
 * Returns 0 on success
 *         1 if file can't be opened
 */
#pragma code-name (push, "LC")
uint8_t load_file(char *filename, uint8_t replace) {
  char c;
  uint8_t i;
  FILE *fp = fopen(filename, "r");
  if (!fp)
    return 1;
  goto_prompt_row();
  if (replace) {
    gapbegin = 0;
    gapend = BUFSZ - 1;
    col = 0;
  }
  while (!feof(fp)) {
    c = fgetc(fp);
    switch (c) {
    case '\r': // Native Apple2 files
    case '\n': // UNIX files
      col = 0;
      gapbuf[gapbegin++] = '\r';
      break;
    case '\t':
      c = next_tabstop(col) - col;
      for (i = 0; i < c; ++i)
        gapbuf[gapbegin++] = ' ';
      col += c;
      break;
    default:
      ++col;
      gapbuf[gapbegin++] = c;      
    }
    if (FREESPACE() < 1000) {
      fclose(fp);
      show_error("File truncated");
      return 0;
    }
    if ((gapbegin % 1000) == 0)
      cputc('.');
  }
  --gapbegin; // Eat EOF character
  fclose(fp);
  if (replace) {
    jump_pos(0);
    pos = 0;
    modified = 0;
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
#pragma code-name (push, "LC")
uint8_t save_file(char *filename) {
  char c;
  uint16_t p = gapbegin;
  FILE *fp = fopen(filename, "w");
  if (!fp)
    return 1;
  jump_pos(0);
  goto_prompt_row();
  while (get_char(&c) == 0) {
    fputc(c, fp);
    if ((gapbegin % 1000) == 0)
      cputc('.');
  }
  fclose(fp);
  jump_pos(p);
  return 0;
}
#pragma code-name (pop)

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
  char c;
  if ((pos >= startsel) && (pos <= endsel))
    revers(1);
  else
    revers(0);
  if ((c = gapbuf[pos++]) == EOL) {
    if (do_print) {
      revers(0);
      rowlen[row] = col + 1;
      clreol_wrap();
      gotox(0);
    }
    ++row;
    col = 0;
    return 1;
  }
  if (do_print) {
    cputc(c);
    revers(0);
  }
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

  goto_prompt_row();

  revers(1);
  cprintf("OA-? Help | ");
  if (strlen(filename)) {
    cprintf(" %c File:%s", modified ? '*' : ' ', filename);
    for (startpos = 0; startpos < 60 - strlen(filename); ++startpos)
      cputc(' ');
  } else {
    cprintf(
    "   File:NONE                                                         ");
  }
  revers(0);

  gotoxy(curscol, cursrow);
  cursor(1);
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
    if (gapbuf[p++] == '\r')
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
  if ((gapbuf[gapend] == EOL) || (i == NCOLS - 1)) {
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

  if (gapbuf[gapbegin] == EOL) {
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
      col = 0;
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
  if ((gapbuf[gapbegin] == EOL) || (i == NCOLS - 1)) {
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
  if ((gapbuf[gapbegin - 1] == EOL) || (prevcol == 0))
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
    gapbuf[gapend--] = gapbuf[--gapbegin];
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
    curscol = rowlen[cursrow];
  } else
    --curscol;
  gotoxy(curscol, cursrow);
}
#pragma code-name (pop)

/*
 * Move the cursor right
 */
#pragma code-name (push, "LC")
void cursor_right(void) {
  if (gapend < BUFSZ - 1)
    gapbuf[gapbegin++] = gapbuf[++gapend];
  else {
    beep();
    gotoxy(curscol, cursrow);
    return;
  }
  ++curscol;
  if (curscol == rowlen[cursrow]) {
    if (gapbuf[gapbegin - 1] == EOL) {
      if (cursrow == NROWS - 1)
        scroll_down();
      ++cursrow;
      curscol = 0;
    }
  }
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
  for (i = 0; i < curscol; ++i)
    gapbuf[gapend--] = gapbuf[--gapbegin];
  --cursrow;
  // Short line ...
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  for (i = 0; i < rowlen[cursrow] - curscol; ++i)
    if (gapbegin > 0)
      gapbuf[gapend--] = gapbuf[--gapbegin];
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
  if ((gapbuf[gapend + rowlen[cursrow] - curscol] != EOL) &&
      (rowlen[cursrow] != NCOLS))
    return 1; // Last line

  for (i = 0; i < rowlen[cursrow] - curscol; ++i) {
    if (gapend < BUFSZ - 1)
      gapbuf[gapbegin++] = gapbuf[++gapend];
    else { 
      beep();
      return 1;
    }
  }
  ++cursrow;
  // Short line ...
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  for (i = 0; i < curscol; ++i)
    if (gapend < BUFSZ - 1)
      gapbuf[gapbegin++] = gapbuf[++gapend];
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
  } while ((gapbuf[gapbegin] != ' ') && (gapbuf[gapbegin] != EOL) &&
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
  } while ((gapbuf[gapbegin] != ' ') && (gapbuf[gapbegin] != EOL) &&
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
 * Help screen
 * EDITHELP.TXT is expected to contain lines of exactly 80 chars
 */
#pragma code-name (push, "LC")
void help(void) {
  FILE *fp = fopen("EDITHELP.TXT", "rb");
  uint8_t *p;
  char c;
  p = (uint8_t*)0xc00e;
  *p = 0; // Turn on ALT CHARSET
  revers(0);
  cursor(0);
  clrscr();
  if (!fp) {
    printf("Can't open EDITHELP.TXT\n\n");
    goto done;
  }
  c = fgetc(fp);
  while (!feof(fp)) {
    if (c == '@') {
      revers(1);
      cputc('A');
      revers(0);
    } else if ((c != '\r') && (c != '\n'))
      cputc(c);
    c = fgetc(fp);
  }
  fclose(fp);
done:
  printf("[Press Any Key]");
  p = (uint8_t*)0xc00f;
  *p = 0; // Turn off ALT CHARSET
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
      modified = 0;
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
  modified = 0;
  startsel = endsel = 65535U;
  mode = SEL_NONE;
  draw_screen();
  while (1) {
    cursor(1);
    c = cgetc();
    switch (c) {
    case 0x80 + '1': // Top
      jump_pos(0);
      draw_screen();
      break;
    case 0x80 + '2':
      jump_pos(DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '3':
      jump_pos(2 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '4':
      jump_pos(3 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '5':
      jump_pos(4 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '6':
      jump_pos(5 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '7':
      jump_pos(6 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '8':
      jump_pos(7 * DATASIZE() / 8);
      draw_screen();
      break;
    case 0x80 + '9': // Bottom
      jump_pos(DATASIZE());
      draw_screen();
      break;
    case 0x80 + 0x08:  // OA-Left "Word left"
      word_left();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x80 + 0x15:  // OA-Right "Word right"
      word_right();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x80 + ',':  // OA-< "Home"
    case 0x80 + '<':
      goto_bol();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x80 + '.':  // OA-> "End"
    case 0x80 + '>':
      goto_eol();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x8b:  // OA-Up "Page Up"
      page_up();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x8a:  // OA-Down "Page Down"
      page_down();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x80 + 'C': // OA-C "Copy"
    case 0x80 + 'c': // OA-c
      mode = SEL_COPY;
      endsel = startsel = gapbegin;
      draw_screen();
      show_info("Copy: Go to end of selection, then [Return]");
      break;
    case 0x80 + 'D': // OA-D "Delete"
    case 0x80 + 'd': // OA-d
      mode = SEL_DEL;
      endsel = startsel = gapbegin;
      draw_screen();
      show_info("Del: Go to end of selection, then [Return]");
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
      p = strstr(gapbuf + gapend + 1, search);
      if (!p) {
        show_error("Not found, wrapping to top");
        gapbuf[gapbegin] = '\0';
        p = strstr(gapbuf, search);
        if (!p) {
          show_error("Not found");
          break;
        }
        jump_pos(p - gapbuf);
        if (tmp == 0) { // Replace mode
          for (i = 0; i < strlen(search); ++i)
            delete_char_right();
          memcpy(gapbuf + gapbegin, replace, strlen(userentry));
          gapbegin += strlen(replace);
        }
        draw_screen();
        break;
      }
      jump_pos(gapbegin + p - (gapbuf + gapend + 1));
      if (tmp == 0) { // Replace mode
        for (i = 0; i < strlen(search); ++i)
          delete_char_right();
        memcpy(gapbuf + gapbegin, replace, strlen(userentry));
        gapbegin += strlen(replace);
      }
      draw_screen();
      break;
    case 0x80 + 'I': // OA-I "Insert file"
    case 0x80 + 'i':
      tmp = 65535U;
    case 0x80 + 'L': // OA-L "Load"
    case 0x80 + 'l':
      ++tmp;
      if (prompt_for_name((tmp == 0 ? "File to insert" : "File to load"), 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      strcpy(filename, userentry);
      if (load_file(filename, (tmp == 0 ? 0 : 1))) {
        sprintf(userentry, "Can't load %s", filename);
        show_error(userentry);
        strcpy(filename, "");
      }
      draw_screen();
      break;
    case 0x80 + 'M': // OA-M "Move"
    case 0x80 + 'm': // OA-m
      mode = SEL_MOVE;
      endsel = startsel = gapbegin;
      draw_screen();
      show_info("Move: Go to end of selection, then [Return]");
      break;
    case 0x80 + 'N': // OA-N "Name"
    case 0x80 + 'n': // OA-n
      if (prompt_for_name("New filename", 1) == 255)
        break; // ESC pressed
      if (strlen(userentry) == 0)
        break;
      strcpy(filename, userentry);
      sprintf(userentry, "Name set to %s", filename);
      show_info(userentry);
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
        modified = 1;
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
        modified = 1;
      }
      break;
    case 0x09:  // Tab
      if (mode == SEL_NONE) {
        c = next_tabstop(curscol) - curscol;
        for (i = 0; i < c; ++i) {
          insert_char(' ');
          update_after_insert_char();
        }
        modified = 1;
      }
      break;
    case 0x08:  // Left
      cursor_left();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x15:  // Right
      cursor_right();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x0b:  // Up
      cursor_up();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case 0x0a:  // Down
      cursor_down();
      if (mode > SEL_MOVE2) {
        endsel = gapbegin;
        draw_screen();
      }
      break;
    case EOL:   // Return
      if (mode == SEL_NONE) {
        insert_char(c);
        update_after_insert_char();
        modified = 1;
      } else {
        if (startsel > endsel) {
          tmp = endsel;
          endsel = startsel;
          startsel = tmp;
        }
        switch (mode) {
        case SEL_DEL:
          if (prompt_okay("Delete selection - ")) {
            jump_pos(startsel);
            gapend += (endsel - startsel);
          }
          startsel = endsel = 65535U;
          mode = SEL_NONE;
          modified = 1;
          draw_screen();
          break;
        case SEL_COPY:
          mode = SEL_COPY2;
          show_info("Go to target, then [Return] to copy");
          break;
        case SEL_MOVE:
          mode = SEL_MOVE2;
          show_info("Go to target, then [Return] to move");
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
              memcpy(gapbuf + gapbegin, gapbuf + startsel, endsel - startsel);
            else
              memcpy(gapbuf + gapbegin,
                     gapbuf + gapend + startsel - gapbegin + 1, endsel - startsel);
            gapbegin += (endsel - startsel);
            if (mode == SEL_MOVE2) {
              jump_pos((gapbegin >= endsel) ? startsel : endsel);
              gapend += (endsel - startsel);
            }
          }
copymove2_cleanup:
          startsel = endsel = 65535U;
          mode = SEL_NONE;
          modified = 1;
          draw_screen();
          break;
        }
      }
      break;
    default:
      //printf("**%02x**", c);
      if ((c >= 0x20) && (c < 0x80)) {
        insert_char(c);
        update_after_insert_char();
        modified = 1;
      }
    }
  }
}

void main(int argc, char *argv[]) {
  if (argc == 2) {
    quit_to_email = 1;
    edit(argv[1]);
  } else {
    quit_to_email = 0;
    edit(NULL);
  }
}


