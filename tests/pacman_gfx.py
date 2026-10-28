#!/usr/bin/env python3
"""pacman_gfx.py - the graphics of the Pac-Man arcade (MAME set "pacman") in
the formats of the VERA, for test_pacman.c.

    python3 pacman_gfx.py pacman.zip gen/pacgfx.s gen/pacgfx.h [preview.png]

From the ROM zip:
  pacman.5e     256 tiles 8x8, 2 bpp       -> VERA tiles 2 bpp (4 KB)
  pacman.5f     64 sprites 16x16, 2 bpp    -> VERA sprites 4 bpp (8 KB)
  82s123.7f     32 colours                 -> VERA palette, 16 groups of 4
  82s126.4a     colour codes: 4 colours each
  pacman.6e-6j  the program: the maze table at $3435 (drawn by $2419: tile
                codes for the left half, mirrored with code ^ 1) and the dot
                table at $35B5 (offsets, $2448), power pellets at
                $4064/$4078/$4384/$4398; the fruit of every level at $0EFD
                (21 levels, then the key: sprite code, colour code, points
                index 6-13) and the tiles of the fruit points at $38BC (the
                strings at video RAM $0212/$0232, colour code $03)

The monitor of the arcade is rotated (ROT90): tiles and sprites are turned
here, and the video RAM ($4000, 28x36 tiles, the rows 2-33 column by column
from the right) becomes a 28x36 map, row by row.  The colour code of a tile
(colour RAM) becomes the palette offset of the VERA map entry, the one of a
sprite its palette offset: every code used gets a group of 16 VERA colours,
of which 0-3 are the 4 colours of the code.  The group SCENE is loaded with
the one colour code of a scene that has no group of its own (pacman_scene.py):
SCENE_CODES, in the order of pac_scene_palette.
"""

import sys
import zipfile

# colour codes used by the game -> VERA palette group (the palette offset)
GROUPS = [
    ('MAZE',    0x10),  # walls (3, blue), dots (1)
    ('WHITE',   0x0F),  # text
    ('PACMAN',  0x09),  # Pac-Man, yellow text
    ('BLINKY',  0x01),  # red ghost, red text
    ('PINKY',   0x03),
    ('INKY',    0x05),  # cyan ghost, cyan text
    ('CLYDE',   0x07),
    ('FRIGHT',  0x11),  # frightened ghost
    ('FLASH',   0x12),  # frightened ghost, flashing
    ('EYES',    0x19),  # eyes of an eaten ghost
    ('DOOR',    0x18),  # ghost house door, ghost scores
    ('CHERRY',  0x14),  # cherry, apple
    ('PEACH',   0x15),
    ('BELL',    0x16),  # bell, key, the big Pac-Man of intermission 1
    ('MELON',   0x17),
    ('SCENE',   0x1F),  # one of SCENE_CODES, loaded by the scene
]
SCENE_CODES = [0x1F, 0x1D, 0x1E]   # attract (texts), intermission 2, intermission 3

FRUIT_TABLE = 0x0EFD
FRUIT_LEVELS = 21
FRUIT_POINTS = [1, 3, 5, 7, 10, 20, 30, 50]   # in hundreds, points index 6-13


def group_of(code):
    """The VERA palette group of an arcade colour code; None if it has none
    (colour code 0, invisible, or a code of SCENE_CODES)."""
    for i, (_, c) in enumerate(GROUPS):
        if c == code and c not in SCENE_CODES:
            return i
    return None


def fruit_point_tiles(prog):
    """The 4 tiles of every fruit points, 100 to 5000: 100 is written at
    $0212 as 2 tiles, the others at $0232 as 4."""
    t = [[0x40, prog[0x38BE], prog[0x38BF], 0x40]]
    for i in range(7):
        a = 0x38C6 + 10 * i
        t.append(list(prog[a:a + 4]))
    return t


