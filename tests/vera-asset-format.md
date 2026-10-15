# VERA Asset Format Spec

Design goal: every exported file is a near-literal dump of the VERA VRAM
region it targets, so the runtime loader is a copy loop, not a parser. All
formats below follow the same conventions and mirror the register/VRAM
layouts in `../Documentation/X16 Reference - 09 - VERA Programmer's Reference.md`.

## Common conventions

- All multi-byte values are little-endian.
- Every file starts with a 4-byte ASCII magic + 1-digit version (e.g. `VSP1`).
- Palette entries are always VERA's native 12-bit RGB444, 2 bytes each:
  `byte0 = GGGGBBBB`, `byte1 = 0000RRRR` (see "Palette" in the VERA reference).
- Tile/tilemap bit layouts are copied verbatim from "Tile mode 2/4/8 bpp" and
  "Sprite attributes" so they can be written straight to `MAP_BASE` /
  `TILE_BASE` / sprite attribute RAM with no repacking.

## VSP1 — Sprite format (implemented)

Already implemented in `libresprite/src/app/file/vera_format.cpp` (commit
`23b359805`, "Add VERA sprite export format for Commander X16"):

```
offset 0:   4 bytes  magic "VSP1"
offset 4:   1 byte   sprite width  in pixels (8/16/32/64)
offset 5:   1 byte   sprite height in pixels (8/16/32/64)
offset 6:   1 byte   bits per pixel (always 8 today)
offset 7:   1 byte   reserved (0)
offset 8:   512 bytes  palette, 256 entries x 2 bytes, RGB444
offset 520: width*height bytes  pixel data, 1 byte/pixel, row-major
```

Width/height are restricted to 8/16/32/64 because that's the full range of
the hardware's sprite width/height field (VERA reference, "Sprite
attributes", table). The format always ships 8bpp/256-color pixel data and
the full 256-entry palette, even though the hardware also supports a 4bpp
sprite mode — see **Open items** below.

**Known gap:** the file is not registered in
`libresprite/src/app/file/file_formats_manager.cpp` nor listed in
`libresprite/CMakeLists.txt`'s format sources, so it currently cannot be
selected from LibreSprite's Save As/Export dialog. See
[`02-tool-setup.md`](02-tool-setup.md).

## VTS1 — Tileset format (proposed)

For layer tile data (2/4/8bpp), matching "Tile mode 2/4/8 bpp":

```
offset 0:  4 bytes  magic "VTS1"
offset 4:  1 byte   tile width  in pixels (8 or 16)
offset 5:  1 byte   tile height in pixels (8 or 16)
offset 6:  1 byte   bits per pixel (2, 4 or 8)
offset 7:  1 byte   tile count (low byte); high byte follows at offset 8
offset 8:  1 byte   tile count (high byte) — supports up to 1024 tiles
offset 9:  1 byte   reserved (0)
offset 10: 512 bytes  palette, 256 entries x 2 bytes, RGB444
offset 522: tile data, packed per VERA's own bit packing:
            - 8bpp: 1 byte/pixel
            - 4bpp: 2 pixels/byte, bit7..4 = left pixel, bit3..0 = right pixel
            - 2bpp: 4 pixels/byte, bit7..6 = leftmost pixel
            tiles stored back to back, row-major within each tile
```

Color index 0 is transparent in every mode, matching hardware behavior — no
separate alpha/mask channel needed.

## VTM1 — Tilemap format (proposed)

One "screen" of a level. Header carries geometry; the map entries are byte-
identical to what "Tile mode 2/4/8 bpp" expects at `MAP_BASE`:

