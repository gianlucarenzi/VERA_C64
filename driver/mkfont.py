#!/usr/bin/env python3
"""mkfont.py - the PETSCII fonts of the VERA screen driver.

Reads the two 128 character sets of font/petscii.s (the X16 ROM charset,
public domain: upper case/graphics, then lower/upper case, C64 screen code
order) and writes the font the driver loads into VRAM:

    set 0 (upper/graphics): screen codes $00-$7F, then $80-$FF reversed
    set 1 (lower/upper):    the same

8 bytes per character, or 16 with --height 16 (every line doubled).

usage: mkfont.py petscii.s out.bin [--height 8|16]
"""

import re
import sys


def main():
    args = sys.argv[1:]
    height = 8
    if '--height' in args:
        i = args.index('--height')
        height = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2 or height not in (8, 16):
        sys.exit(__doc__)
    src, out = args
    rows = [int(m.group(1), 2) for m in
            re.finditer(r'^\s*\.byte\s+%([01]{8})', open(src).read(), re.M)]
    if len(rows) != 2 * 128 * 8:
        sys.exit('%s: %d bytes, 2048 expected' % (src, len(rows)))
    font = bytearray()
    for s in range(2):
        chars = [rows[(s * 128 + c) * 8:(s * 128 + c + 1) * 8] for c in range(128)]
        for rev in (False, True):
            for ch in chars:
                for b in ch:
                    b = b ^ 0xFF if rev else b
                    font += bytes([b]) * (height // 8)
    open(out, 'wb').write(font)


if __name__ == '__main__':
    main()
