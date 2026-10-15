# VERA_C64 - VERA Card Support for Commodore 64

A comprehensive software suite for the **VERA card** expansion port of the Commodore 64, featuring a VGA screen driver, test suite, and music player for the VERA PSG (Programmable Sound Generator).

## 🎯 Project Overview

This project provides three main components for the VERA card on Commodore 64:

1. **VERA Screen Driver** - Redirects C64 screen output to VGA (640x480)
2. **Test Suite** - Comprehensive tests for VERA functionality
3. **RMT Music Player** - Plays Atari RMT music on VERA PSG with visualizer

### What is VERA?

VERA is an expansion card for the Commodore 64 that adds:
- **Graphics**: VGA output (640x480 pixels)
- **Audio**: PSG (Programmable Sound Generator) with 16 voices
- **Memory**: 128KB of dedicated VRAM

The VERA card connects to the C64 expansion port and operates at two possible memory addresses:
- `$DE00` (IO1, default)
- `$DF00` (IO2)

## 📦 Project Components

### 1. Screen Driver (`driver/`)

Moves the C64 KERNAL screen output to VERA while keeping BASIC, LIST, PRINT, INPUT, and the screen editor fully functional.

**Three display modes available:**

| Program | Columns | Rows | Font | VERA Output | Notes |
|---------|---------|------|------|-------------|-------|
| `vera80.prg` | 80 | 30 | 8x16 | 640x480 | Lines doubled for readability |
| `vera60.prg` | 80 | 60 | 8x8 | 640x480 | Full resolution display |
| `vera40.prg` | 40 | 30 | 8x8 | 320x240 (2x scaled) | Traditional C64 aspect ratio |

**Features:**
- Hooks KERNAL vectors (IBSOUT, IBASIN, CINV, IERROR)
- Supports all C64 screen codes (colors, reverse, cursor keys, etc.)
- Transparent operation - BASIC doesn't know it's using VERA
- ~2.5 KB resident code in RAM
- No zero page usage

**How it works:**
```
LOAD"VERA80",8
RUN
```

The driver installs into `$C000` and displays `READY.` on VERA. All subsequent BASIC output goes to VGA.

### 2. Test Suite (`tests/`)

Comprehensive test programs for VERA functionality, including:
- VERA memory access patterns
- Layer configuration tests
- Graphics and palette tests
- Performance benchmarks

### 3. RMT Music Player (`rmt/`)

Full-featured music player for Revenge of the Music Tracker (RMT) files and SAP/XEX exports, with:
- Support for RMT4 (4-channel mono) and RMT8 (8-channel stereo)
- Real-time visualizer on C64 screen and/or VERA display
- Volume bars, frequency display, playback info
- Play/pause, restart, stereo toggle controls

**Two player variants:**
- `testrmt.prg` - Minimal test (debug info on VIC-II)
- `rmtplay.prg` - Full player with visualizer (VIC-II + VERA)

**Available music:**
- Turrican 2 Theme
- Gemx
- Project-X
- Final Fantasy (SAP export with multiple subsongs)

See [rmt/BUILD_GUIDE.md](rmt/BUILD_GUIDE.md) for detailed compilation instructions.

## 🚀 Quick Start

### Prerequisites

