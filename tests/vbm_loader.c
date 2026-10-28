/* vbm_loader.c — read a compiled .vbm file (see vera-asset-format.md) and stream it straight into VERA VRAM via vbm_display.s.
 * Deliberately never buffers the whole 320x240 image in C64 RAM (77KB —
 * far more than this machine has): reads a few hundred bytes
 * at a time into vbm_iobuf and streams each chunk to VRAM before reading
 * the next, the same "know the exact size, never guess" spirit as
 * vtm_loader.c but streamed instead of accumulated, since the destination
 * here is VRAM (plenty of room, no CPU-side random access needed after
 * upload) rather than a buffer vtm_player.s has to read patterns back out
 * of.
 */
#include <stdio.h>
#include "vbm.h"

#define HEADER_SIZE  12u /* magic(4) + width(2) + height(2) + bpp(1) + name_len(1) + reserved(2) */
#define NAME_MAX     40u /* matches test_player.c's SCREEN_COLS — a longer
                               * name is still fully consumed from the file,
                               * just truncated for display */
#define PALETTE_SIZE 512u
#define PIXEL_SIZE   (320ul * 240ul)
#define IOBUF_SIZE   128u /* must match vbm_display.s's VBM_IOBUF_SIZE */

extern unsigned char vbm_iobuf[IOBUF_SIZE];
extern void vbm_seek_palette(void);
extern void vbm_seek_pixels(void);
extern void vbm_stream_len(unsigned char n);

static unsigned char stream_from(FILE *f, unsigned long remaining,
                                 unsigned long *loaded, unsigned long total,
                                 const char *name, unsigned char name_len,
                                 vbm_progress_cb progress)
{
    unsigned char n;

    while (remaining)
    {
        n = (remaining > IOBUF_SIZE) ? IOBUF_SIZE : (unsigned char)remaining;
        if (fread(vbm_iobuf, 1, n, f) != n) return 0;
        vbm_stream_len(n);
        remaining -= n;
        *loaded += n;
        if (progress) progress(name, name_len, *loaded, total);
    }
    return 1;
}

unsigned char vbm_load_file(const char *filename, vbm_progress_cb progress)
{
    FILE *f;
    unsigned char hdr[HEADER_SIZE];
    char name[NAME_MAX];
    unsigned char name_len, shown_len, n;
    unsigned extra;
    unsigned long loaded = 0;
    unsigned long total = PALETTE_SIZE + PIXEL_SIZE;

    f = fopen(filename, "rb");
    if (!f) return 0;

    if (fread(hdr, 1, HEADER_SIZE, f) != HEADER_SIZE)
    {
        fclose(f);
        return 0;
    }
    /* "VBM2" in ASCII (cc65 c64 character constants are PETSCII) */
    if (hdr[0] != 0x56 || hdr[1] != 0x42 || hdr[2] != 0x4D || hdr[3] != 0x32)
    {
        fclose(f);
        return 0;
    }
    /* width/height/bpp are fixed at 320x240x8 for this player (see
     * vbm.h/vera-asset-format.md) — no need to check them beyond the magic, since
     * we always stream a fixed byte count regardless of what's declared. */

    name_len = hdr[9];
    shown_len = (name_len > NAME_MAX) ? NAME_MAX : name_len;
    if (shown_len && fread(name, 1, shown_len, f) != shown_len)
    {
        fclose(f);
        return 0;
    }

    extra = (unsigned)name_len - shown_len; /* rest of a name too long to show, still on disk */
    while (extra)
    {
        n = (extra > IOBUF_SIZE) ? IOBUF_SIZE : (unsigned char)extra;
        if (fread(vbm_iobuf, 1, n, f) != n)
        {
            fclose(f);
            return 0;
        }
        extra -= n;
    }

    vbm_seek_palette();
    if (progress) progress(name, shown_len, loaded, total); /* name known, nothing streamed yet */
    if (!stream_from(f, PALETTE_SIZE, &loaded, total, name, shown_len, progress))
    {
        fclose(f);
        return 0;
    }

    vbm_seek_pixels();
    if (!stream_from(f, PIXEL_SIZE, &loaded, total, name, shown_len, progress))
    {
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}
