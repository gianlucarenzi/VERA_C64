/* vtm_play.c — desktop VTM3 player/previewer (SDL2 audio).
 *
 * Plays a compiled .vtm file (see ../vtm_format.md) through the PC's sound
 * card, so you can audition a song while composing without going through
 * the Atari emulator/hardware round trip. It replays the exact same
 * row/pattern/order/envelope logic as vtm_player.s (../vtm_player.s), one
 * "tick" (VBI frame equivalent) at a time.
 *
 * Not a bit-exact VERA hardware emulation: the 4 waveforms (pulse,
 * sawtooth, triangle, noise) are plain textbook shapes. Real VERA hardware
 * XORs the pulse-width value into the sawtooth/triangle output and clocks
 * noise from an LFSR with specific hardware timing quirks — neither is
 * reproduced here. Good enough to judge melody, timing and envelope
 * decisions; not a substitute for a final listen on real hardware/emulator.
 *
 * --image loads the ORIGINAL artwork straight through SDL2_image (PNG/JPEG/
 * BMP/...) or a VBM2 file (VERA Bitmap). Unlike the Atari side
 * (see vbm_display.s/tools/img2vbm.py), the PC preview has no 256-color-
 * palette hardware constraint, so for non-VBM formats there's no need for
 * a preconverted format — just load and display directly. It's centered
 * (scaled down to fit, never up) on a black 320x240 area — the same
 * framing VERA's 320x240 8bpp bitmap mode uses — so the preview's framing
 * matches what real hardware will show.
 *
 * On real hardware the title/VU meter (Atari's own screen) and the artwork
 * (VERA's own, separate video output) are two different monitors — one
 * PC window can't be two monitors, so instead it's one window stacked
 * vertically: the artwork on top (320x240, as above), a 4-channel VU
 * meter of the same kind test_player.c draws with Player/Missile graphics
 * underneath (320x128), reusing the live per-channel volume already
 * computed for mixing.
 *
 * Build:
 *   cc -O2 -o vtm_play vtm_play.c $(sdl2-config --cflags --libs) -lSDL2_image -lm
 *
 * Usage:
 *   ./vtm_play song.vtm                       play live (Ctrl+C to stop)
 *   ./vtm_play song.vtm --pal                  tick at 50 Hz instead of 60 Hz
 *   ./vtm_play song.vtm --image cover.png      show artwork + VU meter in a window
 *   ./vtm_play song.vtm --wav out.wav --seconds 8   render to a WAV file
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define CANVAS_W 320
#define IMAGE_H  240
#define VU_H     128             /* extra height below the artwork for the VU meter */
#define CANVAS_H (IMAGE_H + VU_H)

#define N_CHANNELS    4
#define NOTE_HOLD     0
#define NOTE_OFF_VAL  97
#define NO_INSTR      0xFF
#define MAX_INSTR     86        /* player limit, see vtm_format.md */
#define MAX_PATTERNS  86

#define SAMPLE_RATE   44100
#define VERA_HW_RATE  (25000000.0 / 512.0)   /* 48828.125 Hz, per VERA doc */
#define MASTER_GAIN   0.85

typedef struct {
    uint8_t reg2;    /* pan (bits7:6) + volume (bits5:0) */
    uint8_t reg3;    /* pulse width (bits7:2) + waveform (bits1:0) */
    uint8_t decay;
} Instrument;

typedef struct {
    uint8_t  n_rows;
    uint16_t data_offset;
} PatternEntry;

typedef struct {
    const uint8_t *data;
    size_t size;

    uint8_t n_channels, frames_per_row, n_instruments, n_patterns, order_len, loop_pos;
    uint8_t title_len;
    const uint8_t *title;    /* not null-terminated; title_len bytes */
    const uint8_t *order;
    Instrument instruments[MAX_INSTR];
    PatternEntry pattern_table[MAX_PATTERNS];
} Song;

typedef struct {
    uint8_t  instr;        /* NO_INSTR = never set */
    uint8_t  vol_pan;      /* live reg2 — decays over time */
    uint8_t  decay;
    uint8_t  waveform;
    uint8_t  pulse_width;
    uint16_t freq_word;
    double   phase;        /* 0..1, free-running (not reset on note-on) */
    uint32_t noise_lfsr;
    int      noise_bit;
} Channel;

