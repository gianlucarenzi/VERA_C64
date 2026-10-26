/* test_pacman.c - a simple Pac-Man on the VERA, with the graphics and the
 * sounds of the arcade (Namco 1980), on the C64.
 *
 * The ROMs are not in the repository: pacman_gfx.py converts the graphics of
 * the MAME set "pacman" (PACMAN_ZIP in the Makefile) and pacman_snd.py the
 * sounds, ripped by running the game in MAME with pacman_rip.lua; both write
 * to gen/.
 *
 * Picture: the 224x288 screen of the rotated arcade monitor on layer 0 (the
 * 256 tiles, 2 bpp, map 32x64 of which 28x36 are used), scaled by 128/77 to
 * the 480 lines of the VGA output.  The active area (DC_HSTART/HSTOP) is
 * 376 pixels wide, the arcade screen, so the sprites in the tunnel vanish at
 * the edges as on the arcade.  Sprites 16x16, 4 bpp.  The colour code of a
 * tile or sprite of the arcade is a palette offset (pacgfx.h).
 *
 * Sound: the 3 voices of the Namco WSG on the 3 SID voices, frame by frame
 * (pacsnd.h): voice 0 the intro and the extra life, voice 1 the intro and
 * the siren / frightened ghosts / eyes, voice 2 dots, fruit, ghost eaten and
 * the death.
 *
 * Joystick in port 2; fire starts a game.  The game runs at the VSYNC of the
 * VERA (60 Hz), with the IRQs of the C64 off.
 *
 * Simpler than the arcade: no cornering, no "Elroy" speed-up, no
 * intermissions, the cherry as the only fruit, ghosts leave the house by dot
 * counts and an idle timer only, the eyes of an eaten ghost do not move while
 * its score is shown.
 */

#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include "detect.h"
#include "gen/pacgfx.h"
#include "gen/pacsnd.h"

#define CARDSEL (*(volatile unsigned char *)0xDFFD)  /* VERA select latch (RBL-C64 card, VICE) */
#define JOY2    (*(volatile unsigned char *)0xDC00)
#define SIDREG  ((volatile unsigned char *)0xD400)

#define VRAM_MAP     0x0000u        /* layer 0: map 32x64, 2 bytes per entry */
#define VRAM_TILES   0x2000u        /* 256 tiles 8x8, 2 bpp */
#define VRAM_SPRITES 0x4000u        /* 64 sprites 16x16, 4 bpp */

/* tile codes of the arcade */
#define T_BLANK  0x40
#define T_DOT    0x10
#define T_POWER  0x14
#define T_WALL   0xC0               /* and above: walls and the ghost house door */
#define T_DOOR1  0xCE
#define T_DOOR2  0xCF

/* sprite codes of the arcade */
#define S_CHERRY 0
#define S_FRIGHT 28                 /* + frame */
#define S_GHOST  32                 /* + direction * 2 + frame */
#define S_SCORE  40                 /* 200, 400, 800, 1600 */
#define S_PAC_C  48                 /* closed; 44/46 open right, 45/47 open down */
#define S_DEATH  52                 /* 12 frames */

/* sprite slots of the VERA */
#define SP_GHOST 0                  /* 4 ghosts */
#define SP_PAC   4
#define SP_FRUIT 5
#define SP_LIVES 6                  /* 3 */
#define SP_LEVEL 9
#define SP_COUNT 10

#define RIGHT 0                     /* the order of the ghost sprites */
#define DOWN  1
#define LEFT  2
#define UP    3

/* positions: centre of the sprite in screen pixels, y from screen row 3 (the
 * top of the maze) so that it fits a byte; x wraps in the tunnel through
 * 224-255, off screen */
#define TX(x)   ((x) >> 3)
#define TY(y)   (((y) >> 3) + 3)
#define CENTRE(x, y) (((x) & 7) == 4 && ((y) & 7) == 4)

#define PAC_X0    112
#define PAC_Y0    188               /* row 26 */
#define HOUSE_X   112
#define HOUSE_Y   116               /* row 17, inside */
#define DOOR_Y    92                /* row 14, above the door */
#define FRUIT_Y   140               /* row 20 */

/* speeds in 1/64 pixel per frame */
#define SP_PAC_N   64
#define SP_PAC_F   72
#define SP_GHOST_N 60
#define SP_GHOST_F 40
#define SP_TUNNEL  32
#define SP_EYES    128
#define SP_HOUSE   32

/* ghost states */
#define G_HOUSE    0
#define G_LEAVING  1
#define G_ACTIVE   2
#define G_EYES     3
#define G_ENTERING 4

/* game phases */
#define PH_TITLE    0
#define PH_READY    1
#define PH_PLAY     2
#define PH_DYING    3
#define PH_LEVEL    4
#define PH_GAMEOVER 5

