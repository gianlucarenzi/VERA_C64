#!/usr/bin/env python3
"""pacman_scene.py - the scenes of the Pac-Man arcade that are not the game,
recorded in MAME by pacman_scenes.lua, as streams for the scene player of
test_pacman.c.

    python3 pacman_scene.py pacman_scenes.log gen/pacscn.s gen/pacscn.h

The scenes: the attract mode (the characters and their nicknames, the chase
of the power pellet) and the three intermissions.  Only the maze area (rows
2-33) and the 8 sprites are kept: the scores and the bottom rows are the
game's own.

A scene is a stream of commands, frame after frame.  The player keeps for
every sprite slot its code, palette group, position and velocity; at every
frame each slot first moves by its velocity, then the commands up to a WAIT
are run:

    $00                 end of the scene
    $01-$3F             WAIT n: show this frame, then n - 1 more frames of
                        velocities only
    $40+s a g p0 p1     slot s: code<<2 | flip, palette group ($FF: hidden),
                        position; velocity 0
    $48+s p0 p1         slot s: position
    $50+s d             slot s: velocity, d0 in the high nibble, d1 in the
                        low one (signed); the position moves by the change
    $58+s a             slot s: code<<2 | flip
    $60+s g             slot s: palette group
    $68 x y t g         tile t, palette group g, at screen column x, row y
    $69 x y n g t...    n tiles in a row from (x, y), palette group g
    $6A                 the intermission song (SND_INTER0/1) on voices 0, 1
    $6B                 the song stops
    $6C i               the SCENE palette group: pac_scene_palette entry i
    $6D n               the song stops n frames from now, after the scene

Positions are the bytes of the arcade ($5060-$506F): the centre of the
sprite is at x = 247 - p0, y = 280 - p1 on the 224x288 screen, one pixel more
to the left for the slots 0-2 (as MAME draws them).  The flips are turned as
the picture: the flip x of the arcade is the vertical one.
"""

import sys
from pacman_gfx import GROUPS, SCENE_CODES, group_of

T_BLANK = 0x40
HIDDEN = 0xFF
PAL_SCENE = [n for n, _ in GROUPS].index('SCENE')
SONG_PERIOD = 320           # frames of the intermission song (pacman_snd.py checks it)

OP_WAIT, OP_FULL, OP_POS, OP_VEL, OP_ATTR, OP_PAL = 0x00, 0x40, 0x48, 0x50, 0x58, 0x60
OP_TILE, OP_TEXT, OP_MUSIC, OP_MUSIC_OFF, OP_SCENE_PAL, OP_MUSIC_LATER = 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D


def screen_xy(o):
    """Screen column and row of the video RAM offset o."""
    if o >= 0x3C0:
        return 29 - (o & 31), (o >> 5) - 30
    if o < 0x40:
        return 29 - (o & 31), 34 + (o >> 5)
    return 29 - (o >> 5), (o & 31) + 2


class Log:
    def __init__(self, path):
        self.marks = {}
        self.tiles = {}             # frame -> 0x800 bytes of video RAM
        self.sprites = {}           # frame -> (codes, positions)
        self.song = {}              # frame -> song request of voice 0
        mem = [0] * 0x800
        for line in open(path):
            p = line.split()
            if p[0] == 'M':
                self.marks.setdefault(p[2], []).append(int(p[1]))
            elif p[0] == 'V':
                for kv in p[2:]:
                    a, v = kv.split('=')
                    mem[int(a, 16)] = int(v, 16)
            elif p[0] == 'S':
                f = int(p[1])
                self.tiles[f] = list(mem)
                self.sprites[f] = (bytes.fromhex(p[2]), bytes.fromhex(p[3]))
                self.song[f] = int(p[12], 16)
        for name in ('attract', 'attract_end', 'act1', 'act2', 'act3'):
            if name not in self.marks:
                sys.exit('pacman_scene.py: no marker "%s" in %s' % (name, path))
        if len(self.marks.get('act_end', [])) != 3:
            sys.exit('pacman_scene.py: the intermissions did not all end in %s' % path)

    def scenes(self):
        m = self.marks
        return [('ATTRACT', m['attract'][0], m['attract_end'][0])] + \
               [('ACT%d' % i, m['act%d' % i][0], m['act_end'][i - 1]) for i in (1, 2, 3)]


