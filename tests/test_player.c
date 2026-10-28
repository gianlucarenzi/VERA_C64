/* test_player.c — VTM PSG music player demo for the VERA card on the C64.
 *
 * Loads demo.vtm from the drive the program was loaded from and plays it
 * on 4 VERA PSG voices, ticked once per jiffy (60 Hz, same poll-loop idiom
 * as test_matrix.c). Shows a 4-channel VU meter drawn with reverse-video
 * characters on the C64 screen (the Atari version used Player/Missile
 * graphics) while playing. Press any key to stop and exit.
 *
 * Screen: cleared to black background / white text, with the song's
 * TITLE (see vtm_format.md's header field, offset 12) printed centered
 * on the top row in reverse video. No other status text — only genuine
 * errors are printed, so they don't flash past unread.
 *
 * If demo.vbm is also present on the same drive (see vbm.h/vbm_display.s,
 * vera-asset-format.md's VBM2 format and tools/img2vbm.py), its artwork is
 * shown centered on a black background on VERA's own video output — a
 * completely separate screen from the C64's own VIC-II display used above
 * for the title/VU meter — for as long as the song plays. No image file
 * means no change to VERA's display at all. The image's own embedded name
 * is printed centered on this (the C64's) screen too, the same way as the
 * song title.
 *
 * The data files are PRG files on the disk (the build writes them with
 * c1541 as any program): they are opened as such (_filetype = 'p') and
 * read as a plain byte stream, load address bytes included — which are
 * simply the first two bytes of the file.
 *
 * Build: standalone (PSG registers are poked directly, same as
 * test_matrix.c pokes VERA video registers directly).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <cbm.h>
#include "detect.h"
#include "vtm.h"
#include "vbm.h"

#define N_BARS      4u
#define SCREEN_COLS 40u

/* KERNAL jiffy clock TIME ($A0-$A2), low byte: 60 Hz */
#define RTCLOK_L (*(volatile unsigned char *)0x00A2)

/* VU meter: four bars of reverse spaces on the C64 screen, BAR_WIDTH
 * columns wide, up to BAR_ROWS rows tall, bottom on row BAR_BOTTOM. */
#define VU_SCREEN  ((unsigned char *)0x0400)
#define VU_COLOR   ((unsigned char *)0xD800)
#define BAR_WIDTH  6u
#define BAR_GAP    3u
#define BAR_LEFT   ((SCREEN_COLS - (N_BARS * BAR_WIDTH + (N_BARS - 1) * BAR_GAP)) / 2)
#define BAR_BOTTOM 23u
#define BAR_ROWS   12u
#define SC_BLOCK   0xA0 /* screen code: reverse space */
#define SC_EMPTY   0x20 /* screen code: space */

static const unsigned char bar_color[N_BARS] = {
    COLOR_LIGHTGREEN, COLOR_YELLOW, COLOR_LIGHTBLUE, COLOR_LIGHTRED
};

/* Bars never fully vanish: level 3 maps to the resting height. Decay is
 * fast (-12/frame), as on the Atari, where the visible range was 128px. */
#define VU_FLOOR_LEVEL 3u
#define VU_DECAY_STEP  12u

static unsigned char displayed_level[N_BARS];
static unsigned char drawn_rows[N_BARS];
static unsigned char bar_filled;
static unsigned char title_shown;
static unsigned char img_bar_filled;
static unsigned char img_bar_started;

static void wait_vbi(void)
{
    unsigned char t = RTCLOK_L;
    while (RTCLOK_L == t)
        ;
}

/* ASCII (song and image names) to PETSCII for the lower/upper case set */
static unsigned char ascii_to_petscii(unsigned char c)
{
    if (c >= 0x61 && c <= 0x7A) return (unsigned char)(c - 0x20); /* a-z */
    if (c >= 0x41 && c <= 0x5A) return (unsigned char)(c + 0x80); /* A-Z */
    if (c < 0x20 || c >= 0x7F) return '?';
    return c;
}

/* Shared by the song title and the image name below: prints up to
 * SCREEN_COLS bytes centered, in reverse video, followed by a newline. */
static void print_centered(const unsigned char *text, unsigned char len)
{
    unsigned char pad, i;

    if (len > SCREEN_COLS) len = SCREEN_COLS; /* clip, don't wrap */
    pad = (SCREEN_COLS - len) / 2;

    for (i = 0; i < pad; i++) cputc(' ');
    revers(1);
    for (i = 0; i < len; i++) cputc(ascii_to_petscii(text[i]));
    revers(0);
    if (wherex() != 0) cputs("\r\n");
}

/* Shared by load_progress/image_progress below: grows a one-row, left-to-
 * right bar of reverse-video blocks, one block per SCREEN_COLS-th of
 * loaded/total. *filled tracks how much of THIS bar is already drawn. */
static void draw_bar(unsigned char *filled, unsigned long loaded, unsigned long total)
{
    unsigned char target = (unsigned char)((loaded * SCREEN_COLS) / total);
    revers(1);
    while (*filled < target && *filled < SCREEN_COLS - 1)
    {
        cputc(' ');
        (*filled)++;
    }
    revers(0);
}

/* Reads the title straight out of the loaded .vtm blob (vtm_format.md:
 * offset 12 = length, offset 13.. = raw ASCII, not null-terminated). */