typedef struct {
    const Song *song;
    Channel  ch[N_CHANNELS];
    uint8_t  order_idx;
    uint8_t  row_left;
    uint8_t  frame_ctr;
    uint16_t row_offset;
    int      tick_hz;
    double   sample_accum;
} Player;

/* Same formula/tuning as tools/gen_note_table.py: A-4 = 440 Hz = word 1181. */
static uint16_t note_freq_word(int note_index)
{
    double hz = 440.0 * pow(2.0, (note_index - 57) / 12.0);
    long w = lround(hz * 131072.0 / VERA_HW_RATE);
    if (w < 0) w = 0;
    if (w > 65535) w = 65535;
    return (uint16_t)w;
}

/* Forward declaration */
static uint8_t *load_file(const char *path, size_t *out_size);

/* VBM (VERA Bitmap) file loader.
 * VBM2 format: magic(4) + width(2) + height(2) + bpp(1) + name_len(1) + reserved(2)
 *              + name(variable) + palette(512) + pixels(width*height)
 * Palette: 256 entries x 2 bytes each
 *   byte0 = GGGGBBBB, byte1 = 0000RRRR
 * Returns NULL if not a valid VBM file. */
static SDL_Surface *vbm_load_surface(const char *path)
{
    size_t size;
    uint8_t *data = load_file(path, &size);
    if (!data) return NULL;

    if (size < 12 || memcmp(data, "VBM2", 4) != 0) {
        free(data);
        return NULL;
    }

    uint16_t w = *(uint16_t *)(data + 4);
    uint16_t h = *(uint16_t *)(data + 6);
    uint8_t bpp = data[8];
    uint8_t name_len = data[9];

    /* Validate basic structure */
    if (bpp != 8 || w > 320 || h > 240) {
        free(data);
        return NULL;
    }

    size_t palette_offset = 12 + name_len;
    size_t pixel_offset = palette_offset + 512;
    size_t expected_size = pixel_offset + (w * h);

    if (size < expected_size) {
        free(data);
        return NULL;
    }

    /* Create palette */
    SDL_Palette *pal = SDL_AllocPalette(256);
    if (!pal) {
        free(data);
        return NULL;
    }

    /* Convert from VERA palette format to SDL RGB.
     * VERA stores each entry as 2 bytes: byte0=GGGGBBBB, byte1=0000RRRR */
    SDL_Color palette[256];
    for (int i = 0; i < 256; i++) {
        uint8_t byte0 = data[palette_offset + i * 2];      /* GGGGBBBB */
        uint8_t byte1 = data[palette_offset + i * 2 + 1];  /* 0000RRRR */

        uint8_t r4 = byte1 & 0xF;
        uint8_t g4 = (byte0 >> 4) & 0xF;
        uint8_t b4 = byte0 & 0xF;

        /* Convert from 4-bit to 8-bit (multiply by 17: 0xF * 17 = 0xFF) */
        palette[i].r = r4 * 17;
        palette[i].g = g4 * 17;
        palette[i].b = b4 * 17;
        palette[i].a = 255;
    }
    SDL_SetPaletteColors(pal, palette, 0, 256);

    /* Create 8bpp indexed surface and set palette */
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_INDEX8);
    if (!fmt) {
        SDL_FreePalette(pal);
        free(data);
        return NULL;
    }
    SDL_SetPixelFormatPalette(fmt, pal);

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 8, SDL_PIXELFORMAT_INDEX8);
    if (!surf) {
        SDL_FreeFormat(fmt);
        SDL_FreePalette(pal);
        free(data);
        return NULL;
    }

    /* Set the palette on the surface */
    if (SDL_SetSurfacePalette(surf, pal) < 0) {
        SDL_FreeSurface(surf);
        SDL_FreePalette(pal);
        SDL_FreeFormat(fmt);
        free(data);
        return NULL;
    }

    /* Copy pixel data */
    memcpy(surf->pixels, data + pixel_offset, w * h);

    SDL_FreeFormat(fmt);

    free(data);
    return surf;
}

