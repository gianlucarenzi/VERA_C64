#!/usr/bin/env python3
"""img2vbm.py — convert any Pillow-readable image (PNG/JPEG/BMP/GIF/...)
into a VBM2 file: a direct dump of VERA's 320x240 8bpp bitmap-mode layer,
ready for a straight copy loop into VRAM (see ../vbm_display.s and
../../vera-asset-format.md for the binary layout).

The source image is processed using ImageMagick (via Wand) for optimal color
quantization and dithering, then scaled down to fit within 320x240 if it's
larger in either dimension (never scaled up — a smaller image stays crisp with
black letterboxing instead of blurring to fill the frame) and centered on a
black 320x240 canvas. This means all the framing/scaling work happens here,
once, on the PC — the Atari-side loader never computes anything, it just
streams bytes to VRAM.

Usage:
  python3 img2vbm.py cover.png cover.vbm
  python3 img2vbm.py cover.png cover.vbm "Bubble Bobble"   # explicit name
"""
import os
import sys
import struct
import tempfile

try:
    from PIL import Image
except ImportError:
    sys.exit("img2vbm.py requires Pillow: pip install Pillow")

try:
    from wand.image import Image as WandImage
    HAS_WAND = True
except ImportError:
    HAS_WAND = False

CANVAS_W = 320
CANVAS_H = 240
NAME_MAX = 255


def preprocess_with_imagemagick(src_path):
    """Preprocess image using ImageMagick to optimize colors for VERA's 4-bit
    per channel color space. This mimics: magick input.png +dither -depth 4 -colors 256
    
    Returns the path to the preprocessed image (temporary file).
    """
    if not HAS_WAND:
        print("Warning: Wand not installed. Skipping ImageMagick preprocessing.")
        print("For optimal results, install Wand: pip install Wand")
        return src_path

    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
        tmp_path = tmp.name

    try:
        with WandImage(filename=src_path) as img:
            # Reduce to 256 colors without dithering (matches +dither in ImageMagick)
            # This uses Wand's quantize which is equivalent to ImageMagick's -colors
            img.quantize(number_colors=256, colorspace_type='rgb')
            # Reduce color depth to simulate 4-bit per channel (16 levels per channel)
            img.depth = 4
            # Save the preprocessed image
            img.format = 'png'
            img.save(filename=tmp_path)
            img.close()
        
        return tmp_path
    except Exception as e:
        print(f"Warning: ImageMagick preprocessing failed: {e}")
        return src_path


def build_vbm(src_path, name=None, use_imagemagick=True):
    if name is None:
        name = os.path.splitext(os.path.basename(src_path))[0]
    name_bytes = name.encode("ascii", "replace")[:NAME_MAX]

    # Preprocess with ImageMagick if available
    processed_path = src_path
    temp_file = None
    try:
        if use_imagemagick and HAS_WAND:
            processed_path = preprocess_with_imagemagick(src_path)
            if processed_path != src_path:
                temp_file = processed_path

        src = Image.open(processed_path).convert("RGBA")

        # Composite onto black now (not after resizing) so semi-transparent
        # edges blend with the same black the letterboxing uses, not whatever
        # was behind them in the original file.
        black_src = Image.new("RGB", src.size, (0, 0, 0))
        black_src.paste(src, mask=src.split()[3])

        scale = min(CANVAS_W / black_src.width, CANVAS_H / black_src.height, 1.0)
        new_w = max(1, round(black_src.width * scale))
        new_h = max(1, round(black_src.height * scale))
        if scale < 1.0:
            black_src = black_src.resize((new_w, new_h), Image.LANCZOS)

        canvas = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0))
        canvas.paste(black_src, ((CANVAS_W - new_w) // 2, (CANVAS_H - new_h) // 2))

        vera_lut = [min(15, (v + 8) >> 4) * 17 for v in range(256)]
        canvas_vera = canvas.point(vera_lut * 3)

        temp = canvas_vera.convert("P", palette=Image.ADAPTIVE, colors=255)
        pal_flat = list(temp.getpalette()[:255 * 3])

        has_white = any(
            pal_flat[i * 3] == 255 and pal_flat[i * 3 + 1] == 255 and pal_flat[i * 3 + 2] == 255
            for i in range(len(pal_flat) // 3)
        )
        if not has_white:
            pal_flat = [255, 255, 255] + pal_flat[:254 * 3]

        pal_flat += [0, 0, 0] * (256 - len(pal_flat) // 3)

        palette_img = Image.new("P", (1, 1))
        palette_img.putpalette(pal_flat)
        indexed = canvas.quantize(palette=palette_img, dither=1)

        pixels = indexed.tobytes()
        assert len(pixels) == CANVAS_W * CANVAS_H, "unexpected pixel byte count"

        rgb_palette = pal_flat

        palette_bytes = bytearray()
        for i in range(256):
            r, g, b = rgb_palette[i * 3], rgb_palette[i * 3 + 1], rgb_palette[i * 3 + 2]
            r4, g4, b4 = r >> 4, g >> 4, b >> 4
            palette_bytes.append((g4 << 4) | b4)
            palette_bytes.append(r4)

        header = struct.pack("<4sHHBBxx", b"VBM2", CANVAS_W, CANVAS_H, 8, len(name_bytes))
        result = header + name_bytes + bytes(palette_bytes) + pixels
    finally:
        if temp_file and os.path.exists(temp_file):
            try:
                os.unlink(temp_file)
            except Exception as e:
                print(f"Warning: could not delete temporary file {temp_file}: {e}")
    
    return result


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(f'Usage: python3 {sys.argv[0]} image.png out.vbm ["Display Name"]')

    src_path, out_path = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) == 4 else None
    data = build_vbm(src_path, name)
    with open(out_path, "wb") as f:
        f.write(data)

    print(f"{out_path}: {len(data)} bytes ({CANVAS_W}x{CANVAS_H}, 8bpp)")


if __name__ == "__main__":
    main()