def rgb(v):
    """82s123.7f: bits 0-2 red, 3-5 green, 6-7 blue (resistor weights)."""
    r = 0x21 * (v & 1) + 0x47 * (v >> 1 & 1) + 0x97 * (v >> 2 & 1)
    g = 0x21 * (v >> 3 & 1) + 0x47 * (v >> 4 & 1) + 0x97 * (v >> 5 & 1)
    b = 0x51 * (v >> 6 & 1) + 0xAE * (v >> 7 & 1)
    return r, g, b


def decode(data, base, size, xoffs, yoffs):
    """A MAME gfx_layout with planes {0, 4}: bit offsets from the MSB."""
    def bit(o):
        o += base * 8
        return data[o >> 3] >> (7 - (o & 7)) & 1
    return [[bit(xoffs[x] + yoffs[y]) << 1 | bit(xoffs[x] + yoffs[y] + 4)
             for x in range(size)] for y in range(size)]


def rot90(g):
    n = len(g)
    return [[g[n - 1 - x][y] for x in range(n)] for y in range(n)]


TILE_X = [64, 65, 66, 67, 0, 1, 2, 3]
TILE_Y = [8 * i for i in range(8)]
SPR_X = [64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195, 0, 1, 2, 3]
SPR_Y = [8 * i for i in range(8)] + [256 + 8 * i for i in range(8)]


def video_offset(x, y):
    """Video RAM offset of the screen tile (x, y), rows 2-33 (the maze)."""
    return (y - 2) + ((29 - x) << 5)


