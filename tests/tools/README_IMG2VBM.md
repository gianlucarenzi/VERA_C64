# img2vbm.py — Convert PNG to VERA VBM Format

## Overview

`img2vbm.py` converts PNG, JPEG, BMP, GIF, and other image formats into **VBM2** files, which are direct dumps of VERA's 320x240 8bpp bitmap-mode layer, ready to be streamed directly into VRAM.

## Features

- **ImageMagick Integration**: Uses Wand (Python API for ImageMagick) for optimal color quantization and dithering
- **Automatic Scaling**: Scales images down to fit within 320x240 while preserving aspect ratio
- **Centered Letterboxing**: Smaller images are centered on a black background
- **VERA-Native Palette**: Automatically generates 256-color palettes optimized for VERA's 4-bit per channel (12-bit) color space
- **Floyd-Steinberg Dithering**: Applies professional dithering for smooth color transitions
- **Graceful Fallback**: Works without Wand (uses Pillow only), though with reduced quality

## Installation

### Prerequisites

- Python 3.6+
- ImageMagick library (system-level)

### Setup

1. **Install Python dependencies**:
   ```bash
   pip install Pillow Wand
   ```

2. **Install ImageMagick system library** (if not already installed):
   
   **On Ubuntu/Debian**:
   ```bash
   sudo apt-get install libmagickwand-dev imagemagick
   ```
   
   **On macOS** (with Homebrew):
   ```bash
   brew install imagemagick
   ```
   
   **On Windows**:
   Download and install from [ImageMagick Downloads](https://imagemagick.org/script/download.php)

## Usage

### Basic Usage

```bash
python3 img2vbm.py input.png output.vbm
```

### With Custom Display Name

```bash
python3 img2vbm.py input.png output.vbm "Game Title"
```

The display name (max 255 characters) is embedded in the VBM file header.

### Examples

```bash
# Convert a game screenshot
python3 img2vbm.py screenshot.png game_screen.vbm "Level 1"

# Convert game artwork
python3 img2vbm.py cover_art.jpg game_cover.vbm "Box Art"

# Convert with auto-generated name (derived from filename)
python3 img2vbm.py my_image.png output.vbm
```

## How It Works

### Processing Pipeline

1. **ImageMagick Preprocessing** (optional, if Wand is installed):
   - Quantizes image to 256 colors
   - Reduces color depth to 4-bit per channel
   - Applies ImageMagick's optimal color space conversion
   - Command equivalent: `magick input.png +dither -depth 4 -colors 256 intermediate.png`

2. **Image Composition**:
   - Converts image to RGBA format
   - Composites onto black background (preserves alpha channel)
   - Scales to fit within 320x240 maintaining aspect ratio
   - Centers on black 320x240 canvas

3. **VERA Palette Quantization**:
   - Maps colors to VERA's 12-bit color space (4 bits per channel)
   - Uses Pillow's median-cut algorithm to select optimal 256 colors
   - Reserves pure white (255,255,255) to prevent loss of highlights
   - Applies Floyd-Steinberg dithering for smooth transitions

4. **VBM2 File Generation**:
   - Builds VBM2 header with image metadata
   - Encodes 256-entry palette in VERA format (GGGGBBBB + 0000RRRR)
   - Writes 320×240 pixel data (76,800 bytes)

### VBM2 File Format

```
Offset  Size    Content
------  ----    -------
0x00    4       Magic: "VBM2"
0x04    2       Width (320)
0x06    2       Height (240)
0x08    1       Bit depth (8)
0x09    1       Name length
0x0A    2       Reserved (0x0000)
0x0C    N       Name (0-255 bytes, ASCII)
0x0C+N  512     VERA Palette (256 entries × 2 bytes)
0x20C+N 76800   Pixel data (320 × 240 × 1 byte)
```

## Quality Settings

The current implementation uses:
- **Quantization**: Adaptive/median-cut (256 colors)
- **Dithering**: Floyd-Steinberg (highest quality)
- **Color Space**: VERA's native 12-bit (4:4:4) RGB
- **Scaling**: LANCZOS (high-quality downsampling)

## Troubleshooting

### "Warning: Wand not installed..."

The script detects that Wand/ImageMagick preprocessing is not available. The conversion still works using Pillow alone, but with slightly lower quality. To fix:

```bash
pip install Wand
```

### ImageMagick policy errors

If you get "not authorized" errors from ImageMagick, you may need to update the ImageMagick security policy. Edit `/etc/ImageMagick-6/policy.xml` (on Ubuntu) and comment out restrictive policies for PNG/JPEG.

### "temporary file not found" warnings

These are non-critical warnings that don't affect output. They occur when the temp file cleanup races with the OS. Safe to ignore.

## Performance

- Typical conversion time: 0.5-2 seconds per image
- Output file size: ~76 KB (fixed: 76,800 pixels + palette + header)
- Memory usage: ~20-50 MB (temporary canvas + processing buffers)

## See Also

- `vbm_display.s` — VERA assembly loader for VBM2 files
- `vbm_loader.c` — C-language VBM2 loader
- `vbm.h` — VBM2 format header and constants
- `01-vera-asset-format.md` — VERA asset format documentation