static uint8_t *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static int song_load(Song *s, const uint8_t *data, size_t size)
{
    if (size < 13 || memcmp(data, "VTM3", 4) != 0) return 0;

    s->data           = data;
    s->size           = size;
    s->n_channels     = data[4];
    s->frames_per_row = data[5];
    s->n_instruments  = data[6];
    s->n_patterns     = data[7];
    s->order_len      = data[8];
    s->loop_pos       = data[9];
    s->title_len      = data[12];

    if (s->n_channels != N_CHANNELS) return 0;
    if (s->n_instruments > MAX_INSTR || s->n_patterns > MAX_PATTERNS) return 0;
    if (s->order_len == 0 || s->loop_pos >= s->order_len) return 0;

    size_t off = 13;
    if (off + s->title_len > size) return 0;
    s->title = data + off;
    off += s->title_len;

    if (off + s->order_len > size) return 0;
    s->order = data + off;
    off += s->order_len;

    if (off + (size_t)s->n_instruments * 3 > size) return 0;
    for (int i = 0; i < s->n_instruments; i++) {
        s->instruments[i].reg2  = data[off++];
        s->instruments[i].reg3  = data[off++];
        s->instruments[i].decay = data[off++];
    }

    if (off + (size_t)s->n_patterns * 3 > size) return 0;
    for (int i = 0; i < s->n_patterns; i++) {
        s->pattern_table[i].n_rows      = data[off];
        s->pattern_table[i].data_offset = (uint16_t)(data[off + 1] | (data[off + 2] << 8));
        off += 3;
    }

    return 1;
}

static void player_load_pattern(Player *p)
{
    const Song *s = p->song;
    if (p->order_idx >= s->order_len) p->order_idx = s->loop_pos;
    uint8_t pat = s->order[p->order_idx];
    p->row_left   = s->pattern_table[pat].n_rows;
    p->row_offset = s->pattern_table[pat].data_offset;
}

static void player_init(Player *p, const Song *s, int tick_hz)
{
    memset(p, 0, sizeof(*p));
    p->song = s;
    p->tick_hz = tick_hz;
    for (int i = 0; i < N_CHANNELS; i++) {
        p->ch[i].instr = NO_INSTR;
        p->ch[i].noise_lfsr = 0x1234ACE1u ^ (uint32_t)(i * 0x9E3779B1u);
    }
    p->frame_ctr = 1;    /* fire row 0 on the very first tick, like vtm_player.s */
    p->order_idx = 0;
    player_load_pattern(p);
}

static void envelope_tick(Player *p)
{
    for (int i = 0; i < N_CHANNELS; i++) {
        Channel *c = &p->ch[i];
        if (!c->decay) continue;
        int vol = (c->vol_pan & 0x3F) - c->decay;
        if (vol < 0) vol = 0;
        c->vol_pan = (uint8_t)((c->vol_pan & 0xC0) | vol);
        if (vol == 0) c->decay = 0;
    }
}

static void apply_cell(Player *p, int ch_idx, uint8_t note, uint8_t instr)
{
    Channel *c = &p->ch[ch_idx];
    if (note == NOTE_HOLD) return;
    if (note == NOTE_OFF_VAL) {
        c->vol_pan = 0;
        c->decay = 0;
        return;
    }
    if (instr != NO_INSTR) c->instr = instr;
    if (c->instr == NO_INSTR) return;

    const Instrument *ins = &p->song->instruments[c->instr];
    c->vol_pan      = ins->reg2;
    c->decay        = ins->decay;
    c->waveform     = ins->reg3 & 0x03;
    c->pulse_width  = (ins->reg3 >> 2) & 0x3F;
    c->freq_word    = note_freq_word(note - 1);
}

static void play_row(Player *p)
{
    const uint8_t *row = p->song->data + p->row_offset;
    for (int i = 0; i < N_CHANNELS; i++)
        apply_cell(p, i, row[i * 2], row[i * 2 + 1]);
}

static void player_tick(Player *p)
{
    envelope_tick(p);

    if (--p->frame_ctr != 0) return;
    p->frame_ctr = p->song->frames_per_row;

    play_row(p);

    p->row_offset = (uint16_t)(p->row_offset + N_CHANNELS * 2);
    if (--p->row_left == 0) {
        p->order_idx++;
        player_load_pattern(p);
    }
}