def build_maze(prog):
    vram = [0x40] * 0x400                     # $40: blank tile
    hl, bc = 0, 0x3435
    while prog[bc]:
        a = prog[bc]
        if a < 0x80:                          # a skip, then the tile
            hl += a - 1
            bc += 1
            a = prog[bc]
        hl += 1
        vram[hl] = a
        vram[0x3E0 + 2 * (hl & 0x1F) - hl] = a ^ 1
        bc += 1
    hl = 0
    for i in range(240):
        hl += prog[0x35B5 + i]
        vram[hl] = 0x10
    for a in (0x64, 0x78, 0x384, 0x398):
        vram[a] = 0x14
    maze = [[0x40] * 28 for _ in range(36)]
    for y in range(2, 34):
        for x in range(28):
            maze[y][x] = vram[video_offset(x, y)]
    return maze


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit('usage: pacman_gfx.py pacman.zip OUT.s OUT.h [preview.png]')
    z = zipfile.ZipFile(sys.argv[1])
    prog = b''.join(z.read(n) for n in ('pacman.6e', 'pacman.6f', 'pacman.6h', 'pacman.6j'))
    tiles = [rot90(decode(z.read('pacman.5e'), 16 * i, 8, TILE_X, TILE_Y)) for i in range(256)]
    sprites = [rot90(decode(z.read('pacman.5f'), 64 * i, 16, SPR_X, SPR_Y)) for i in range(64)]
    pal = [rgb(v) for v in z.read('82s123.7f')]
    lut = z.read('82s126.4a')
    maze = build_maze(prog)

    tile_bytes = []
    for t in tiles:                           # 2 bpp: pixel 0 in bits 7-6
        for row in t:
            for h in (0, 4):
                tile_bytes.append(row[h] << 6 | row[h + 1] << 4 | row[h + 2] << 2 | row[h + 3])
    spr_bytes = []
    for s in sprites:                         # 4 bpp: pixel 0 in the high nibble
        for row in s:
            for x in range(0, 16, 2):
                spr_bytes.append(row[x] << 4 | row[x + 1])
    pal_bytes = []
    for _, code in GROUPS:                    # GGGGBBBB, 0000RRRR
        for i in range(4):
            r, g, b = pal[lut[code * 4 + i] & 15]
            pal_bytes += [(g >> 4) << 4 | b >> 4, r >> 4]
    maze_bytes = [c for row in maze for c in row]
    scene_pal = []
    for code in SCENE_CODES:
        for i in range(4):
            r, g, b = pal[lut[code * 4 + i] & 15]
            scene_pal += [(g >> 4) << 4 | b >> 4, r >> 4]
    fruit_spr, fruit_pal, fruit_pts = [], [], []
    for lv in range(FRUIT_LEVELS):
        code, colour, pts = prog[FRUIT_TABLE + 3 * lv:FRUIT_TABLE + 3 * lv + 3]
        fruit_spr.append(code)
        fruit_pal.append(group_of(colour))
        fruit_pts.append(pts - 6)
    point_tiles = [c for t in fruit_point_tiles(prog) for c in t]

    with open(sys.argv[2], 'w') as s:
        s.write('; pacgfx.s - generated by pacman_gfx.py from the Pac-Man arcade ROMs: do not edit.\n\n'
                '.export _pac_tiles, _pac_sprites, _pac_palette, _pac_maze, _pac_scene_palette\n'
                '.export _pac_fruit_spr, _pac_fruit_pal, _pac_fruit_pts, _pac_fruit_points, _pac_point_tiles\n'
                '.rodata\n')
        for name, data, n in (('_pac_tiles', tile_bytes, 16), ('_pac_sprites', spr_bytes, 16),
                              ('_pac_palette', pal_bytes, 16), ('_pac_maze', maze_bytes, 28),
                              ('_pac_scene_palette', scene_pal, 8),
                              ('_pac_fruit_spr', fruit_spr, FRUIT_LEVELS),
                              ('_pac_fruit_pal', fruit_pal, FRUIT_LEVELS),
                              ('_pac_fruit_pts', fruit_pts, FRUIT_LEVELS),
                              ('_pac_fruit_points', FRUIT_POINTS, 8),
                              ('_pac_point_tiles', point_tiles, 4)):
            s.write('\n%s:\n' % name)
            for i in range(0, len(data), n):
                s.write('        .byte ' + ','.join('$%02x' % b for b in data[i:i + n]) + '\n')

    with open(sys.argv[3], 'w') as h:
        h.write('/* pacgfx.h - generated by pacman_gfx.py: do not edit. */\n'
                '#ifndef PACGFX_H\n#define PACGFX_H\n\n'
                '/* VERA palette groups (palette offsets): the colour codes of the arcade */\n')
        for i, (name, code) in enumerate(GROUPS):
            h.write('#define PAL_%-8s %2d   /* colour code $%02X */\n' % (name, i, code))
        h.write('#define PAL_GROUPS   %2d\n' % len(GROUPS))
        h.write('#define FRUIT_LEVELS %2d   /* then always the last fruit */\n\n' % FRUIT_LEVELS)
        h.write('#define PAC_TILES_SIZE   %5u\n#define PAC_SPRITES_SIZE %5u\n'
                '#define PAC_MAZE_W       %5u\n#define PAC_MAZE_H       %5u\n\n'
                % (len(tile_bytes), len(spr_bytes), 28, 36))
        h.write('extern const unsigned char pac_tiles[], pac_sprites[];\n'
                'extern const unsigned char pac_palette[];      /* PAL_GROUPS x 4 colours, 2 bytes */\n'
                'extern const unsigned char pac_maze[];         /* 36 rows x 28 tile codes */\n'
                'extern const unsigned char pac_scene_palette[]; /* SCENE: 4 colours of each scene colour */\n'
                '/* the fruit of a level (from 1: [level - 1]): sprite code, palette group,\n'
                ' * points index into pac_fruit_points (hundreds) and pac_point_tiles (4 tiles) */\n'
                'extern const unsigned char pac_fruit_spr[], pac_fruit_pal[], pac_fruit_pts[];\n'
                'extern const unsigned char pac_fruit_points[], pac_point_tiles[];\n\n#endif\n')

    if len(sys.argv) == 5:
        from PIL import Image
        img = Image.new('RGB', (224, 288))
        colours = [pal[lut[0x10 * 4 + i] & 15] for i in range(4)]
        for y in range(36):
            for x in range(28):
                t = tiles[maze[y][x]]
                for ty in range(8):
                    for tx in range(8):
                        img.putpixel((x * 8 + tx, y * 8 + ty), colours[t[ty][tx]])
        img.resize((448, 576), Image.NEAREST).save(sys.argv[4])


if __name__ == '__main__':
    main()