static const signed char dx[4] = { 1, 0, -1, 0 };
static const signed char dy[4] = { 0, 1, 0, -1 };

/* scatter corners (screen tiles) */
static const signed char corner_x[4] = { 25, 2, 27, 0 };
static const signed char corner_y[4] = { 0, 0, 35, 35 };
static const unsigned char ghost_x0[4] = { 112, 112, 96, 128 };
static const unsigned char house_dots[4] = { 0, 0, 30, 60 };

/* scatter / chase periods in frames, the last chase forever */
static const unsigned int mode_frames[8] = { 420, 1200, 420, 1200, 300, 1200, 300, 0 };

static unsigned char maze[36 * 28];
static unsigned char *mrow[36];
static unsigned int sq[64];
static unsigned char total_dots;

static unsigned char phase, phase_t2;
static unsigned int phase_t;
static unsigned char level, lives, extra_given;
/* the scores as 7 decimal digits, the most significant first */
static unsigned char score_d[7], hi_d[7];
static unsigned char dots, life_dots, idle;
static unsigned int fruit_left;

static unsigned char px, py, pdir, pwant, pacc, panim, pmoving, pac_hidden;
static unsigned char gx[4], gy[4], gdir[4], gacc[4], gstate[4], gfright[4];
static signed char tgx, tgy;
static unsigned int fright_t, mode_t;
static unsigned char mode_i, ghosts_eaten, score_ghost;
static unsigned char freeze;
static unsigned char anim;

/* ------------------------------------------------------------------ */
/* VERA                                                                 */
/* ------------------------------------------------------------------ */

static void vram_at(unsigned int addr, unsigned char bank)
{
    VERA_CTRL = 0;
    VERA_ADDR_L = (unsigned char)addr;
    VERA_ADDR_M = (unsigned char)(addr >> 8);
    VERA_ADDR_H = VERA_INC1 | bank;
}

static void vram_copy(unsigned int addr, const unsigned char *src, unsigned int n)
{
    vram_at(addr, 0);
    while (n--) VERA_DATA0 = *src++;
}

static void put_tile(unsigned char tx, unsigned char ty, unsigned char code, unsigned char pal)
{
    vram_at(VRAM_MAP + ((unsigned int)ty << 6) + (tx << 1), 0);
    VERA_DATA0 = code;
    VERA_DATA0 = pal << 4;
}

/* The strings are written in lower case: cc65 turns them into PETSCII,
 * whose $41-$5A are the letters of the arcade tiles.  ' ' and '!' have tiles
 * of their own. */
static void put_text(unsigned char tx, unsigned char ty, const char *s, unsigned char pal)
{
    unsigned char c;

    vram_at(VRAM_MAP + ((unsigned int)ty << 6) + (tx << 1), 0);
    while ((c = *s++) != 0) {
        VERA_DATA0 = c == ' ' ? T_BLANK : c == '!' ? 0x5B : c;
        VERA_DATA0 = pal << 4;
    }
}

/* the tiles of the maze again where a text was */
static void clear_text(unsigned char tx, unsigned char ty, unsigned char n)
{
    unsigned char *m = mrow[ty] + tx;

    vram_at(VRAM_MAP + ((unsigned int)ty << 6) + (tx << 1), 0);
    while (n--) {
        VERA_DATA0 = *m++;
        VERA_DATA0 = PAL_MAZE << 4;
    }
}

/* a score ending at column right, blank up to the last two digits */
static void put_digits(unsigned char right, unsigned char ty, const unsigned char *d)
{
    unsigned char k, lead = 1;

    vram_at(VRAM_MAP + ((unsigned int)ty << 6) + ((right - 6) << 1), 0);
    for (k = 0; k < 7; ++k) {
        if (d[k] || k >= 5) lead = 0;
        VERA_DATA0 = lead ? T_BLANK : '0' + d[k];
        VERA_DATA0 = PAL_WHITE << 4;
    }
}

static void draw_maze(void)
{
    unsigned char x, y, c;

    for (y = 0; y < 36; ++y) {
        vram_at(VRAM_MAP + ((unsigned int)y << 6), 0);
        for (x = 0; x < 32; ++x) {
            c = x < 28 ? mrow[y][x] : T_BLANK;
            VERA_DATA0 = c;
            VERA_DATA0 = (c == T_DOOR1 || c == T_DOOR2 ? PAL_DOOR : PAL_MAZE) << 4;
        }
    }
}

static void draw_status(void)
{
    put_text(3, 0, "1up", PAL_WHITE);
    put_text(9, 0, "high score", PAL_WHITE);
    put_digits(6, 1, score_d);
    put_digits(16, 1, hi_d);
}

