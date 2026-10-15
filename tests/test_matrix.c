/* test_matrix.c — Digital-rain screensaver on the VERA.
 *
 * Standalone program: the VERA text layer is set up by vtext.c, the screen
 * size is the one of its build-time mode (VT_COLS × VT_ROWS).
 *
 * Algorithm: each column runs one vertical "string drop" at a time.
 * The head advances TWO rows per frame (60 Hz jiffy).  Four colour-
 * transition rows are written per step, keeping the bus traffic to
 * ≈8 × screen_width writes per frame (2 steps × 4 writes).
 *
 *   head        → white  (COL_WHITE)
 *   head – 1    → white→light-green transition
 *   head – ZONE_LIGHT  → light-green→dark-green transition
 *   head – ZONE_TOTAL  → dark-green→black (clear)
 *
 * PRNG: 8-bit Galois LFSR (~15 cycles) instead of cc65 rand() (~100 cycles).
 *
 * Build: see Makefile (testmtx4/8/6.prg).
 */

#include <stdio.h>
#include <conio.h>
#include "detect.h"
#include "vtext.h"

#define VERA_ADDR_LO  VERA_ADDR_L
#define VERA_ADDR_MID VERA_ADDR_M
#define VERA_ADDR_HI  VERA_ADDR_H
#define DC_BORDER     VERA_DC_BORDER

/* Tilemap: Layer 1 at SCREEN_ADDR=$01B000 (vera_common.inc).
 * Row stride = 128 cells × 2 bytes = 256 bytes.
 * All VRAM addresses are in bank 1, so ADDR_HI bit 0 is always 1. */
#define VRAM_MAP     0x01B000UL
#define ROW_STRIDE   256u
#define ADDR_H_INC1  0x11   /* inc=+1, bank=1 */
#define ADDR_H_NOINC 0x01   /* inc=0,  bank=1 */

/* We program the VERA palette ourselves so colours are independent of
 * whatever VERA.SYS loaded.  Palette VRAM starts at $1FA00 (bank 1).
 * Entry format: byte0 = (green4<<4)|blue4, byte1 = red4.
 *
 * Entry 4 is the cell background (black), as on the Atari, where the
 * VERA.SYS VBI handler reset entry 0 every frame.  Entries 1-3 hold
 * the green shades.
 *
 * Index  Name     #rrggbb   byte0  byte1
 *   0    black    #000000    0x00   0x00
 *   1    white    #FFFFFF    0xFF   0x0F
 *   2    lt-grn   #00FF00    0xF0   0x00
 *   3    dk-grn   #004400    0x40   0x00
 *   4    black    #000000    0x00   0x00   ← cell bg (safe slot)
 */
#define VERA_PAL_BASE  0x1FA00UL
#define COL_BG_IDX   4           /* palette index used for black cell background */

/* Tilemap colour byte = (bg_nibble << 4) | fg_nibble.
 * bg nibble = COL_BG_IDX = 4 for all cells so cell backgrounds are
 * palette[4] = black. */
#define COL_BLACK    0x44   /* space: bg=4(blk), fg=4(blk) */
#define COL_WHITE    0x41   /* head:  bg=4(blk), fg=1(wht) */
#define COL_LGRE     0x42   /* zone:  bg=4(blk), fg=2(lgr) */
#define COL_DGRE     0x43   /* zone:  bg=4(blk), fg=3(dgr) */

/* Trail geometry (rows behind the white head) */
#define ZONE_LIGHT   5
#define ZONE_DARK    8
#define ZONE_TOTAL   (ZONE_LIGHT + ZONE_DARK)

/* Steps per VBI frame: 2 = double visual speed */
#define STEPS_PER_FRAME 2

/* Spawn: fast_rand() < SPAWN_THRESH ≈ 13/256 ≈ 5 % */
#define SPAWN_THRESH 13

/* Entropy for seeding: jiffy clock and CIA 1 timer A, low bytes */
#define RTCLOK_L      (*(volatile unsigned char*)0x00A2)
#define CIA1_TA_LO    (*(volatile unsigned char*)0xDC04)

#define MAX_COLS  80

static int           screen_width  = VT_COLS;
static int           screen_height = VT_ROWS;
static unsigned char saved_border = 6;   /* DC_BORDER at entry, restored on exit */

static unsigned char col_head[MAX_COLS];
static unsigned char col_active[MAX_COLS];

/* ------------------------------------------------------------------ */
/* 8-bit Galois LFSR — ~15 cycles vs ~100 for cc65 rand()             */
/* Polynomial x^8+x^6+x^5+x^2+1 = 0xB4 (maximal-length, period 255) */
/* ------------------------------------------------------------------ */
static unsigned char prng8;

static unsigned char fast_rand(void)
{
    unsigned char lsb = prng8 & 1;
    prng8 >>= 1;
    if (lsb) prng8 ^= 0xB4;
    return prng8;
}

/* ------------------------------------------------------------------ */
/* Palette setup                                                       */
/* ------------------------------------------------------------------ */

/* Original VERA default palette entries 1..4 (GGGGBBBB, 0000RRRR), restored on exit */
static void restore_default_palette(void)
{
    VERA_CTRL     = 0;
    VERA_ADDR_LO  = 0x02;            /* entry 1 = $1FA02 */
    VERA_ADDR_MID = 0xFA;
    VERA_ADDR_HI  = ADDR_H_INC1;
    VERA_DATA0 = 0xFF; VERA_DATA0 = 0x0F;   /* 1: #FFFFFF */
    VERA_DATA0 = 0x00; VERA_DATA0 = 0x08;   /* 2: #880000 */
    VERA_DATA0 = 0xFE; VERA_DATA0 = 0x0A;   /* 3: #AAFFEE */
    VERA_DATA0 = 0x4C; VERA_DATA0 = 0x0C;   /* 4: #CC44CC */
}