static void print_title(const void *song)
{
    const unsigned char *buf = (const unsigned char *)song;
    print_centered(buf + 13, buf[12]);
}

/* vtm_load_file()'s progress callback (see vtm.h): fires once as soon as
 * the title is readable (prints it), then again per chunk of pattern
 * data (grows the bar). */
static void load_progress(const void *song, unsigned long loaded, unsigned long total)
{
    if (!title_shown)
    {
        print_title(song);
        cputs("\r\n"); /* blank row between title and progress bar */
        title_shown = 1;
    }
    draw_bar(&bar_filled, loaded, total);
}

/* vbm_load_file()'s progress callback (see vbm.h): fires once as soon as
 * the file's embedded name is readable (prints it, same as the song
 * title above), then again per streamed chunk (grows its own bar, on its
 * own row below the song's). */
static void image_progress(const char *name, unsigned char name_len,
                           unsigned long loaded, unsigned long total)
{
    if (!img_bar_started)
    {
        print_centered((const unsigned char *)name, name_len);
        cputs("\r\n"); /* blank row between name and progress bar */
        img_bar_started = 1;
    }
    draw_bar(&img_bar_filled, loaded, total);
}

/* level 0-63 to a bar height of 1..BAR_ROWS rows */
static unsigned char level_rows(unsigned char level)
{
    return (unsigned char)(1u + ((unsigned int)level * (BAR_ROWS - 1u) + 31u) / 63u);
}

/* Redraw only the rows of bar ch that changed since the last frame. */
static void vu_draw(unsigned char ch, unsigned char rows)
{
    unsigned char r, x, code;
    unsigned int off;

    if (rows == drawn_rows[ch]) return;
    for (r = 0; r < BAR_ROWS; r++)
    {
        if ((r < rows) == (r < drawn_rows[ch])) continue;
        code = (r < rows) ? SC_BLOCK : SC_EMPTY;
        off = (BAR_BOTTOM - r) * SCREEN_COLS + BAR_LEFT + ch * (BAR_WIDTH + BAR_GAP);
        for (x = 0; x < BAR_WIDTH; x++)
        {
            VU_SCREEN[off + x] = code;
            VU_COLOR[off + x] = bar_color[ch];
        }
    }
    drawn_rows[ch] = rows;
}

static void vu_init(void)
{
    unsigned char ch;
    for (ch = 0; ch < N_BARS; ch++)
    {
        drawn_rows[ch] = BAR_ROWS; /* force a full redraw */
        vu_draw(ch, 0);
    }
}

static void vu_tick(void)
{
    unsigned char ch, lvl;
    for (ch = 0; ch < N_BARS; ch++)
    {
        lvl = vtm_level(ch);
        if (lvl >= displayed_level[ch])
        {
            displayed_level[ch] = lvl;
        }
        else if (displayed_level[ch] > VU_FLOOR_LEVEL + VU_DECAY_STEP)
        {
            displayed_level[ch] -= VU_DECAY_STEP;
        }
        else
        {
            displayed_level[ch] = VU_FLOOR_LEVEL;
        }
        vu_draw(ch, level_rows(displayed_level[ch]));
    }
}

/* Song and artwork are read from the drive the program was loaded from:
 * cc65 opens files on _curunit, the last device the KERNAL loaded from. */
static const char song_path[] = "demo.vtm";
static const char image_path[] = "demo.vbm";

int main(void)
{
    void *song;
    int rval = 0;
    unsigned char ch;
    unsigned char have_image;

    putchar(14); /* lower/upper case character set */
    bgcolor(COLOR_BLACK);
    bordercolor(COLOR_BLACK);
    textcolor(COLOR_WHITE);
    clrscr();

    vera_require();

    _filetype = 'p'; /* the data files are PRG files on the disk */

    song = vtm_load_file(song_path, load_progress);
    if (!song)
    {
        cprintf("\r\nerror: could not load %s\r\n", song_path);
        rval = 1;
        goto err;
    }
    cputs("\r\n\r\n"); /* move past the progress bar row */

    if (!vtm_init(song))
    {
        cputs("error: demo.vtm is not a valid VTM3 file\r\n");
        free(song);
        rval = 1;
        goto err;
    }

    /* Artwork is optional: demo.vbm on the load drive (see
     * vera-asset-format.md, tools/img2vbm.py) shows on VERA's own video
     * output — a separate screen from this C64 text/VU-meter display —
     * while the song plays. If it's missing, playback proceeds exactly as
     * before with no VERA display-mode change at all. */
    have_image = vbm_load_file(image_path, image_progress);
    if (have_image)
    {
        cputs("\r\n\r\n"); /* move past the image progress bar row */
        vbm_init();
    }

    for (ch = 0; ch < N_BARS; ch++)
        displayed_level[ch] = VU_FLOOR_LEVEL;

    while (kbhit()) cgetc();
    vu_init();
    while (!kbhit())
    {
        wait_vbi();
        vtm_tick();
        vu_tick();
    }
    cgetc(); /* the key that stopped playback */

    if (have_image) vbm_done();

    vtm_stop();
    free(song);
    clrscr();
    return rval;

err:
    /* Only on error: keep the message on screen until a key is pressed. */
    cputs("Press any key...\r\n");
    cgetc();
    return rval;
}