/* the colour of the walls: blue, or white while the maze flashes */
static void wall_colour(unsigned char white)
{
    vram_at(0xFA00u + PAL_MAZE * 32 + 3 * 2, 1);
    VERA_DATA0 = white ? 0xDF : 0x2F;       /* $dedeff / $2121ff */
    VERA_DATA0 = white ? 0x0D : 0x02;
}

static void wait_frame(void)
{
    while (!(VERA_ISR & 1))
        ;
    VERA_ISR = 1;
}

static void video_init(void)
{
    unsigned int i;
    unsigned char out;

    VERA_IEN = 0;
    VERA_ISR = 7;
    VERA_CTRL = 0;
    out = VERA_DC_VIDEO & 0x0F;             /* output mode as it is, VGA if off */
    if (!(out & 3)) out |= 1;
    VERA_DC_VIDEO = out;                    /* layers off while loading */

    vram_copy(VRAM_TILES, pac_tiles, PAC_TILES_SIZE);
    vram_copy(VRAM_SPRITES, pac_sprites, PAC_SPRITES_SIZE);
    vram_at(0xFA00u, 1);                    /* palette: PAL_GROUPS groups of 16, 4 used */
    for (i = 0; i < PAL_GROUPS * 4; ++i) {
        VERA_DATA0 = pac_palette[i * 2];
        VERA_DATA0 = pac_palette[i * 2 + 1];
        if ((i & 3) == 3) {
            unsigned char k;
            for (k = 0; k < 24; ++k) VERA_DATA0 = 0;
        }
    }
    vram_at(0xFC00u, 1);                    /* all 128 sprites off */
    for (i = 0; i < 1024; ++i) VERA_DATA0 = 0;
    vram_at(0xF9C0u, 1);                    /* PSG voices silent */
    for (i = 0; i < 64; ++i) VERA_DATA0 = 0;
    vram_at(VRAM_MAP, 0);                   /* the whole map blank */
    for (i = 0; i < 32 * 64; ++i) {
        VERA_DATA0 = T_BLANK;
        VERA_DATA0 = 0;
    }

    VERA_L0_CONFIG = 0x41;                  /* map 32x64, 2 bpp tiles */
    VERA_L0_MAPBASE = VRAM_MAP >> 9;
    VERA_L0_TILEBASE = (VRAM_TILES >> 11) << 2;   /* 8x8 */
    VERA_L0_HSCR_L = 0; VERA_L0_HSCR_H = 0;
    VERA_L0_VSCR_L = 0; VERA_L0_VSCR_H = 0;
    VERA_DC_HSCALE = 77;                    /* 288 lines -> 480 */
    VERA_DC_VSCALE = 77;
    VERA_DC_BORDER = 0;
    VERA_CTRL = 0x02;                       /* DCSEL 1 */
    VERA_DC_HSTART = 33;                    /* 132..508: 226 pixels of the layer */
    VERA_DC_HSTOP = 127;
    VERA_DC_VSTART = 0;
    VERA_DC_VSTOP = 240;
    VERA_CTRL = 0;
    VERA_DC_VIDEO = out | 0x50;             /* layer 0 and sprites */
}

/* ------------------------------------------------------------------ */
/* Sound: the clips of pacsnd.s on the SID                              */
/* ------------------------------------------------------------------ */

static unsigned char sv_clip[3] = { 0xFF, 0xFF, 0xFF };
static unsigned char sv_left[3], sv_vol[3], sv_wave[3];
static const unsigned char *sv_cur[3];

static void snd_play(unsigned char v, unsigned char clip)
{
    sv_clip[v] = clip;
    sv_cur[v] = (const unsigned char *)(snd_ptr_lo[clip] | (snd_ptr_hi[clip] << 8));
    sv_left[v] = snd_len[clip];
}

static void snd_stop(unsigned char v)
{
    sv_clip[v] = 0xFF;
    SIDREG[v * 7 + 4] = sv_wave[v];            /* gate off */
    sv_vol[v] = 0;
}

static void snd_stop_all(void)
{
    snd_stop(0);
    snd_stop(1);
    snd_stop(2);
}

/* voice 1: a continuous sound, started only when it changes */
static void snd_background(unsigned char clip)
{
    if (sv_clip[1] != clip) snd_play(1, clip);
}

/* voice 2: one shots, a later clip of pacsnd.h (dots < fruit < ghost < death)
 * is not cut by an earlier one */
static void snd_effect(unsigned char clip)
{
    if (sv_clip[2] != 0xFF && sv_clip[2] > SND_DOT2 && sv_clip[2] > clip) return;
    snd_play(2, clip);
}