def frame_state(log, f, scene_code):
    """The tiles of rows 2-33 {(x, y): (tile, group)} and the 8 sprites
    [(attr, group, p0, p1)] of frame f."""
    def group(code):
        if code in SCENE_CODES:
            if code != scene_code:
                sys.exit('pacman_scene.py: frame %d: two scene colours, $%02X and $%02X'
                         % (f, code, scene_code))
            return PAL_SCENE
        g = group_of(code)
        if g is None:
            sys.exit('pacman_scene.py: frame %d: colour code $%02X has no palette group' % (f, code))
        return g

    mem = log.tiles[f]
    tiles = {}
    for o in range(0x400):
        x, y = screen_xy(o)
        if 0 <= x < 28 and 2 <= y < 34:
            t, c = mem[o], mem[0x400 + o] & 0x1F
            tiles[x, y] = (T_BLANK, 0) if t == T_BLANK or c == 0 else (t, group(c))
    codes, pos = log.sprites[f]
    sprites = []
    for s in range(8):
        a, c = codes[2 * s], codes[2 * s + 1] & 0x1F
        if c == 0 or a >> 2 == 63:
            sprites.append((0, HIDDEN, 0, 0))
        else:
            flip = (a >> 1 & 1) | (a & 1) << 1     # VERA: bit 0 h, bit 1 v
            sprites.append(((a & 0xFC) | flip, group(c), pos[2 * s], pos[2 * s + 1]))
    return tiles, sprites


def scene_colour(log, a, b):
    used = set()
    for f in range(a, b):
        mem = log.tiles[f]
        for o in range(0x400):
            x, y = screen_xy(o)
            if 0 <= x < 28 and 2 <= y < 34 and mem[o] != T_BLANK:
                used.add(mem[0x400 + o] & 0x1F)
        codes, _ = log.sprites[f]
        for s in range(8):
            used.add(codes[2 * s + 1] & 0x1F)
    used &= set(SCENE_CODES)
    if len(used) > 1:
        sys.exit('pacman_scene.py: frames %d-%d: more scene colours %s' % (a, b, used))
    return used.pop() if used else None


