/* test_font.c - every glyph of the VERA text font, over and over.
 *
 * Standalone program: sets up its own text console on the VERA (vtext.c,
 * mode chosen at build time) and prints the 256 tile codes (128 glyphs,
 * then the same in inverse video) until a key is pressed.  On the Atari
 * the same loop went through the VERA.SYS driver (ESC + character).
 */

#include <stdio.h>
#include <conio.h>
#include "detect.h"
#include "vtext.h"

int main(void)
{
    int i;
    unsigned int id;

    putchar(14);            /* lower/upper case character set */
    id = vera_require();
    printf("VERA detected (ID: 0x%04X)\n", id);
    printf("Character test loop on the VERA\n");
    printf("(%ux%u). Press a key to exit.\n", VT_COLS, VT_ROWS);

    vt_init();
    vt_puts("VERA detected, character test loop. Press a key to exit.\n");
    while (1) {
        for (i = 0; i < 256; i++) {
            vt_glyph((unsigned char)i);
            if (kbhit())
                goto done;
        }
    }
done:
    cgetc();
    vt_puts("\nTest ended.\n");
    printf("Test ended.\n");
    return 0;
}
