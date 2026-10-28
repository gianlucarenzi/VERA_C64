#ifndef VTEXT_H
#define VTEXT_H

/*
 * vtext.h - a minimal text console on the VERA layer 1, for the tests that
 * on the Atari printed through the VERA.SYS driver.
 *
 * Same layout as the Atari driver (vera_common.inc): tilemap 128x64 at
 * $1B000 (256 bytes per row), 128 glyphs at $1F000 in ATASCII order
 * (fonts/font8x8.bin, fonts/font8x16.bin), colour byte $61.  The mode is
 * chosen at build time with VT_MODE:
 *   4  40x30, 8x8 font, 320x240 scaled 2x
 *   8  80x30, 8x16 font (default)
 *   6  80x60, 8x8 font
 * Scrolling moves the layer (VSCROLL) over the 64 rows of the tilemap.
 */

#ifndef VT_MODE
#define VT_MODE 8
#endif

#if VT_MODE == 4
#define VT_COLS        40
#define VT_ROWS        30
#define VT_TILE_HEIGHT 8
#elif VT_MODE == 6
#define VT_COLS        80
#define VT_ROWS        60
#define VT_TILE_HEIGHT 8
#else
#define VT_COLS        80
#define VT_ROWS        30
#define VT_TILE_HEIGHT 16
#endif

#define VT_MAP      0x1B000ul /* tilemap, 256 bytes per row */
#define VT_MAP_ROWS 64
#define VT_FONT     0x1F000ul /* 128 glyphs */
#define VT_COLOR    0x61      /* bg 6 (blue), fg 1 (white) */

/* Set up layer 1 as text, load the font, clear the screen. */
void vt_init(void);

/* Clear the screen and home the cursor (scroll back to 0). */
void vt_clear(void);

/* Put glyph g literally (bit 7: inverse video) and advance the cursor. */
void __fastcall__ vt_glyph(unsigned char g);

/* Print a cc65 (PETSCII) character or string as ASCII glyphs; '\n' starts a
 * new line. */
void __fastcall__ vt_putc(char c);
void __fastcall__ vt_puts(const char *s);

/* First tilemap row of the screen (the layer is scrolled by whole rows). */
extern unsigned char vt_top;

#endif /* VTEXT_H */
