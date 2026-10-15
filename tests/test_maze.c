/* test_maze.c - the "10 PRINT" maze with the two diagonal glyphs.
 *
 * Standalone program: sets up its own text console on the VERA (vtext.c,
 * mode chosen at build time) and prints glyph 6 or 7 at random, one page
 * with the cc65 rand(), the next one with an 8 bit LFSR (the Atari version
 * used the POKEY RANDOM register; the C64 has no such register: the SID
 * voice 3 noise reads as a constant in VICE), until a key is pressed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "detect.h"
#include "vtext.h"

/* seed: KERNAL jiffy clock and CIA 1 timer A, low bytes */
#define RTCLOK_L       (*(volatile unsigned char *)0x00A2)
#define CIA1_TA_LO     (*(volatile unsigned char *)0xDC04)

/* 8-bit Galois LFSR, x^8+x^6+x^5+x^2+1 (as in test_matrix.c) */
static unsigned char prng8;

static unsigned char fast_rand(void)
{
    unsigned char lsb = prng8 & 1;
    prng8 >>= 1;
    if (lsb) prng8 ^= 0xB4;
    return prng8;
}

int main(void)
{
    int maze;
    int do_rand = 1;
    int counter = VT_COLS * 25;
    unsigned int id;

    putchar(14);            /* lower/upper case character set */
    id = vera_require();
    printf("VERA detected (ID: 0x%04X)\n", id);
    printf("Maze test loop on the VERA\n");
    printf("(%ux%u). Press a key to exit.\n", VT_COLS, VT_ROWS);
    srand(12345);

    prng8 = RTCLOK_L ^ CIA1_TA_LO;
    if (!prng8) prng8 = 0xA5;   /* LFSR must not be zero */

    vt_init();

    while (1)
    {
        if (kbhit())
            break;

        if (--counter > 1)
        {
            if (do_rand)
            {
                /* first a page of random values from the C rand(); its
                 * low bit only alternates (diagonal stripes): take bit 8 */
                maze = (rand() >> 8) & 0x01;
            }
            else
            {
                /* then the random numbers of the LFSR */
                maze = fast_rand() & 0x01;
            }
            /* print the maze */
            vt_glyph((unsigned char)(maze + 6));
        }
        else
        {
            counter = VT_COLS * 25;
            do_rand = !do_rand;
        }
    }
    cgetc();
    vt_puts("\nTest ended.\n");
    printf("Test ended.\n");
    return 0;
}