```
offset 0: 4 bytes  magic "VTM1"
offset 4: 1 byte   map width  in tiles (<= 256, hardware MAP_WIDTH limit)
offset 5: 1 byte   map height in tiles (<= 256, hardware MAP_HEIGHT limit)
offset 6: 1 byte   tile width/height flag (0 = 8px tiles, 1 = 16px tiles)
offset 7: 1 byte   reserved (0)
offset 8: width*height*2 bytes  map entries, 2 bytes each:
            byte0: tile index (7:0)
            byte1: bit7=palette offset(3:0)<<4 | bit1=V-flip | bit0=H-flip
                   | bits1:0=tile index(9:8)
            (exact bit layout of the hardware tile map entry)
```

A VTM1 file references tile indices into a companion VTS1; it carries no
pixel or palette data of its own.

**Hardware ceiling:** `MAP_WIDTH`/`MAP_HEIGHT` register values only encode
32/64/128/256 tiles. A level wider than 256 tiles (at 16px tiles: 4096px)
must be split into multiple VTM1 "screens" that share one VTS1 tileset — see
the Rastan level graphic in the case study, which requires at least 3
screens. This screen-splitting decision also shapes the room/streaming model
in the [engine roadmap](04-engine-roadmap.md).

## VPL1 — Standalone palette (proposed)

Trivial 512-byte dump (256 × RGB444) with a `VPL1` magic, used when a
character and a level are meant to share one palette bank instead of each
asset carrying a redundant copy. Useful once palette offsets (the 4-bit
"palette offset" field in tile/sprite attributes) are used to multiplex
several 16-color sub-palettes out of one 256-entry table.

## VBM2 — Full-screen bitmap format (implemented)

A direct dump of VERA's 320x240 8bpp bitmap-mode layer (see the VERA
reference's "Bitmap mode 1/2/4/8 bpp" and the KERNAL's own
`320x240@256c Bitmap` at `$0:0000-$1:2BFF`), used by `vera-tests/test_player.c`
to show artwork on VERA's own video output while a `.vtm` song plays.
Implemented by `vera-tests/tools/img2vbm.py` (any Pillow-readable source
image in) and `vera-tests/vbm_display.s`/`vbm_loader.c` (Atari-side loader).

```
offset 0:   4 bytes  magic "VBM2"
offset 4:   2 bytes  width  (little-endian) — always 320 today
offset 6:   2 bytes  height (little-endian) — always 240 today
offset 8:   1 byte   bits per pixel (always 8 today)
offset 9:   1 byte   name_len (0-255, byte length of `name` that follows)
offset 10:  2 bytes  reserved (0)
offset 12:  name_len bytes  name — raw ASCII, NOT null-terminated (same
            convention as VTM3's title, vtm_format.md); img2vbm.py defaults
            this to the source image's filename, or takes an explicit name
offset 12+name_len:       512 bytes  palette, 256 entries x 2 bytes, RGB444
offset 12+name_len+512:   width*height bytes  pixel data, 1 byte/pixel, row-major
```

(v1 had no `name`/`name_len` — bumped to v2 when that field was added; no
v1 files exist outside development, so the loader only ever accepts v2.)

Unlike VSP1/VTS1, color index 0 is **not** treated as transparent — this is
a full-screen bitmap meant to be the only thing on VERA's display while
shown (see `test_player.c`: layer 1/text is disabled and the whole screen
goes to this layer), so there's nothing beneath it to show through. The
source image is always letterboxed onto a black 320x240 canvas at
conversion time (scaled down to fit if larger, centered either way, never
scaled up) — the file is always exactly 320x240 regardless of the input
image's own dimensions, so the Atari-side loader never needs to compute
centering itself, only stream bytes straight to VRAM.

Width/height fields are stored (rather than assumed) so a future variant
covering VERA's other bitmap resolution (640x480, 1bpp/2bpp/4bpp) can reuse
the same magic-plus-header shape without a new format version.

## Open items

- 4bpp sprite mode (hardware `Mode=0`) is not yet represented in VSP1 — today
  every sprite is exported as 8bpp. Worth adding once VRAM budget for
  character animation frames is measured on real hardware.
- VTS1/VTM1 are specs only; no exporter implements them yet (tracked in
  [`02-tool-setup.md`](02-tool-setup.md)).