/* One frame of the 3 voices.  The volume of the WSG is the sustain level:
 * a louder frame retriggers the envelope (attack 2 ms), a quieter one lowers
 * the sustain (the SID decays to it). */
static void snd_frame(void)
{
    static unsigned char v, vol, c;
    static volatile unsigned char *r;
    static const unsigned char *p;

    for (v = 0; v < 3; ++v) {
        if (sv_clip[v] == 0xFF) continue;
        if (!sv_left[v]) {
            if (!snd_loop[sv_clip[v]]) {
                snd_stop(v);
                continue;
            }
            snd_play(v, sv_clip[v]);
        }
        p = sv_cur[v];
        sv_cur[v] = p + 3;
        --sv_left[v];
        r = SIDREG + v * 7;
        c = p[2];
        vol = c & 0x0F;
        if (!vol) {
            r[4] = sv_wave[v];
            sv_vol[v] = 0;
            continue;
        }
        sv_wave[v] = c & 0xF0;
        r[0] = p[0];
        r[1] = p[1];
        if (vol > sv_vol[v]) {
            r[6] = vol << 4;
            r[4] = sv_wave[v];
            r[4] = sv_wave[v] | 1;
        } else {
            if (vol < sv_vol[v]) r[6] = vol << 4;
            r[4] = sv_wave[v] | 1;
        }
        sv_vol[v] = vol;
    }
}

static void sid_init(void)
{
    unsigned char i;

    for (i = 0; i < 0x18; ++i) SIDREG[i] = 0;
    SIDREG[0x18] = 0x0F;                       /* volume 15, no filter */
}

/* ------------------------------------------------------------------ */
/* Maze                                                                 */
/* ------------------------------------------------------------------ */

static unsigned char tile_at(unsigned char tx, unsigned char ty)
{
    if (tx >= 28) return ty == 17 ? T_BLANK : T_WALL;   /* the tunnel */
    return mrow[ty][tx];
}

static unsigned char can_go(unsigned char x, unsigned char y, unsigned char d)
{
    return tile_at(TX(x) + dx[d], TY(y) + dy[d]) < T_WALL;
}

static void maze_reset(void)
{
    unsigned int i;

    memcpy(maze, pac_maze, sizeof maze);
    total_dots = 0;
    for (i = 0; i < sizeof maze; ++i)
        if (maze[i] == T_DOT || maze[i] == T_POWER) ++total_dots;
    dots = 0;
    draw_maze();
}

/* ------------------------------------------------------------------ */
/* Ghosts                                                               */
/* ------------------------------------------------------------------ */

static unsigned char chase(void)
{
    return mode_i & 1;
}

static void ghost_target(unsigned char i)
{
    static signed char ptx, pty, ox, oy;

    if (gstate[i] == G_EYES) {
        tgx = 13; tgy = 14;
        return;
    }
    if (!chase()) {
        tgx = corner_x[i]; tgy = corner_y[i];
        return;
    }
    ptx = TX(px); pty = TY(py);
    if (px >= 224) ptx = px >= 240 ? -1 : 28;
    switch (i) {
    case 0:                                 /* Blinky: Pac-Man */
        tgx = ptx; tgy = pty;
        break;
    case 1:                                 /* Pinky: 4 ahead (up: also 4 left) */
        tgx = ptx + 4 * dx[pdir]; tgy = pty + 4 * dy[pdir];
        if (pdir == UP) tgx -= 4;
        break;
    case 2:                                 /* Inky: Blinky mirrored on 2 ahead */
        ox = ptx + 2 * dx[pdir]; oy = pty + 2 * dy[pdir];
        if (pdir == UP) ox -= 2;
        tgx = 2 * ox - TX(gx[0]); tgy = 2 * oy - TY(gy[0]);
        break;
    default:                                /* Clyde: Pac-Man while 8 tiles away */
        ox = TX(gx[3]) - ptx; oy = TY(gy[3]) - pty;
        if (ox < 0) ox = -ox;
        if (oy < 0) oy = -oy;
        if (ox > 63) ox = 63;
        if (oy > 63) oy = 63;
        if (sq[ox] + sq[oy] > 64) { tgx = ptx; tgy = pty; }
        else { tgx = corner_x[3]; tgy = corner_y[3]; }
        break;
    }
}

/* At the centre of a tile: the open direction (not back) closest to the
 * target, ties in the order up, left, down, right; frightened: at random. */