static void setup_matrix_palette(void)
{
    /* Write 5 consecutive palette entries starting at $1FA00.
     * Auto-increment=1 streams all 10 bytes in one sequential burst. */
    VERA_CTRL     = 0;       /* ensure DCSEL=0 */
    VERA_ADDR_LO  = 0x00;
    VERA_ADDR_MID = 0xFA;
    VERA_ADDR_HI  = ADDR_H_INC1;   /* bank=1, inc=+1 */
    VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 0: black    #000000 */
    VERA_DATA0 = 0xFF; VERA_DATA0 = 0x0F;   /* 1: white    #FFFFFF */
    VERA_DATA0 = 0xF0; VERA_DATA0 = 0x00;   /* 2: lt-grn   #00FF00 */
    VERA_DATA0 = 0x40; VERA_DATA0 = 0x00;   /* 3: dk-grn   #004400 */
    VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 4: black    #000000 (cell bg) */

    /* DC_BORDER (DCSEL=0): palette index for the display border/
     * background area outside the tilemap.  Point it to our black entry. */
    DC_BORDER = COL_BG_IDX;
}

/* ------------------------------------------------------------------ */
/* Low-level VERA helpers                                              */
/* ------------------------------------------------------------------ */

static void vera_put_char(int x, int y, unsigned char ch, unsigned char color)
{
    unsigned long off = VRAM_MAP + (unsigned long)y * ROW_STRIDE + (unsigned int)x * 2;
    VERA_ADDR_LO  = (unsigned char)(off);
    VERA_ADDR_MID = (unsigned char)(off >> 8);
    VERA_ADDR_HI  = ADDR_H_INC1;   /* bank=1 always, inc=1 for char+color pair */
    VERA_DATA0 = ch;
    VERA_DATA0 = color;
}

static void vera_clear_screen(void)
{
    int x, y;
    for (y = 0; y < screen_height; ++y) {
        unsigned long row = VRAM_MAP + (unsigned long)y * ROW_STRIDE;
        VERA_ADDR_LO  = (unsigned char)(row);
        VERA_ADDR_MID = (unsigned char)(row >> 8);
        VERA_ADDR_HI  = ADDR_H_INC1;
        for (x = 0; x < screen_width; ++x) {
            VERA_DATA0 = 32;
            VERA_DATA0 = COL_BLACK;
        }
    }
}

/* Returns a printable char from the 94-char range [33..126] */
static unsigned char matrix_char(void)
{
    unsigned char r = fast_rand() % 94;
    return 33 + r;
}

static void wait_vbi(void)
{
    unsigned char t = RTCLOK_L;
    while (RTCLOK_L == t)
        ;
}

/* ------------------------------------------------------------------ */
/* One animation step for column col at head position h.              */
/* Writes at most 4 cells; caller increments col_head after.          */
/* ------------------------------------------------------------------ */
static void column_step(int col, int h)
{
    int r;

    if (h < screen_height)
        vera_put_char(col, h, matrix_char(), COL_WHITE);

    r = h - 1;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, matrix_char(), COL_LGRE);

    r = h - ZONE_LIGHT;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, matrix_char(), COL_DGRE);

    r = h - ZONE_TOTAL;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, 32, COL_BLACK);
}

/* ------------------------------------------------------------------ */
/* Main animation update — called once per VBI frame.                 */
/* Each active column advances STEPS_PER_FRAME rows.                  */
/* ------------------------------------------------------------------ */
static void update_matrix(void)
{
    int col;
    unsigned char step;
    unsigned char done_thresh;

    done_thresh = (unsigned char)(screen_height + ZONE_TOTAL);

    for (col = 0; col < screen_width; ++col) {

        if (!col_active[col]) {
            if (fast_rand() < SPAWN_THRESH) {
                col_head[col]   = 0;
                col_active[col] = 1;
            }
            continue;
        }

        for (step = 0; step < STEPS_PER_FRAME; ++step) {
            column_step(col, (int)(unsigned char)col_head[col]);
            col_head[col]++;
            if (col_head[col] >= done_thresh) {
                col_active[col] = 0;
                break;
            }
        }
    }

}

/* ------------------------------------------------------------------ */

int main(void)
{
    putchar(14);            /* lower/upper case character set */
    printf("VERA Matrix\n");

    vera_require();
    vt_init();              /* text layer, scroll at 0, VERA_CTRL = 0 */
    saved_border = DC_BORDER;
    setup_matrix_palette();

    printf("Mode: %dx%d\n", screen_width, screen_height);
    printf("Press a key to exit.\n");

    /* Seed the LFSR from the jiffy clock and the CIA 1 timer */
    prng8 = RTCLOK_L ^ CIA1_TA_LO;
    if (!prng8) prng8 = 0xA5;   /* LFSR must not be zero */

    vera_clear_screen();

    while (!kbhit()) {
        wait_vbi();
        update_matrix();
    }
    cgetc();

    /* Leave the VERA with its default colours */
    restore_default_palette();
    DC_BORDER = saved_border;
    vt_clear();
    vt_puts("Test ended.\n");
    printf("Test ended.\n");
    return 0;
}