- **Emulator**: [VICE](https://vice-emu.sourceforge.io/) with VERA card support (branch `vera-io1`)
- **Compiler**: [cc65](https://cc65.github.io/) (ca65/ld65)
- **Tools**: Python 3, c1541 (for D64 disk images)

### Build Everything

```bash
# Compile all components and create vera-c64.d64 disk image
make

# Or compile for VERA at $DF00 instead of $DE00
make VERA_BASE=0xDF00

# Clean all generated files
make clean
```

### Run on VICE

```bash
# Launch VERA-enabled VICE with disk
make run

# Or manually:
x64sc -vera vera-c64.d64 &

# Or with autostart:
x64sc -vera -autostart driver/vera80.prg &
x64sc -vera -autostart rmt/rmtplay.prg &
```

## 📚 Documentation

- **[driver/README.md](driver/README.md)** - Screen driver architecture, memory layout, limitations
- **[rmt/BUILD_GUIDE.md](rmt/BUILD_GUIDE.md)** - How to compile RMT player with specific songs
- **[VIC_MIRROR_GUIDE.md](VIC_MIRROR_GUIDE.md)** - VIC-II screen mirroring techniques and examples
- **[Documentation/](Documentation/)** - VERA technical references and X16 specs

### VERA Resources

- **VERA Programmer's Reference**: `Documentation/X16 Reference - 09 - VERA Programmer's Reference.md`
- **VERA FX Reference**: `Documentation/X16 Reference - 10 - VERA FX Reference.md`
- **Sound Programming**: `Documentation/X16 Reference - 11 - Sound Programming.md`

## 📋 Directory Structure

```
VERA_C64/
├── README.md                                    # This file
├── LICENSE                                      # BSD 2-Clause License
├── Makefile                                     # Main build system
├── VIC_MIRROR_GUIDE.md                         # VIC-II mirroring techniques
│
├── driver/                                      # VERA Screen Driver
│   ├── README.md                               # Driver documentation
│   ├── vera_screen.s                           # Main driver (assembly)
│   ├── vera80.prg                              # 80x30 display
│   ├── vera60.prg                              # 80x60 display
│   ├── vera40.prg                              # 40x30 display
│   ├── font/                                   # PETSCII fonts
│   └── gen/                                    # Generated files
│
├── rmt/                                         # RMT Music Player
│   ├── BUILD_GUIDE.md                          # Build instructions
│   ├── rmtplay.prg                             # Full player
│   ├── testrmt.prg                             # Minimal test
│   ├── rmtplay.c                               # Player UI (C)
│   ├── test_rmt.c                              # Test UI (C)
│   ├── rmtplayr.s                              # RMT engine (assembly)
│   ├── psgrmt.s                                # PSG driver (assembly)
│   ├── rmtirq.s                                # IRQ hook (assembly)
│   ├── music/                                  # Music files
│   │   ├── gemx.rmt / gemx.sap
│   │   ├── turrican2_rev2s.rmt
│   │   ├── PROJECT-X_THESMOPHORIA_pokey.rmt
│   │   └── Final_Fantasy.sap
│   ├── tools/                                  # Build tools
│   │   ├── rmt2ca65.py                         # RMT → assembly converter
│   │   ├── sap2ca65.py                         # SAP/XEX → assembly converter
│   │   └── mkpsgtab.py                         # PSG lookup table generator
│   └── gen/                                    # Generated files
│
├── tests/                                       # Test Suite
│   ├── vic_mirror.c                            # VIC-II mirroring example
│   └── [Various other test programs]
│
├── include/                                     # C/ASM Headers
│   ├── vera.h                                  # VERA C API
│   ├── vera_detect.h                           # VERA detection
│   └── vera_common.inc                         # Assembly constants
│
├── tools/                                       # Project Tools
│   └── run-vice.sh                             # VICE launcher script
│
├── vera-c64.d64                                # Compiled disk image
└── Documentation/                              # Technical references
    ├── X16 Reference - 09 - VERA Programmer's Reference.md
    ├── X16 Reference - 10 - VERA FX Reference.md
    ├── X16 Reference - 11 - Sound Programming.md
    └── X16 Reference - Appendix A - Sound.md
```

## 🔨 Build System

The project uses **GNU Make** with a hierarchical build structure:

```bash
# Main Makefile delegates to subdirectories
make                    # Build all: driver/, rmt/, tests/
make VERA_BASE=0xDF00   # Compile for alternate VERA address
make run                # Start VICE with vera-c64.d64
make clean              # Remove all generated files
```

### Individual Builds

```bash
# Build just the driver
cd driver && make

# Build just the RMT player with a specific song
cd rmt && make RMT_SONG=music/turrican2_rev2s.rmt rmtplay.prg

# Run tests
cd tests && make
```

## 🎮 Usage Examples

### Run the Screen Driver

```bash
x64sc -vera -autostart driver/vera80.prg &
# Then load and run BASIC programs:
# LOAD"PROGRAM",8
# RUN
```

### Compile and Play a Specific Song

```bash
cd rmt
make RMT_SONG=music/turrican2_rev2s.rmt rmtplay.prg
x64sc -vera -autostart rmtplay.prg &
```

### Create a Custom Player with Metadata

```bash
cd rmt
make RMT_SONG=music/Final_Fantasy.sap RMT_SUBSONG=0 \
     RMT_NAME="Final Fantasy Theme" \
     RMT_AUTHOR="Nobuo Uematsu" \
     RMT_DATE="2024" \
     rmtplay.prg
```

### Build for VERA at $DF00

```bash
make VERA_BASE=0xDF00 clean
make VERA_BASE=0xDF00
x64sc -vera -autostart driver/vera80.prg &
```

## 🛠️ Development

### Requirements

- **VICE** with VERA support (`vera-io1` branch)
  ```bash
  git clone https://github.com/gianlucarenzi/vice.git -b vera-io1
  # Compile and install VICE
  ```

- **CC65** compiler suite
  ```bash
  # On Linux
  sudo apt-get install cc65
  # Or compile from source: https://github.com/cc65/cc65
  ```

- **Python 3** for build tools
  ```bash
  sudo apt-get install python3
  ```

- **c1541** for D64 disk image creation
  ```bash
  # Included with VICE installation
  ```

### Project Configuration

**VERA Base Address:**
- Default: `$DE00` (IO1)
- Alternate: `$DF00` (IO2)

Set via environment variable:
```bash
export VERA_BASE=0xDF00
make clean && make
```

**Architecture:**
- 6502 assembly (`.s` files)
- CC65 C compiler (`-t c64` target)
- Linker configuration (`.cfg` files)

### Memory Layout

**C64 RAM:**
```
$0000-$00FF  - Zero page (system)
$0100-$01FF  - Hardware stack
$0200-$07FF  - System variables
$0800-$1FFF  - Available RAM
$2000-$9FFF  - Program area
$A000-$BFFF  - Basic ROM / Cartridge
$C000-$CFFF  - Driver resident code (screen driver)
$D000-$DFFF  - I/O area
$E000-$FFFF  - Kernal ROM
```

**VERA VRAM:**
```
$00000-$00FFF  - Reserved
$01000-$01FFF  - RMT player font (rmtplay.prg)
$1B000-$1EFFF  - Screen driver tilemap (vera80/60/40.prg)
$1F000-$1FFFF  - Screen driver font
```

## 🐛 Troubleshooting

### "VERA NOT FOUND" Error

The VERA card is not detected. Possible causes:
- VICE not launched with `-vera` flag
- Wrong VERA address (try `VERA_BASE=0xDF00`)
- VERA not supported in VICE build

**Solution:**
```bash
# Ensure -vera is used
x64sc -vera -autostart program.prg &

# Or rebuild for the correct address
make VERA_BASE=0xDF00 clean && make VERA_BASE=0xDF00
```

### Build Errors

**Missing cc65:**
```bash
sudo apt-get install cc65
```

**Python not found:**
```bash
python3 rmt/tools/rmt2ca65.py file.rmt out.s
```

**c1541 not found:**
- c1541 is part of VICE. Check your VICE installation.

## 📖 Further Reading

- **Commander X16**: https://www.commanderx16.com/
- **VERA Emulator**: https://github.com/mooinglemur/vera-emulator
- **RITMO Music Tracker**: https://github.com/gianlucarenzi/RITMO-Music-Tracker
- **CC65 Documentation**: https://cc65.github.io/doc/

## 📄 License

This project is licensed under the **BSD 2-Clause License**.

See [LICENSE](LICENSE) file for details.

**Copyright © 2026 Gianluca Renzi**

### Attribution

- **PETSCII Font**: Public domain from Commander X16 ROM
- **RMT Player**: Based on Atari RMT engine
- **VERA Card Design**: From Commander X16 project

## 🤝 Contributing

Contributions, bug reports, and suggestions are welcome!

## 📮 Contact

Author: **Gianluca Renzi**

---

**Last Updated**: 2024

**Status**: Active Development

**Supported VERA Cards**: All versions compatible with Commodore 64 expansion port