static void ghost_turn(unsigned char i)
{
    static const unsigned char order[4] = { UP, LEFT, DOWN, RIGHT };
    static unsigned char k, d, best, back, tx, ty;
    static signed char ax, ay;
    static unsigned int bd, dd;

    tx = TX(gx[i]); ty = TY(gy[i]);
    back = gdir[i] ^ 2;
    if (gfright[i] && gstate[i] == G_ACTIVE) {
        d = rand() & 3;
        for (k = 0; k < 4; ++k, d = (d + 1) & 3)
            if (d != back && tile_at(tx + dx[d], ty + dy[d]) < T_WALL) {
                gdir[i] = d;
                return;
            }
        return;
    }
    ghost_target(i);
    best = gdir[i];
    bd = 0xFFFF;
    for (k = 0; k < 4; ++k) {
        d = order[k];
        if (d == back || tile_at(tx + dx[d], ty + dy[d]) >= T_WALL) continue;
        /* no turns up above the house and above Pac-Man's start */
        if (d == UP && gstate[i] == G_ACTIVE && (tx == 12 || tx == 15) && (ty == 14 || ty == 26))
            continue;
        ax = tx + dx[d] - tgx;
        ay = ty + dy[d] - tgy;
        if (ax < 0) ax = -ax;
        if (ay < 0) ay = -ay;
        if (ax > 63) ax = 63;
        if (ay > 63) ay = 63;
        dd = sq[ax] + sq[ay];
        if (dd < bd) { bd = dd; best = d; }
    }
    gdir[i] = best;
}

static void ghost_step(unsigned char i)
{
    switch (gstate[i]) {
    case G_HOUSE:                           /* up and down inside */
        if (gdir[i] == UP) { if (--gy[i] <= HOUSE_Y - 4) gdir[i] = DOWN; }
        else { if (++gy[i] >= HOUSE_Y + 4) gdir[i] = UP; }
        return;
    case G_LEAVING:
        if (gx[i] < HOUSE_X) { ++gx[i]; gdir[i] = RIGHT; }
        else if (gx[i] > HOUSE_X) { --gx[i]; gdir[i] = LEFT; }
        else if (gy[i] > DOOR_Y) { --gy[i]; gdir[i] = UP; }
        else { gstate[i] = G_ACTIVE; gdir[i] = LEFT; }
        return;
    case G_ENTERING:
        if (gy[i] < HOUSE_Y) { ++gy[i]; gdir[i] = DOWN; }
        else { gstate[i] = G_LEAVING; gfright[i] = 0; }
        return;
    }
    if (CENTRE(gx[i], gy[i])) {
        if (gstate[i] == G_EYES && TY(gy[i]) == 14 && (TX(gx[i]) == 13 || TX(gx[i]) == 14)) {
            gx[i] = HOUSE_X;                /* home: down through the door */
            gstate[i] = G_ENTERING;
            return;
        }
        ghost_turn(i);
    }
    gx[i] += dx[gdir[i]];
    gy[i] += dy[gdir[i]];
}

static unsigned char ghost_speed(unsigned char i)
{
    switch (gstate[i]) {
    case G_EYES:     return SP_EYES;
    case G_ENTERING: return SP_EYES / 2;
    case G_ACTIVE:   break;
    default:         return SP_HOUSE;
    }
    if (gfright[i]) return SP_GHOST_F;
    if (TY(gy[i]) == 17 && (gx[i] < 48 || gx[i] >= 176)) return SP_TUNNEL;
    return level > 1 ? SP_GHOST_N + 6 : SP_GHOST_N;
}

static void ghosts_reverse(void)
{
    unsigned char i;

    for (i = 0; i < 4; ++i)
        if (gstate[i] == G_ACTIVE) gdir[i] ^= 2;
}

static void ghosts_release(void)
{
    unsigned char i;

    for (i = 1; i < 4; ++i)
        if (gstate[i] == G_HOUSE) {
            if (life_dots >= house_dots[i] || idle >= 240) {
                gstate[i] = G_LEAVING;
                idle = 0;
            }
            return;                         /* one at a time, in order */
        }
}

/* ------------------------------------------------------------------ */
/* Pac-Man                                                              */
/* ------------------------------------------------------------------ */

/* v (0-9) at digit k of the score, with the carry */
static void add_digit(unsigned char k, unsigned char v)
{
    score_d[k] += v;
    while (score_d[k] >= 10 && k) {
        score_d[k] -= 10;
        ++score_d[--k];
    }
}

/* points in tens (decimal digits: no 32 bit divisions on the 6502) */
static void add_score(unsigned char tens)
{
    while (tens >= 100) { tens -= 100; add_digit(3, 1); }
    while (tens >= 10) { tens -= 10; add_digit(4, 1); }
    add_digit(5, tens);
    if (!extra_given && (score_d[0] | score_d[1] | score_d[2])) {   /* 10000 */
        extra_given = 1;
        ++lives;
        snd_play(0, SND_XLIFE);
    }
    if (memcmp(score_d, hi_d, sizeof hi_d) > 0) {
        memcpy(hi_d, score_d, sizeof hi_d);
        put_digits(16, 1, hi_d);
    }
    put_digits(6, 1, score_d);
}