static double waveform_sample(const Channel *c)
{
    double t = c->phase;
    switch (c->waveform) {
    case 0: {                                   /* pulse */
        double duty = c->pulse_width / 64.0;
        return (t < duty) ? 1.0 : -1.0;
    }
    case 1:                                      /* sawtooth */
        return 2.0 * t - 1.0;
    case 2:                                       /* triangle */
        return (t < 0.5) ? (4.0 * t - 1.0) : (3.0 - 4.0 * t);
    default:                                      /* noise: sample-and-hold */
        return c->noise_bit ? 1.0 : -1.0;
    }
}

static void mix_sample(Player *p, double *out_l, double *out_r)
{
    double l = 0.0, r = 0.0;

    for (int i = 0; i < N_CHANNELS; i++) {
        Channel *c = &p->ch[i];
        int vol = c->vol_pan & 0x3F;
        if (vol == 0 || c->freq_word == 0) continue;

        double freq_hz = c->freq_word * (VERA_HW_RATE / 131072.0);
        double step = freq_hz / SAMPLE_RATE;

        c->phase += step;
        if (c->phase >= 1.0) {
            c->phase -= floor(c->phase);
            if (c->waveform == 3) {
                uint32_t x = c->noise_lfsr;
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                c->noise_lfsr = x;
                c->noise_bit = x & 1;
            }
        }

        double s = waveform_sample(c) * (vol / 63.0);

        int pan = c->vol_pan & 0xC0;
        if (pan == 0) pan = 0xC0;    /* neither L nor R set — treat as center */
        if (pan & 0x40) l += s;
        if (pan & 0x80) r += s;
    }

    *out_l = l;
    *out_r = r;
}

static int16_t clamp16(double x)
{
    double v = x * (32767.0 * MASTER_GAIN / N_CHANNELS);
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)v;
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    Player *p = (Player *)userdata;
    int16_t *out = (int16_t *)stream;
    int nframes = len / 4;                       /* stereo, 16-bit */
    double samples_per_tick = (double)SAMPLE_RATE / p->tick_hz;

    for (int i = 0; i < nframes; i++) {
        p->sample_accum += 1.0;
        if (p->sample_accum >= samples_per_tick) {
            p->sample_accum -= samples_per_tick;
            player_tick(p);
        }
        double l, r;
        mix_sample(p, &l, &r);
        out[i * 2 + 0] = clamp16(l);
        out[i * 2 + 1] = clamp16(r);
    }
}

static void write_wav_header(FILE *f, uint32_t data_bytes, int sample_rate, int channels)
{
    uint32_t byte_rate   = (uint32_t)(sample_rate * channels * 2);
    uint16_t block_align = (uint16_t)(channels * 2);
    uint32_t riff_size   = 36 + data_bytes;
    uint32_t fmt_size    = 16;
    uint16_t audio_fmt   = 1;
    uint16_t nch         = (uint16_t)channels;
    uint32_t sr          = (uint32_t)sample_rate;
    uint16_t bits        = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_fmt, 2, 1, f);
    fwrite(&nch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

static int render_wav(Player *p, const char *path, double seconds)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fseek(f, 44, SEEK_SET);          /* header written last, once size is known */

    long n_frames = (long)(seconds * SAMPLE_RATE);
    double samples_per_tick = (double)SAMPLE_RATE / p->tick_hz;

    for (long i = 0; i < n_frames; i++) {
        p->sample_accum += 1.0;
        if (p->sample_accum >= samples_per_tick) {
            p->sample_accum -= samples_per_tick;
            player_tick(p);
        }
        double l, r;
        mix_sample(p, &l, &r);
        int16_t sl = clamp16(l), sr = clamp16(r);
        fwrite(&sl, 2, 1, f);
        fwrite(&sr, 2, 1, f);
    }

    uint32_t data_bytes = (uint32_t)(n_frames * 4);
    fseek(f, 0, SEEK_SET);
    write_wav_header(f, data_bytes, SAMPLE_RATE, 2);
    fclose(f);
    return 1;
}

/* Calculate window scale dynamically based on display height.
 * Use at most 3/4 of the screen height, maintain aspect ratio.
 * If force_scale > 0, use that value instead. */
