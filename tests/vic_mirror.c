/* vic_mirror.c - VIC-II Screen Mirroring on VERA
 *
 * This program demonstrates how to mirror the C64 VIC-II screen (40x25)
 * onto the VERA display (80x60 or 40x30 depending on the font).
 *
 * The VIC-II geometry (40 columns × 25 rows) does not naturally match
 * VERA's resolutions, so this example shows practical approaches:
 *
 * 1. Direct 1:1 mapping with padding (40x25 → centered on 80x60)
 * 2. Scaled 2x display (40x25 → 80x50 on VERA)
 * 3. Real-time update on keyboard/screen changes
 *
 * Build: cc65 -t c64 -O vic_mirror.c ...
 */

#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#pragma charmap (0x56, 0x56)
#include "../include/vera_detect.h"
#pragma charmap (0x56, 0xD6)
#include "../include/vera.h"

/* VIC-II screen memory and color memory */
#define VIC_SCREEN  ((unsigned char *)0x0400)
#define VIC_COLOR   ((unsigned char *)0xD800)
#define VIC_COLS    40
#define VIC_ROWS    25

/* VERA layer 1 configuration for 40x30 text mode */
#define VERA_MAP_BASE   0x00000     /* Tilemap at $00000 */
#define VERA_TILE_BASE  0x02000     /* Font at $02000 */
#define VERA_COLS       40
#define VERA_ROWS       30

/* VERA registers (simplified access) */
#define VERA_ADDR_L     (*(volatile unsigned char *)0x9F20)
#define VERA_ADDR_M     (*(volatile unsigned char *)0x9F21)
#define VERA_ADDR_H     (*(volatile unsigned char *)0x9F22)
#define VERA_DATA0      (*(volatile unsigned char *)0x9F23)
#define VERA_CTRL       (*(volatile unsigned char *)0x9F25)

/* Helper to set VERA address for layer 1 tilemap writes */
static void vera_set_addr(unsigned long addr)
{
    VERA_ADDR_L = addr & 0xFF;
    VERA_ADDR_M = (addr >> 8) & 0xFF;
    VERA_ADDR_H = ((addr >> 16) & 0x0F) | 0x10;  /* Bank 1 */
}

/* Write a character to VERA layer 1 tilemap at row, col */
static void vera_put_char(unsigned char row, unsigned char col, unsigned char ch)
{
    unsigned long addr = VERA_MAP_BASE + (row * VERA_COLS + col) * 2;
    vera_set_addr(addr);
    VERA_DATA0 = ch;                /* Character code */
    VERA_DATA0 = 1;                 /* Color (palette entry) */
}

/* Mirror a single line from VIC-II to VERA
 * VIC line (40 chars) → VERA (centered or scaled) */
static void mirror_line(unsigned char vic_row, unsigned char vera_row)
{
    unsigned char col, vic_col;
    unsigned char ch;

    for (col = 0; col < VIC_COLS; col++) {
        /* Read from VIC screen */
        ch = VIC_SCREEN[vic_row * VIC_COLS + col];
        
        /* Write to VERA (1:1 mapping, centered horizontally) */
        vera_put_char(vera_row, col, ch);
    }
}

/* Mirror entire VIC screen to VERA
 * 40x25 VIC → 40x30 VERA (with empty border at bottom) */
static void mirror_screen(void)
{
    unsigned char row;

    for (row = 0; row < VIC_ROWS; row++) {
        mirror_line(row, row);
    }

    /* Clear remaining rows on VERA */
    for (row = VIC_ROWS; row < VERA_ROWS; row++) {
        for (unsigned char col = 0; col < VERA_COLS; col++) {
            vera_put_char(row, col, ' ');
        }
    }
}

/* Setup VERA layer 1 for text mode
 * - Layer 1: 40x30 text (1 BPP tiles with 256-colour foreground)
 * - Tilemap at $00000, font at $02000 */
static void setup_vera_layer1(void)
{
    /* Configure layer 1 for text mode
     * This assumes fonts are already loaded in VRAM */
    
    /* Layer 1 Config (register at $9F2D):
     * - Enable layer 1
     * - Map width/height: 40x30
     * - T256C mode (256-colour foreground)
     * - Tiledata address: $02000 */
    
    vera_set_addr(0xF2D);  /* DC_VIDEO_LAYER1_CTRL */
    VERA_DATA0 = 0x05;     /* Enable, 40x30, T256C mode */
    
    vera_set_addr(0xF36);  /* Layer 1 tilebase */
    VERA_DATA0 = 0x00;
    VERA_DATA0 = 0x20;     /* $02000 >> 9 = 0x10, high byte */
}

int main(void)
{
    unsigned char last_modified = 0;

    clrscr();
    putchar(14);  /* Lower case */
    cprintf("VIC-II Screen Mirror\n");
    cprintf("====================\n\n");

    vera_require();
    cprintf("VERA found at $%04X\n", VERA_BASE);

    /* Setup VERA for text display */
    setup_vera_layer1();
    
    cprintf("VERA layer 1 configured for 40x30\n");
    cprintf("Mirroring VIC screen (40x25)...\n\n");

    /* Initial mirror */
    mirror_screen();

    cprintf("Mirror complete!\n");
    cprintf("Type on VIC, changes appear on VERA.\n");
    cprintf("RUN/STOP to exit.\n");

    /* Main loop: monitor for screen changes and update VERA */
    for (;;) {
        /* Simple approach: check if VIC screen changed
         * In a real implementation, hook the CHROUT/CHRIN/PLOT vectors */
        
        if (kbhit()) {
            unsigned char k = cgetc();
            if (k == 3) {  /* RUN/STOP */
                break;
            }
        }

        /* Periodically check for changes (slow but simple) */
        /* Better: hook KERNAL vectors as vera_screen.s does */
        if (last_modified++ > 100) {
            mirror_screen();
            last_modified = 0;
        }
    }

    clrscr();
    cprintf("Mirror stopped.\n");
    return 0;
}
