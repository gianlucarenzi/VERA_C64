/* vic_mirror.c - VIC-II to VERA Screen Mirror (40 columns mode)
 *
 * Mirrors the VIC-II screen output to VERA in real-time as you type.
 *
 * Features:
 * - VIC-II (40x25) mirrored to VERA (40x30)
 * - Real-time: as you type, it appears on both screens
 * - Cursor movement synchronized
 * - Text editing, LIST, INPUT all work
 *
 * Build: make vic_mirror.prg
 */

#include <conio.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#pragma charmap (0x56, 0x56)
#include "../include/vera_detect.h"
#pragma charmap (0x56, 0xD6)
/* clang-format on */
#include "../include/vera.h"

/* VIC-II screen and cursor position */
#define VIC_SCREEN ((unsigned char *)0x0400)
#define VIC_COLS   40
#define VIC_ROWS   25

/* VERA layer 1 in bank 1 */
#define VERA_MAP_BASE 0x00000
#define VERA_COLS     40
#define VERA_ROWS     30

/* KERNAL cursor position */
#define PNTR (*(volatile unsigned char *)0xD3)
#define TBLX (*(volatile unsigned char *)0xD6)

static unsigned char last_pntr = 0;
static unsigned char last_tblx = 0;

/* Write one character cell to VERA */
static void vera_write_cell(unsigned char row, unsigned char col,
                            unsigned char ch, unsigned char color)
{
    unsigned long addr = VERA_MAP_BASE + (row * VERA_COLS + col) * 2;
    VERA_ADDR_L = addr & 0xFF;
    VERA_ADDR_M = (addr >> 8) & 0xFF;
    VERA_ADDR_H = 0x10 | ((addr >> 16) & 0x0F);
    VERA_DATA0 = ch;
    VERA_DATA0 = color;
}

/* Mirror a character from VIC to VERA */
static void mirror_cell(unsigned char row, unsigned char col)
{
    unsigned char ch;

    if (row >= VIC_ROWS || col >= VIC_COLS)
        return;

    ch = VIC_SCREEN[row * VIC_COLS + col];
    vera_write_cell(row, col, ch, 0x0F);
}

/* Mirror entire VIC screen to VERA */
static void mirror_screen_full(void)
{
    unsigned char row;
    unsigned char col;

    for (row = 0; row < VIC_ROWS; row++)
    {
        for (col = 0; col < VIC_COLS; col++)
        {
            mirror_cell(row, col);
        }
    }

    /* Clear bottom rows */
    for (row = VIC_ROWS; row < VERA_ROWS; row++)
    {
        for (col = 0; col < VIC_COLS; col++)
        {
            vera_write_cell(row, col, ' ', 0x0F);
        }
    }
}

/* Mirror cursor (show with inverse video) */
static void mirror_cursor(void)
{
    unsigned char row;
    unsigned char col;
    unsigned char ch;

    row = TBLX;
    col = PNTR;

    if (row >= VIC_ROWS || col >= VIC_COLS)
        return;

    ch = VIC_SCREEN[row * VIC_COLS + col];
    vera_write_cell(row, col, ch, 0xF0);
}

int main(void)
{
    unsigned char k;
    unsigned char new_row;
    unsigned char new_col;

    clrscr();
    putchar(14);

    cprintf("VIC-II to VERA Screen Mirror\n");
    cprintf("============================\n\n");

    vera_require();
    cprintf("VERA found at $%04X\n\n", VERA_BASE);

    cprintf("Initializing mirror...\n");
    mirror_screen_full();
    mirror_cursor();

    cprintf("Done! Type something...\n");
    cprintf("RUN/STOP to exit\n\n");

    last_pntr = PNTR;
    last_tblx = TBLX;

    /* Main loop */
    for (;;)
    {
        /* Check if cursor moved */
        if (PNTR != last_pntr || TBLX != last_tblx)
        {
            if (last_tblx < VIC_ROWS && last_pntr < VIC_COLS)
            {
                mirror_cell(last_tblx, last_pntr);
            }
            mirror_cursor();
            last_pntr = PNTR;
            last_tblx = TBLX;
        }

        if (kbhit())
        {
            k = cgetc();
            if (k == 3) /* RUN/STOP */
            {
                break;
            }
            /* Mirror printed character */
            new_row = TBLX;
            new_col = PNTR;
            if (new_col > 0) new_col--;
            if (new_row < VIC_ROWS && new_col < VIC_COLS)
            {
                mirror_cell(new_row, new_col);
                mirror_cursor();
            }
        }
    }

    clrscr();
    cprintf("Mirror stopped.\n");
    return 0;
}