static int calculate_window_scale(int force_scale)
{
    if (force_scale > 0) return force_scale;

    SDL_DisplayMode mode;
    if (SDL_GetDisplayMode(0, 0, &mode) != 0) {
        /* fallback to scale 1 if display mode query fails */
        return 1;
    }

    /* Maximum usable height is 3/4 of display height */
    int max_height = (int)(mode.h * 0.75);

    /* Calculate scale needed to fit CANVAS_H into max_height */
    int scale = max_height / CANVAS_H;

    /* Ensure at least scale of 1 */
    return scale > 1 ? scale : 1;
}

/* Centers src (any size) on the CANVAS_W x IMAGE_H artwork area (the top
 * of the window — the VU meter strip below is separate), scaling down to
 * fit if it's larger in either dimension — never scaled up, so a small
 * image stays crisp with black letterboxing around it rather than
 * blurring to fill the frame. Matches the framing tools/img2vbm.py bakes
 * into the Atari-side .vbm file at conversion time. */
static SDL_Rect centered_dest_rect(int src_w, int src_h)
{
    double scale = 1.0;
    if (src_w > CANVAS_W) scale = (double)CANVAS_W / src_w;
    if (src_h > IMAGE_H && (double)IMAGE_H / src_h < scale) scale = (double)IMAGE_H / src_h;

    SDL_Rect r;
    r.w = (int)(src_w * scale);
    r.h = (int)(src_h * scale);
    r.x = (CANVAS_W - r.w) / 2;
    r.y = (IMAGE_H - r.h) / 2;
    return r;
}

/* VU meter: 4 bars, bottom-anchored, growing upward, same 8-band green-to-
 * red gradient idea as vu_pm.s's Player/Missile meter on the Atari side —
 * band 0 here is the top-most/loudest (red), band VU_BANDS-1 is the
 * bottom/baseline (green, shown as soon as a channel has ever sounded). */
#define VU_BANDS  8
#define VU_BAND_H (VU_H / VU_BANDS)
#define VU_FLOOR  3               /* out of 63 — bars never fully vanish */
#define VU_DECAY  6               /* per-rendered-frame falloff */

static double vu_level[N_CHANNELS];   /* 0-63, smoothed for display */

static const uint8_t vu_colors[VU_BANDS][3] = {
    {220, 30, 30},    /* red    (top)    */
    {220, 90, 20},
    {220, 90, 20},
    {220, 170, 20},
    {170, 210, 20},
    {170, 210, 20},
    {40, 210, 40},
    {40, 210, 40},    /* green (bottom) */
};

/* Reads the Player's live per-channel volume (same vol_pan bits mix_sample()
 * uses) — no locking against the audio thread that writes it: a torn read
 * of a single byte isn't possible on any real CPU, and a VU meter doesn't
 * need sample-accurate timing anyway. */
static void vu_tick(const Player *p)
{
    int i;
    for (i = 0; i < N_CHANNELS; i++) {
        double raw = p->ch[i].vol_pan & 0x3F;
        if (raw >= vu_level[i]) {
            vu_level[i] = raw;
        } else if (vu_level[i] > VU_FLOOR + VU_DECAY) {
            vu_level[i] -= VU_DECAY;
        } else {
            vu_level[i] = VU_FLOOR;
        }
    }
}

static void draw_vu_meters(SDL_Renderer *ren)
{
    int ch, band, slot_w, bar_w;

    slot_w = CANVAS_W / N_CHANNELS;
    bar_w = slot_w - 24;

    for (ch = 0; ch < N_CHANNELS; ch++) {
        int lit = ((int)vu_level[ch] * VU_BANDS) / 63;
        if (lit == 0 && vu_level[ch] > 0) lit = 1;

        for (band = VU_BANDS - 1; band >= VU_BANDS - lit; band--) {
            SDL_Rect r;
            r.x = ch * slot_w + (slot_w - bar_w) / 2;
            r.y = IMAGE_H + band * VU_BAND_H;
            r.w = bar_w;
            r.h = VU_BAND_H;
            SDL_SetRenderDrawColor(ren, vu_colors[band][0], vu_colors[band][1], vu_colors[band][2], 255);
            SDL_RenderFillRect(ren, &r);
        }
    }
}