static void frighten(void)
{
    unsigned char i;
    static const unsigned char secs[6] = { 6, 5, 4, 3, 2, 1 };

    fright_t = secs[level > 6 ? 5 : level - 1] * 60;
    ghosts_eaten = 0;
    for (i = 0; i < 4; ++i)
        if (gstate[i] != G_EYES && gstate[i] != G_ENTERING) {
            if (gstate[i] == G_ACTIVE) gdir[i] ^= 2;
            gfright[i] = 1;
        }
}

static void eat(void)
{
    unsigned char tx = TX(px), ty = TY(py), t;

    if (tx >= 28) return;
    t = mrow[ty][tx];
    if (t != T_DOT && t != T_POWER) return;
    mrow[ty][tx] = T_BLANK;
    put_tile(tx, ty, T_BLANK, PAL_MAZE);
    ++dots;
    ++life_dots;
    idle = 0;
    snd_effect(dots & 1 ? SND_DOT1 : SND_DOT2);
    if (t == T_POWER) {
        add_score(5);
        frighten();
    } else {
        add_score(1);
    }
    if (dots == 70 || dots == 170) fruit_left = 600;
}

static void pac_step(void)
{
    if (CENTRE(px, py)) {
        eat();
        if (pwant != pdir && can_go(px, py, pwant)) pdir = pwant;
        if (!can_go(px, py, pdir)) {
            pmoving = 0;
            return;
        }
    } else if (pwant == (pdir ^ 2)) {
        pdir = pwant;
    }
    pmoving = 1;
    px += dx[pdir];
    py += dy[pdir];
    ++panim;
}

static void read_joystick(void)
{
    unsigned char j = ~JOY2;

    if (j & 0x08) pwant = RIGHT;
    else if (j & 0x04) pwant = LEFT;
    else if (j & 0x01) pwant = UP;
    else if (j & 0x02) pwant = DOWN;
}

static unsigned char fire(void)
{
    return !(JOY2 & 0x10);
}

/* ------------------------------------------------------------------ */
/* Sprites                                                              */
/* ------------------------------------------------------------------ */

/* one sprite at the current VRAM address: sp_code centred on (sp_x, sp_y),
 * screen pixels (globals: cc65 passes arguments slowly) */
static unsigned char sp_code, sp_flip, sp_pal;
static unsigned int sp_x, sp_y;

static void sprite(void)
{
    sp_x -= 8;
    sp_y -= 8;
    VERA_DATA0 = sp_code << 2;              /* VRAM_SPRITES + code * 128, >> 5 */
    VERA_DATA0 = (VRAM_SPRITES >> 13) + (sp_code >> 6);
    VERA_DATA0 = (unsigned char)sp_x;
    VERA_DATA0 = (sp_x >> 8) & 3;
    VERA_DATA0 = (unsigned char)sp_y;
    VERA_DATA0 = (sp_y >> 8) & 3;
    VERA_DATA0 = 0x0C | sp_flip;            /* Z 3: in front of the layers */
    VERA_DATA0 = 0x50 | sp_pal;             /* 16x16 */
}

static void no_sprite(void)
{
    VERA_DATA0 = 0; VERA_DATA0 = 0; VERA_DATA0 = 0; VERA_DATA0 = 0;
    VERA_DATA0 = 0; VERA_DATA0 = 0; VERA_DATA0 = 0; VERA_DATA0 = 0;
}

/* the moving sprites, every frame: the ghosts and Pac-Man */
static void draw_sprites(void)
{
    static const unsigned char pac_h[4] = { S_PAC_C, 46, 44, 46 };
    static const unsigned char pac_v[4] = { S_PAC_C, 47, 45, 47 };
    static unsigned char i, k, hide;

    vram_at(0xFC00u, 1);
    hide = phase == PH_TITLE || phase == PH_GAMEOVER;
    for (i = 0; i < 4; ++i) {
        if (hide || ((phase == PH_DYING || phase == PH_LEVEL) && phase_t >= 60)) {
            no_sprite();
            continue;
        }
        sp_x = gx[i];
        sp_y = gy[i] + 24;
        sp_flip = 0;
        if (freeze && i == score_ghost) {
            sp_code = S_SCORE + ghosts_eaten - 1;
            sp_pal = PAL_DOOR;
        } else if (gstate[i] == G_EYES || gstate[i] == G_ENTERING) {
            sp_code = S_GHOST + gdir[i] * 2;
            sp_pal = PAL_EYES;
        } else if (gfright[i]) {
            sp_code = S_FRIGHT + (anim >> 3 & 1);
            sp_pal = fright_t < 120 && (fright_t & 8) ? PAL_FLASH : PAL_FRIGHT;
        } else {
            sp_code = S_GHOST + gdir[i] * 2 + (anim >> 3 & 1);
            sp_pal = PAL_BLINKY + i;
        }
        sprite();
    }

    if (hide || pac_hidden) {
        no_sprite();
        return;
    }
    sp_x = px;
    sp_y = py + 24;
    sp_pal = PAL_PACMAN;
    sp_flip = 0;
    if (phase == PH_DYING && phase_t >= 60) {
        k = (phase_t - 60) >> 3;
        if (k >= 12) {
            no_sprite();
            return;
        }
        sp_code = S_DEATH + k;
    } else {
        k = phase == PH_READY ? 0 : (panim >> 1) & 3;
        if (pdir == LEFT || pdir == RIGHT) {
            sp_code = pac_h[k];
            sp_flip = pdir == LEFT;
        } else {
            sp_code = pac_v[k];
            sp_flip = pdir == UP ? 2 : 0;
        }
    }
    sprite();
}

