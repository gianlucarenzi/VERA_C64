# RMT Player - Build and Testing Guide

This document explains how to compile RMT programs with specific songs and how to run tests on the VERA card of the Commodore 64.

## Table of Contents

- [Overview](#overview)
- [Basic Compilation](#basic-compilation)
- [Compiling with Specific Songs](#compiling-with-specific-songs)
- [Supported Formats](#supported-formats)
- [Running Tests](#running-tests)
- [Running on VICE](#running-on-vice)
- [Advanced Options](#advanced-options)
- [Troubleshooting](#troubleshooting)

## Overview

The RMT project contains two main programs:

1. **testrmt.prg** - Minimal RMT player test with text display
2. **rmtplay.prg** - Full RMT player with graphical visualizer (supports VERA and VIC-II)

Both use the Atari RMT player adapted for the VERA PSG (Programmable Sound Generator) of the Commodore 64.

### Features

- **RMT4**: Mono modules with 4 channels
- **RMT8**: Stereo modules with 8 channels
- **Supported formats**: `.rmt` (Revenge of the Music Tracker), `.sap`, `.xex` (RITMO export)
- **Audio**: VERA PSG at 48kHz stereo
- **Screens**: VIC-II (C64), VERA, or both

## Basic Compilation

From the `rmt/` directory:

```bash
cd rmt
make                    # Compiles testrmt.prg and rmtplay.prg (default: gemx.rmt)
```

This will generate:
- `testrmt.prg` - Test version (C64 VIC-II screen)
- `rmtplay.prg` - Player with visualizer (VIC-II and VERA)

## Compiling with Specific Songs

### RMT Format (native)

Compile the player with a specific RMT song:

```bash
make RMT_SONG=music/turrican2_rev2s.rmt
make RMT_SONG=music/PROJECT-X_THESMOPHORIA_pokey.rmt
```

**Available RMT songs:**
- `music/gemx.rmt` (default) - Gemx, RMT4
- `music/turrican2_rev2s.rmt` - Turrican 2 Theme, RMT8
- `music/PROJECT-X_THESMOPHORIA_pokey.rmt` - Project-X, RMT4

### SAP Format (RITMO export)

For `.sap` or `.xex` files exported from RITMO/RMT:

```bash
make RMT_SONG=music/gemx.sap
make RMT_SONG=music/Final_Fantasy.sap
```

**Specify a subsong:**
```bash
make RMT_SONG=music/Final_Fantasy.sap RMT_SUBSONG=0
make RMT_SONG=music/Final_Fantasy.sap RMT_SUBSONG=1
```

**Available SAP songs:**
- `music/gemx.sap` - Gemx (SAP export)
- `music/Final_Fantasy.sap` - Final Fantasy (multiple subsongs)

### Customize Metadata

Specify song name, author, and date:

```bash
make RMT_SONG=music/turrican2_rev2s.rmt \
     RMT_NAME="Turrican 2" \
     RMT_AUTHOR="Rok" \
     RMT_DATE="2024"
```

Metadata is displayed in the player (`rmtplay.prg`). If not specified, the player uses data from the music file.

## Supported Formats

### RMT (Revenge of the Music Tracker)

The native Raster Music Tracker format:
- **Header**: Signature `RMT4` or `RMT8`
- **Channels**: 4-8 channels
- **Usage**: `make RMT_SONG=file.rmt`

Converted to assembly by `tools/rmt2ca65.py` → `gen/song.s`

### SAP / XEX (RITMO export)

Files exported from RITMO or RMT in LZSS format:
- **Extensions**: `.sap`, `.xex`
- **Formats**: SAP is Atari's internal format, XEX is the load format
- **Usage**: `make RMT_SONG=file.sap` or `make RMT_SONG=file.xex`
- **Subsongs**: SAP files can contain multiple songs

Converted by `tools/sap2ca65.py` → `gen/song.s`

**Specify subsongs:**
```bash
make RMT_SONG=file.sap RMT_SUBSONG=0   # First song
make RMT_SONG=file.sap RMT_SUBSONG=1   # Second song
```

## Running Tests

### Minimal Test (testrmt.prg)

The test shows the RMT player with debug information:

```bash
make RMT_SONG=music/turrican2_rev2s.rmt testrmt.prg
```

**Test output:**
```
RMT player: VERA PSG on the C64
===============================
Song: turrican2_rev2s  RMT8
PAL: 50 Hz frames, 312 lines

P PSG on/off  S VERA stereo
RUN/STOP or Q exit

Frames   123  player  45/ 48 lines
  pokey  01  02  03  04  vera  01  02  03  04
R pokey  01  02  03  04  vera  01  02  03  04

Instrument speed: 2
```

**Test commands:**
- `P` - Toggle PSG on/off
- `S` - Toggle VERA stereo
- `RUN/STOP` or `Q` - Exit

### Full Test with Visualizer (rmtplay.prg)

Full player with complete graphical visualizer:

```bash
make RMT_SONG=music/turrican2_rev2s.rmt rmtplay.prg
```

**Features:**
- Display song name/author/date
- Volume bars for each channel (colors: red high, green low)
- AUDF/AUDC/AUDCTL registers in hexadecimal
- Song line, row, speed, tempo
- LZSS stream information for .sap/.xex files

**Player commands:**
- `SPACE` - Pause/Play
- `R` - Restart song
- `S` - VERA stereo on/off
- `RUN/STOP` or `Q` - Exit

## Running on VICE

### Prerequisites

Make sure VICE is compiled with VERA support:
```bash
which x64sc
/home/gianluca/bin/x64sc  # Verify it exists
```

### Launch Test on VICE

```bash
# With default song (gemx.rmt)
make run

# With specific song
x64sc -vera -verabase 0xdf00 -autostart testrmt.prg &

# With saved configuration
x64sc -config ~/.config/vice/vicerc-3.10 -vera -autostart rmtplay.prg &
```

### Utility Scripts

From the project root:
```bash
make run        # Launch VICE with vera-c64.d64 disk

# From project root
tools/run-vice.sh vera-c64.d64
```

## Advanced Options

### VERA Card Location

The VERA card is always at `$DF00` (IO2): IO1 (`$DE00`-`$DEFF`) is the window of
the GeoRAM of the card. VICE: `x64sc -vera -verabase 0xdf00`.

### Select Display

The `rmtplay.prg` player can use:
- VIC-II (traditional C64 screen)
- VERA (new display)
- Both (default)

```bash
make RMT_SCREEN=vera       # VERA only
make RMT_SCREEN=vic        # VIC-II only
make RMT_SCREEN=both       # Both (default)
```

### Number of Channels

Automatically detected from the file:
- RMT4 → 4 channels (mono)
- RMT8 → 8 channels (stereo)

For SAP/XEX files, automatic based on format.

## Build Combinations

### Example 1: Minimal test with Turrican 2

```bash
cd rmt
make RMT_SONG=music/turrican2_rev2s.rmt testrmt.prg
# Generates: testrmt.prg
```

Launch on VICE:
```bash
x64sc -vera -verabase 0xdf00 -autostart testrmt.prg &
```

### Example 2: Full player with Final Fantasy

```bash
cd rmt
make RMT_SONG=music/Final_Fantasy.sap RMT_SUBSONG=0 rmtplay.prg
# Generates: rmtplay.prg with first subsong of Final Fantasy
```

### Example 3: Player with custom metadata

```bash
cd rmt
make RMT_SONG=music/gemx.rmt \
     RMT_NAME="Gemx Theme" \
     RMT_AUTHOR="Jakub Husak" \
     RMT_DATE="2024" \
     RMT_SCREEN=vera \
     rmtplay.prg
```

VERA only, without VIC-II.

### Example 4: Run all tests with all songs

```bash
cd rmt
for song in music/*.rmt; do
    make RMT_SONG="$song" testrmt.prg
    # Run the test
done
```

## Generated Files Structure

During compilation, the `gen/` directory contains:

```
gen/
├── build.cfg          # Build configuration (song, VERA_BASE)
├── songinfo.cfg       # Song metadata
├── screen.cfg         # Display type
├── psgtab.s           # PSG lookup table
├── song.s             # Compiled song (rmt2ca65.py or sap2ca65.py)
├── songinfo.h         # C header with metadata
├── player.o           # Object: rmtplayr.s or lzsplayr.s
├── psgrmt.o           # Object: psgrmt.s (PSG driver)
├── rmtirq.o           # Object: rmtirq.s (IRQ hook)
├── psgtab.o           # Object: psgtab.s
├── song.o             # Object: song.s
├── test_rmt.o         # Object: test_rmt.c
├── rmtplay.o          # Object: rmtplay.c
├── testrmt.map        # Test map (addresses and symbols)
└── rmtplay.map        # Player map
```

## Cleanup

Remove all generated files:

```bash
cd rmt
make clean              # Removes gen/, testrmt.prg, rmtplay.prg
```

## Troubleshooting

### Error: "VERA NOT FOUND"

The program cannot find the VERA card. Possible causes:
- VICE launched without `-vera` flag
- Wrong VERA card address (try `VERA_BASE=0xDF00`)
- VERA not supported in your VICE build

**Solution:**
```bash
# Make sure to use -vera
x64sc -vera -verabase 0xdf00 -autostart testrmt.prg &

# Or compile for the other address
make VERA_BASE=0xDF00 clean
make VERA_BASE=0xDF00 testrmt.prg
```

### Error: "Unknown file format" or "No such file"

The music file doesn't exist or the path is wrong.

**Solution:**
```bash
# Verify the file exists
ls -l music/
# Use full path from rmt/
make RMT_SONG=music/turrican2_rev2s.rmt
```

### Audio distorted or absent

Possible causes:
- PSG disabled in test (press `P` to toggle)
- Volume too low
- VERA audio not configured in VICE

**Solution:**
```bash
# In test, press P to enable PSG
# In rmtplay.prg, check volume bars
# In VICE, check audio settings
```

### SAP file not recognized

The SAP file might be a different format or corrupted.

**Solution:**
```bash
# Check file type
file music/Final_Fantasy.sap

# Try to extract information
python3 rmt/tools/sap2ca65.py music/Final_Fantasy.sap /dev/null --info -
```

## Useful Commands

```bash
# List all songs
ls -l rmt/music/

# Compile everything with defaults
cd rmt && make

# Compile specific song for test
cd rmt && make RMT_SONG=music/turrican2_rev2s.rmt testrmt.prg

# Run test on VICE
x64sc -vera -verabase 0xdf00 -autostart rmt/testrmt.prg &

# Check symbol map (addresses)
cat rmt/gen/testrmt.map | head -20

# Clean up
cd rmt && make clean
```

## References

- **Makefile**: `rmt/Makefile` - Detailed build configuration
- **Test player**: `rmt/test_rmt.c` - C code for test
- **Full player**: `rmt/rmtplay.c` - C code for player
- **PSG driver**: `rmt/psgrmt.s` - VERA PSG implementation
- **IRQ hook**: `rmt/rmtirq.s` - Raster IRQ hook

## Quick Start

```bash
# 1. Build default version
cd rmt && make

# 2. Run on VICE
x64sc -vera -verabase 0xdf00 -autostart rmtplay.prg &

# 3. Build with Turrican 2 and test
make RMT_SONG=music/turrican2_rev2s.rmt testrmt.prg
x64sc -vera -verabase 0xdf00 -autostart testrmt.prg &

# 4. Try different song with metadata
make RMT_SONG=music/Final_Fantasy.sap RMT_SUBSONG=0 \
     RMT_NAME="Final Fantasy" RMT_AUTHOR="Nobuo Uematsu" rmtplay.prg
x64sc -vera -verabase 0xdf00 -autostart rmtplay.prg &
```

---

Documentation for VERA_C64 RMT Player - 2024