int main(int argc, char **argv)
{
    const char *song_path = NULL;
    const char *wav_path = NULL;
    const char *image_path = NULL;
    double wav_seconds = 8.0;
    int tick_hz = 60;
    int force_scale = 0;  /* 0 means auto-calculate */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pal") == 0) {
            tick_hz = 50;
        } else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            wav_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            image_path = argv[++i];
        } else if (strncmp(argv[i], "--scale=", 8) == 0) {
            force_scale = atoi(argv[i] + 8);
            if (force_scale < 1) force_scale = 1;
        } else if (!song_path) {
            song_path = argv[i];
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }
    if (!song_path) {
        fprintf(stderr, "usage: %s song.vtm [--pal] [--image art.png|art.vbm] [--scale=N] [--wav out.wav --seconds N]\n", argv[0]);
        return 1;
    }

    size_t size;
    uint8_t *data = load_file(song_path, &size);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", song_path);
        return 1;
    }

    Song song;
    if (!song_load(&song, data, size)) {
        fprintf(stderr, "%s: not a valid VTM3 file\n", song_path);
        free(data);
        return 1;
    }

    Player player;
    player_init(&player, &song, tick_hz);

    if (wav_path) {
        int ok = render_wav(&player, wav_path, wav_seconds);
        free(data);
        if (!ok) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            return 1;
        }
        printf("%s: rendered %.1fs at %d Hz ticks\n", wav_path, wav_seconds, tick_hz);
        return 0;
    }

    Uint32 sdl_flags = SDL_INIT_AUDIO | (image_path ? SDL_INIT_VIDEO : 0);
    if (SDL_Init(sdl_flags) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(data);
        return 1;
    }

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = audio_callback;
    want.userdata = &player;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(data);
        return 1;
    }

    SDL_Window   *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture  *tex = NULL;
    SDL_Rect      dest;

    if (image_path) {
        SDL_Surface *surf = NULL;

        /* Try loading as VBM first (if it ends with .vbm or .VBM) */
        const char *ext = strrchr(image_path, '.');
        if (ext && (strcasecmp(ext, ".vbm") == 0)) {
            surf = vbm_load_surface(image_path);
            if (!surf) {
                fprintf(stderr, "cannot load VBM file %s\n", image_path);
                SDL_CloseAudioDevice(dev);
                SDL_Quit();
                free(data);
                return 1;
            }
        } else {
            /* Load as standard image (PNG, JPEG, etc.) via SDL_image */
            surf = IMG_Load(image_path);
            if (!surf) {
                fprintf(stderr, "cannot load %s: %s\n", image_path, IMG_GetError());
                SDL_CloseAudioDevice(dev);
                SDL_Quit();
                free(data);
                return 1;
            }
        }

        int init_scale = calculate_window_scale(force_scale);
        win = SDL_CreateWindow(song_path, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                CANVAS_W * init_scale, CANVAS_H * init_scale,
                                SDL_WINDOW_RESIZABLE);
        ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
        if (!win || !ren) {
            fprintf(stderr, "SDL window/renderer failed: %s\n", SDL_GetError());
            SDL_FreeSurface(surf);
            SDL_CloseAudioDevice(dev);
            SDL_Quit();
            free(data);
            return 1;
        }
        SDL_RenderSetLogicalSize(ren, CANVAS_W, CANVAS_H);

        dest = centered_dest_rect(surf->w, surf->h);
        tex = SDL_CreateTextureFromSurface(ren, surf);
        SDL_FreeSurface(surf);
        if (!tex) {
            fprintf(stderr, "SDL_CreateTextureFromSurface failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
            SDL_CloseAudioDevice(dev);
            SDL_Quit();
            free(data);
            return 1;
        }
    }

    if (song.title_len)
        printf("Playing %.*s (%s)", song.title_len, song.title, song_path);
    else
        printf("Playing %s", song_path);
    printf(image_path ? " — close the window or Ctrl+C to stop.\n" : " — Ctrl+C to stop.\n");

    SDL_PauseAudioDevice(dev, 0);

    if (!image_path) {
        for (;;) SDL_Delay(200);
    }

    for (;;) {
        SDL_Event ev;
        int quit = 0;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) quit = 1;
            /* With SDL_RenderSetLogicalSize, window resize is handled automatically:
             * SDL scales the logical 320x368 canvas to fit the new window size,
             * maintaining aspect ratio with letterboxing if needed. */
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                /* Resize event acknowledged; SDL renderer handles scaling. */
            }
        }
        if (quit) break;

        vu_tick(&player);

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, &dest);
        draw_vu_meters(ren);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    free(data);
    return 0;
}
