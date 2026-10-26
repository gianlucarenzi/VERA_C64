# VERA screen driver for the C64

The C64 counterpart of `VERA.SYS` of the Atari PBI card: a small resident
driver that moves the screen of the KERNAL to the VERA. BASIC, `LIST`,
`PRINT`, `INPUT` and the screen editor then work on the VGA picture of the
card.

| Program      | Screen | Font                         | VERA output                |
|--------------|--------|------------------------------|----------------------------|
| `vera80.prg` | 80x30  | 8x16 (PETSCII, lines doubled) | 640x480                    |
| `vera60.prg` | 80x60  | 8x8                          | 640x480                    |
| `vera40.prg` | 40x30  | 8x8                          | 320x240, scaled 2x         |

## Use

    LOAD"VERA80",8
    RUN

The program prints `VERA NOT FOUND AT $DE00` when the card does not answer
(the identity bytes of DCSEL 63 are checked). Otherwise it:

1. copies the resident part to `$C000`;
2. loads the two PETSCII character sets into VRAM;
3. sets up layer 1 and hooks the KERNAL vectors;
4. clears the VERA screen and prints a banner.

BASIC's `READY.` then appears on the VERA.

Running the program again, or another of the three, replaces the driver
already in memory.

In VICE (the build with the VERA card, branch `main` of
https://github.com/gianlucarenzi/vice):

    x64sc -vera -autostart vera80.prg

## Build

    make                    # card at $DE00 (IO1)
    make VERA_BASE=0xDF00   # card at $DF00 (IO2)

The tools needed are `ca65`/`ld65` (cc65) and `python3`. `mkfont.py`
builds the fonts in `gen/`.

## How it works

| Vector            | Hook          | What it does |
|-------------------|---------------|--------------|
| `IBSOUT` ($0326)  | `chrout_hook` | Output to device 3 (the screen) is drawn on the VERA; other devices go to the old vector. |
| `IBASIN` ($0324)  | `chrin_hook`  | Input from device 0 (the keyboard) is the screen editor on the VERA; other devices go to the old vector. |
| `CINV` ($0314)    | `irq_hook`    | Cursor blink; the font follows C= + SHIFT (`$D018` bit 1), the VERA border follows `$D020`; then the old IRQ handler. |
| `IERROR` ($0300)  | `ierror_hook` | When the KERNAL vectors were restored (RUN/STOP + RESTORE), it hooks them again and clears the screen; then the old vector. |

**Output.** All the C64 screen codes are handled:
- RETURN and shifted RETURN;
- CLR, HOME and the four cursor keys;
- DEL and INST, which shift the rest of the logical line;
- RVS ON and RVS OFF;
- the 16 colour codes;
- the case codes 14 and 142, and the lock codes 8 and 9.

Quote mode and insert mode show control codes reversed, as the KERNAL
does. The text colour is `COLOR` ($0286), so `POKE 646,n` works. The
background is taken from `$D021` at every CLR. The default palette of the
VERA starts with the 16 C64 colours.

**Logical lines.** A line is 80 characters long, as on the C64:
- in 80 columns it is one row;
- in 40 columns it is up to two rows, and a link table marks the
  continuation rows.

When printing wraps into the second row of a line, a blank row is opened
below it, as the C64 does.

**Input.** CHRIN echoes the keys through the same output code, so the
cursor and editing keys work while typing. On RETURN the whole logical
line under the cursor is read back from VRAM:
- screen codes are converted to PETSCII;
- inside quotes, reversed characters become control codes again;
- trailing spaces are cut.

When the input started on that same line, reading starts where it
started, so the `? ` of `INPUT` and its prompt are skipped. As in the
KERNAL, the RETURN is not printed when the output device is the screen
(BASIC prints it). You can edit `LIST` output by moving the cursor onto a
line and pressing RETURN.

**PLOT.** The cursor position is mirrored to `PNTR` ($D3) and `TBLX`
($D6), so `POS()` and `TAB()` work. A position set with PLOT (or a POKE) is
taken over before the next character is printed.

**Scrolling and clearing** use the FX 32-bit cache, as the Atari driver
does:
- a row copy reads 4 bytes through DATA0 and writes them with one DATA1
  write;
- a clear writes 4 bytes per store.

**VIC-II.** The VIC screen is left alone: it keeps what it showed when the
driver was installed. Mirroring it would not work, because its 40x25
geometry differs from the VERA's.

## Memory map

| Address           | Content |
|-------------------|---------|
| `$0801-`          | BASIC stub, installer and the fonts (used only at install time) |
| `$C000`           | `JMP` to reinstall (`SYS 49152`) |
| `$C003`           | signature `VERA` |
| `$C007-$C00E`     | the old `IBSOUT`, `IBASIN`, `CINV`, `IERROR` |
| `$C00F-$C9B2`     | resident code and variables (about 2.5 KB; the exact end is in `gen/vera*.map`) |

No zero page is used by the resident code. The installer borrows
`$FB-$FC` and restores them.

**VRAM** (bank 1, the same tilemap as the Atari driver):

| Address           | Content |
|-------------------|---------|
| `$1B000-$1EFFF`   | layer 1 tilemap, 128x64 cells, 256 bytes per row |
| `$19000-`         | font set 0 (upper case/graphics): 256 characters, `$80-$FF` are the reversed `$00-$7F` |
| `$19000 + 256*h`  | font set 1 (lower/upper case), where h = 8 or 16 lines per character |

Layer 0, the sprites, the PSG and the palette are not touched.

## Reset, RUN/STOP + RESTORE

- **RUN/STOP + RESTORE:** the KERNAL restores its vectors and BASIC's
  warm start goes through `IERROR`. The driver hooks itself again and
  clears the VERA screen.
- **Reset:** the driver stays in RAM (the KERNAL RAM test does not
  destroy memory) and the fonts stay in VRAM (the VERA has no reset
  input). `SYS 49152` installs it again.

## Fonts

`font/petscii.s` is the PETSCII character set of the Commander X16 ROM
(`x16-rom/charset/petscii.s`), which is in the **public domain** (see
`x16-rom/LICENSE.md`). It holds the C64 upper case/graphics and
lower/upper case sets in screen code order, 128 characters each.
`mkfont.py` adds the reversed half and, for `vera80`, doubles every line
for the 8x16 font. The Commodore character ROM is not used.

## Limits

- **Border:** the VERA border is not visible in these modes, because the
  active area covers the whole 640x480 picture. It follows `$D020`
  anyway.
- **Background:** a change of `$D021` takes effect at the next CLR. Cells
  that are already drawn keep their background.
- **Reading the screen:** `CHRIN` from device 3 (`OPEN 1,3` then
  `INPUT#1`/`GET#1`) still goes to the KERNAL, which reads the VIC
  screen.
- **INST** only works while the last character of the logical line is a
  space. The KERNAL would make the line longer instead.
- **Other programs:** a program that writes the VIC screen directly, or
  that changes the VERA layer 1 setup, is not seen by the driver. After
  such a program, `SYS 49152` sets the VERA screen up again.
- **IRQ handlers:** the IRQ hook touches only CTRL (saved and restored),
  `DC_BORDER` and `L1_TILEBASE`. Only while CHRIN waits for a key does it
  also touch data port 0 under the cursor. A program that installs its
  own IRQ handler should chain to the old vector.