/* the fruit, the lives and the level: only when they change */
static void draw_hud(void)
{
    static unsigned char key = 0xFF, k, fruit;

    fruit = fruit_left && phase == PH_PLAY;
    k = lives << 2 | fruit << 1 | (phase == PH_TITLE);
    if (k == key) return;
    key = k;

    vram_at(0xFC00u + SP_FRUIT * 8, 1);
    sp_code = S_CHERRY;
    sp_pal = PAL_CHERRY;
    sp_flip = 0;
    if (fruit) {
        sp_x = 112; sp_y = FRUIT_Y + 24;
        sprite();
    } else {
        no_sprite();
    }
    sp_code = 44;                           /* Pac-Man, facing left */
    sp_pal = PAL_PACMAN;
    sp_flip = 1;
    for (k = 0; k < 3; ++k) {
        if (phase != PH_TITLE && k + 1 < lives) {
            sp_x = 24 + k * 16; sp_y = 280;
            sprite();
        } else {
            no_sprite();
        }
    }
    if (phase != PH_TITLE) {
        sp_code = S_CHERRY; sp_pal = PAL_CHERRY; sp_flip = 0;
        sp_x = 200; sp_y = 280;
        sprite();
    } else {
        no_sprite();
    }
}

/* ------------------------------------------------------------------ */
/* Game                                                                 */
/* ------------------------------------------------------------------ */

static void positions_reset(void)
{
    unsigned char i;

    px = PAC_X0; py = PAC_Y0; pdir = LEFT; pwant = LEFT; pacc = 0; panim = 0;
    pmoving = 0; pac_hidden = 0;
    for (i = 0; i < 4; ++i) {
        gx[i] = ghost_x0[i];
        gy[i] = i ? HOUSE_Y : DOOR_Y;
        gdir[i] = i ? (i & 1 ? UP : DOWN) : LEFT;
        gstate[i] = i ? G_HOUSE : G_ACTIVE;
        gacc[i] = 0;
        gfright[i] = 0;
    }
    fright_t = 0; mode_i = 0; mode_t = 0; freeze = 0;
    life_dots = 0; idle = 0; fruit_left = 0;
}

static void set_phase(unsigned char p)
{
    phase = p;
    phase_t = 0;
}

static void ready(unsigned char first)
{
    positions_reset();
    if (first) {
        put_text(9, 14, "player one", PAL_INKY);
        snd_play(0, SND_INTRO0);
        snd_play(1, SND_INTRO1);
    }
    put_text(11, 20, "ready!", PAL_PACMAN);
    phase_t2 = first;
    set_phase(PH_READY);
}

static void new_game(void)
{
    memset(score_d, 0, sizeof score_d);
    lives = 3; level = 1; extra_given = 0;
    maze_reset();
    draw_status();
    ready(1);
}

static void title(void)
{
    maze_reset();
    draw_status();
    put_text(9, 20, "push fire", PAL_WHITE);
    snd_stop_all();
    set_phase(PH_TITLE);
}

static void collisions(void)
{
    unsigned char i, tx = TX(px), ty = TY(py);

    for (i = 0; i < 4; ++i) {
        if (gstate[i] != G_ACTIVE || TX(gx[i]) != tx || TY(gy[i]) != ty) continue;
        if (gfright[i]) {
            gfright[i] = 0;
            gstate[i] = G_EYES;
            ++ghosts_eaten;
            add_score(10 << ghosts_eaten);  /* 200 400 800 1600 */
            score_ghost = i;
            freeze = 60;
            pac_hidden = 1;
            snd_effect(SND_GHOST);
        } else {
            snd_stop_all();
            set_phase(PH_DYING);
            return;
        }
    }
    if (fruit_left && ty == 20 && (tx == 13 || tx == 14)) {
        fruit_left = 0;
        add_score(10);
        snd_effect(SND_FRUIT);
    }
}

