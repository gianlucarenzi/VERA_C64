/* test_rmt.c - RMT music on the VERA PSG of the C64 VERA card.
 *
 * The Atari RMT player (rmtplayr.s, or lzsplayr.s for a .sap/.xex song,
 * -D RMT_C64 -D RMT_VERA) runs on a raster IRQ (rmtirq.s) and computes the
 * POKEY registers in RAM; psgrmt.s turns them into VERA PSG voices once
 * per frame. RMT4 modules (mono, 4 channels) use voices 0-3; RMT8 modules
 * (stereo, made for two POKEYs) use voices 0-7.
 *
 *   P  PSG on/off      S  VERA stereo on/off      RUN/STOP or Q  exit
 *
 * The song is linked in at build time (Makefile: RMT_SONG=..., converted by
 * tools/rmt2ca65.py or tools/sap2ca65.py; RMT_TRACKS follows the module).
 * Output on the C64 screen; standalone, no VERA screen driver needed.
 */

#include <stdio.h>
#include <conio.h>
/* vera_detect.h compares the identity byte with 'V': the ASCII code, not
 * the PETSCII one cc65 makes of it on the C64 */
#pragma charmap (0x56, 0x56)
#include "vera_detect.h"
#pragma charmap (0x56, 0xD6)
#include "rmt.h"

#ifndef RMT_SONG_NAME
#define RMT_SONG_NAME "?"
#endif

#define KEY_STOP 3

static void show_levels(unsigned char row, const char *label, unsigned char first)
{
    unsigned char i;

    gotoxy(0, row);
    cputs(label);
    for (i = first; i < first + 4; i++)
        cprintf(" %2u", rmt_audc[i] & 0x0F);
    cputs("  vera");
    for (i = first; i < first + 4; i++)
        cprintf(" %2u", psg_volume[i]);
}

int main(void)
{
    unsigned char k, speed;

    clrscr();
    putchar(14);            /* lower case character set */
    cputs("RMT player: VERA PSG on the C64\r\n");
    cputs("===============================\r\n");
    vera_require();
    cprintf("Song: %s  RMT%u\r\n", RMT_SONG_NAME, (unsigned int)rmt_tracks);
    cprintf("%s: %u Hz frames, %u lines\r\n\r\n", rmt_ntsc ? "NTSC" : "PAL",
            rmt_ntsc ? 60 : 50, rmt_frame_lines);
    cputs("P PSG on/off  S VERA stereo\r\n");
    cputs("RUN/STOP or Q exit\r\n");

    psg_init();
    psg_enable = 1;
    speed = rmt_init(rmt_song);
    cprintf("\r\nInstrument speed: %u\r\n", speed);
    rmt_irq_on();

    for (;;) {
        gotoxy(0, 11);
        cprintf("PSG: %s  Stereo: %s", psg_enable ? "on " : "off", psg_stereo ? "on " : "off");
        gotoxy(0, 13);
        cprintf("Frames %5u  player %3u/%3u lines", rmt_frames, rmt_lines, rmt_maxlines);
        show_levels(15, rmt_tracks > 4 ? "L pokey" : "  pokey", 0);
#if RMT_TRACKS > 4
        show_levels(16, "R pokey", 4);
#endif

        if (!kbhit())
            continue;
        k = cgetc();
        if (k == KEY_STOP || k == 'q' || k == 'Q')
            break;
        if (k == 'p' || k == 'P')
            psg_enable = (unsigned char)!psg_enable;
        else if (k == 's' || k == 'S')
            psg_stereo = (unsigned char)!psg_stereo;
    }

    rmt_irq_off();          /* silences the PSG voices */
    clrscr();
    cputs("Music stopped.\r\n");
    return 0;
}