def signed(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


def encode(log, a, b):
    scode = scene_colour(log, a, b)
    out = []
    if scode is not None:
        out += [OP_SCENE_PAL, SCENE_CODES.index(scode)]

    tiles = {}                              # what the player shows
    slot = [[0, HIDDEN, 0, 0, 0, 0] for _ in range(8)]   # attr, group, p0, p1, v0, v1
    wait = 0
    song_at = None
    for f in range(a, b):
        ops = []
        want_t, want_s = frame_state(log, f, scode)

        # music: the song starts, restarts every period, stops
        if log.song[f] and not log.song.get(f - 1) or \
                log.song[f] and song_at is not None and f - song_at == SONG_PERIOD:
            ops.append(OP_MUSIC)
            song_at = f
        elif not log.song[f] and log.song.get(f - 1) and f > a:
            ops.append(OP_MUSIC_OFF)
            song_at = None

        # tiles, in runs along the rows
        changed = sorted(((y, x), t) for (x, y), t in want_t.items()
                         if tiles.get((x, y), (T_BLANK, 0)) != t)
        i = 0
        while i < len(changed):
            (y, x), (t, g) = changed[i]
            run = [t]
            while i + len(run) < len(changed):
                (y2, x2), (t2, g2) = changed[i + len(run)]
                if y2 != y or x2 != x + len(run) or (g2 != g and t2 != T_BLANK):
                    break
                run.append(t2)
            if len(run) == 1:
                ops += [OP_TILE, x, y, t, g]
            else:
                ops += [OP_TEXT, x, y, len(run), g] + run
            for k in range(len(run)):
                tiles[x + k, y] = changed[i + k][1]
            i += len(run)

        # sprites: the velocities first, as the player does
        for s in range(8):
            st = slot[s]
            st[2] = (st[2] + st[4]) & 0xFF
            st[3] = (st[3] + st[5]) & 0xFF
            attr, g, p0, p1 = want_s[s]
            if g == HIDDEN:
                if st[1] != HIDDEN:
                    ops += [OP_PAL + s, HIDDEN]
                    st[1] = HIDDEN
                continue
            if st[1] == HIDDEN:
                ops += [OP_FULL + s, attr, g, p0, p1]
                slot[s] = [attr, g, p0, p1, 0, 0]
                continue
            if (st[2], st[3]) != (p0, p1):
                d0, d1 = signed(p0 - st[2] + st[4]), signed(p1 - st[3] + st[5])
                if -8 <= d0 < 8 and -8 <= d1 < 8:
                    ops += [OP_VEL + s, (d0 & 15) << 4 | (d1 & 15)]
                    st[4], st[5] = d0, d1
                else:
                    ops += [OP_POS + s, p0, p1]
                st[2], st[3] = p0, p1
            if st[0] != attr:
                ops += [OP_ATTR + s, attr]
                st[0] = attr
            if st[1] != g:
                ops += [OP_PAL + s, g]
                st[1] = g

        if ops or f == a:
            if wait:
                out.append(wait)
            out += ops
            wait = 1
        else:
            wait += 1
            if wait == 0x3F:
                out.append(wait)
                wait = 0
                # the next frame is a new WAIT: velocities still apply
    if wait:
        out.append(wait)
    # the song goes on after the scene until the game stops it
    if song_at is not None:
        f = b
        while log.song.get(f):
            f += 1
        if f not in log.song:
            sys.exit('pacman_scene.py: the log ends before the song after frame %d' % b)
        out += [OP_MUSIC_LATER, f - b]
    out.append(0)
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: pacman_scene.py SCENES.log OUT.s OUT.h')
    log = Log(sys.argv[1])
    scenes = [(name, b - a, encode(log, a, b)) for name, a, b in log.scenes()]

    with open(sys.argv[2], 'w') as s:
        s.write('; pacscn.s - generated by pacman_scene.py from the Pac-Man arcade\n'
                '; scenes recorded in MAME (pacman_scenes.lua): do not edit.\n\n'
                '.export _scn_ptr_lo, _scn_ptr_hi\n.rodata\n\n')
        for name, _, data in scenes:
            s.write('scn_%s:\n' % name.lower())
            for i in range(0, len(data), 24):
                s.write('        .byte ' + ','.join('$%02x' % b for b in data[i:i + 24]) + '\n')
        s.write('\n_scn_ptr_lo:  .byte ' + ','.join('<scn_%s' % c[0].lower() for c in scenes))
        s.write('\n_scn_ptr_hi:  .byte ' + ','.join('>scn_%s' % c[0].lower() for c in scenes) + '\n')

    with open(sys.argv[3], 'w') as h:
        h.write('/* pacscn.h - generated by pacman_scene.py: do not edit. */\n'
                '#ifndef PACSCN_H\n#define PACSCN_H\n\n')
        for i, (name, frames, data) in enumerate(scenes):
            h.write('#define SCN_%-8s %d   /* %4d frames, %5d bytes */\n' % (name, i, frames, len(data)))
        h.write('#define SCN_COUNT    %d\n\n' % len(scenes))
        h.write('#define SCN_END      0x00\n#define SCN_WAIT     0x3F   /* 1-63 */\n'
                '#define SCN_FULL     0x40\n#define SCN_POS      0x48\n#define SCN_VEL      0x50\n'
                '#define SCN_ATTR     0x58\n#define SCN_PAL      0x60\n#define SCN_TILE     0x68\n'
                '#define SCN_TEXT     0x69\n#define SCN_MUSIC    0x6A\n#define SCN_MUSIC_OFF 0x6B\n'
                '#define SCN_SCENE_PAL 0x6C\n#define SCN_MUSIC_LATER 0x6D\n#define SCN_HIDDEN   0xFF\n\n')
        h.write('extern const unsigned char scn_ptr_lo[], scn_ptr_hi[];\n\n#endif\n')

    for name, frames, data in scenes:
        print('%-8s %4d frames %5d bytes' % (name, frames, len(data)))


if __name__ == '__main__':
    main()