static void play_frame(void)
{
    static unsigned char i, any_eyes;

    if (freeze) {
        if (!--freeze) pac_hidden = 0;
        return;
    }
    read_joystick();

    /* scatter / chase, paused while the ghosts are frightened */
    if (fright_t) {
        if (!--fright_t)
            for (i = 0; i < 4; ++i) gfright[i] = 0;
    } else if (mode_frames[mode_i] && ++mode_t >= mode_frames[mode_i]) {
        mode_t = 0;
        ++mode_i;
        ghosts_reverse();
    }

    pacc += fright_t ? SP_PAC_F : (level > 1 ? SP_PAC_F : SP_PAC_N);
    while (pacc >= 64) {
        pacc -= 64;
        pac_step();
    }
    collisions();
    if (phase != PH_PLAY || freeze) return;

    for (i = 0; i < 4; ++i) {
        gacc[i] += ghost_speed(i);
        while (gacc[i] >= 64) {
            gacc[i] -= 64;
            ghost_step(i);
        }
    }
    collisions();
    if (phase != PH_PLAY) return;

    if (idle < 255) ++idle;
    ghosts_release();
    if (fruit_left) --fruit_left;

    /* the power pellets blink */
    if ((anim & 15) == 0) {
        static const unsigned char pp_x[4] = { 1, 26, 1, 26 };
        static const unsigned char pp_y[4] = { 6, 6, 26, 26 };
        for (i = 0; i < 4; ++i)
            if (mrow[pp_y[i]][pp_x[i]] == T_POWER)
                put_tile(pp_x[i], pp_y[i], anim & 16 ? T_BLANK : T_POWER, PAL_MAZE);
    }

    any_eyes = 0;
    for (i = 0; i < 4; ++i)
        if (gstate[i] == G_EYES || gstate[i] == G_ENTERING) any_eyes = 1;
    if (any_eyes) snd_background(SND_EYES);
    else if (fright_t) snd_background(SND_FRIGHT);
    else if (dots < 116) snd_background(SND_SIREN1);
    else if (dots < 180) snd_background(SND_SIREN2);
    else if (dots < 212) snd_background(SND_SIREN3);
    else if (dots < 228) snd_background(SND_SIREN4);
    else snd_background(SND_SIREN5);

    if (dots == total_dots) {
        snd_stop_all();
        set_phase(PH_LEVEL);
    }
}

static void frame(void)
{
    ++anim;
    ++phase_t;
    switch (phase) {
    case PH_TITLE:
        if (fire()) new_game();
        break;
    case PH_READY:
        if (phase_t == 120) clear_text(9, 14, 10);
        if (phase_t >= (phase_t2 ? 250 : 120)) {
            clear_text(11, 20, 6);
            set_phase(PH_PLAY);
        }
        break;
    case PH_PLAY:
        play_frame();
        break;
    case PH_DYING:
        if (phase_t == 60) snd_play(2, SND_DEATH);
        if (phase_t >= 60 + 12 * 8 + 40) {
            if (--lives == 0) {
                put_text(9, 20, "game  over", PAL_BLINKY);
                set_phase(PH_GAMEOVER);
            } else {
                ready(0);
            }
        }
        break;
    case PH_LEVEL:                          /* the maze flashes, then the next one */
        if (phase_t >= 60) wall_colour((phase_t - 60) / 12 & 1);
        if (phase_t >= 60 + 8 * 12) {
            wall_colour(0);
            ++level;
            maze_reset();
            ready(0);
        }
        break;
    case PH_GAMEOVER:
        if (phase_t >= 180) title();
        break;
    }
}

int main(void)
{
    unsigned char i;

    CARDSEL = 1;                            /* select the VERA (the latch of the card) */
    clrscr();
    vera_require();
    cputs("pac-man on the vera\r\n\r\n"
          "graphics and sounds of the arcade\r\n"
          "(c) namco 1980\r\n\r\n"
          "joystick in port 2, fire to start\r\n");

    for (i = 0; i < 64; ++i) sq[i] = (unsigned int)i * i;
    for (i = 0; i < 36; ++i) mrow[i] = maze + i * 28;
    srand(VERA_SCANLINE_L);

    __asm__("sei");                         /* the joystick is read directly */
    JOY2 = 0xFF;                            /* no keyboard column selected: port A */
                                            /* reads the joystick lines only */
    video_init();
    sid_init();
    title();
    for (;;) {
        wait_frame();
        draw_sprites();
        draw_hud();
        snd_frame();
        frame();
    }
}
