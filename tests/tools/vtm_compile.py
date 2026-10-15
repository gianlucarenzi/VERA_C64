#!/usr/bin/env python3
"""vtm_compile.py — compile a .vtms tracker-source text file into a .vtm
binary module for vtm_player.s. See ../vtm_format.md for the binary layout
and the source language.

Usage: python3 vtm_compile.py song.vtms song.vtm
"""
import sys
import struct

N_CHANNELS = 4
NOTE_HOLD = 0
NOTE_OFF = 97

SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
WAVEFORMS = {"PULSE": 0, "SAWTOOTH": 1, "SAW": 1, "TRIANGLE": 2, "TRI": 2, "NOISE": 3}
PANS = {"LR": 0xC0, "L": 0x40, "R": 0x80, "NONE": 0x00}


class VtmError(Exception):
    def __init__(self, line_no, msg):
        super().__init__(f"line {line_no}: {msg}")


def parse_note_cell(tok, line_no):
    """Returns (note_byte, instr_byte_or_None)."""
    if tok == "...":
        return NOTE_HOLD, None
    if tok.upper() == "OFF":
        return NOTE_OFF, None

    body, sep, instr_s = tok.partition(":")
    instr = None
    if sep:
        if not instr_s.isdigit():
            raise VtmError(line_no, f"bad instrument suffix in '{tok}'")
        instr = int(instr_s)

    if len(body) != 3:
        raise VtmError(line_no, f"bad note '{tok}' (expected e.g. C-4, C#4, OFF, ...)")
    letter, accidental, octave_s = body[0].upper(), body[1], body[2]
    if letter not in SEMITONE:
        raise VtmError(line_no, f"bad note letter in '{tok}'")
    if accidental not in ("#", "-"):
        raise VtmError(line_no, f"bad accidental in '{tok}' (use '#' or '-')")
    if not octave_s.isdigit():
        raise VtmError(line_no, f"bad octave in '{tok}'")
    octave = int(octave_s)
    if not (0 <= octave <= 7):
        raise VtmError(line_no, f"octave out of range 0-7 in '{tok}'")

    semi = SEMITONE[letter] + (1 if accidental == "#" else 0)
    idx = octave * 12 + semi
    if idx > 95:
        raise VtmError(line_no, f"note out of range in '{tok}'")
    return idx + 1, instr


def parse_kv_args(tokens, line_no):
    kv = {}
    for t in tokens:
        if "=" not in t:
            raise VtmError(line_no, f"expected KEY=VALUE, got '{t}'")
        k, v = t.split("=", 1)
        kv[k.upper()] = v.upper()
    return kv


def compile_source(text):
    tempo = 4
    loop_pos = 0
    title = ""
    order = None
    instruments = {}
    patterns = {}
    cur_pattern = None
    cur_rows = None

    for line_no, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        tokens = line.split()
        directive = tokens[0].upper()

        if directive == "TEMPO":
            if len(tokens) != 2 or not tokens[1].isdigit():
                raise VtmError(line_no, "usage: TEMPO <frames-per-row>")
            tempo = int(tokens[1])
            if not (1 <= tempo <= 255):
                raise VtmError(line_no, "TEMPO must be 1-255")
            cur_pattern = None

        elif directive == "LOOP":
            if len(tokens) != 2 or not tokens[1].isdigit():
                raise VtmError(line_no, "usage: LOOP <order-index>")
            loop_pos = int(tokens[1])
            cur_pattern = None

        elif directive == "TITLE":
            rest = line[len(tokens[0]):].strip()
            if len(rest) < 2 or rest[0] != '"' or rest[-1] != '"':
                raise VtmError(line_no, 'usage: TITLE "text"')
            title = rest[1:-1]
            if len(title.encode("ascii", errors="replace")) > 255:
                raise VtmError(line_no, "TITLE must be at most 255 bytes")
            cur_pattern = None

        elif directive == "ORDER":
            if len(tokens) < 2:
                raise VtmError(line_no, "usage: ORDER <pattern> [pattern ...]")
            if not all(t.isdigit() for t in tokens[1:]):
                raise VtmError(line_no, "ORDER values must be pattern indices")
            order = [int(t) for t in tokens[1:]]
            cur_pattern = None

        elif directive == "INSTRUMENT":
            if len(tokens) < 2 or not tokens[1].isdigit():
                raise VtmError(line_no, "usage: INSTRUMENT <index> WAVE=.. PW=.. VOL=.. PAN=.. [DECAY=..]")
            idx = int(tokens[1])
            kv = parse_kv_args(tokens[2:], line_no)
            try:
                wave = WAVEFORMS[kv.get("WAVE", "PULSE")]
                pw = int(kv.get("PW", "0"))
                vol = int(kv.get("VOL", "32"))
                pan = PANS[kv.get("PAN", "LR")]
                decay = int(kv.get("DECAY", "0"))
            except KeyError as e:
                raise VtmError(line_no, f"bad instrument field {e}")
            if not (0 <= pw <= 63):
                raise VtmError(line_no, "PW must be 0-63")
            if not (0 <= vol <= 63):
                raise VtmError(line_no, "VOL must be 0-63")
            if not (0 <= decay <= 63):
                raise VtmError(line_no, "DECAY must be 0-63 (0 = sustain, no envelope)")
            reg2 = pan | vol
            reg3 = (pw << 2) | wave
            instruments[idx] = (reg2, reg3, decay)
            cur_pattern = None

        elif directive == "PATTERN":
            if len(tokens) != 2 or not tokens[1].isdigit():
                raise VtmError(line_no, "usage: PATTERN <index>")
            cur_pattern = int(tokens[1])
            if cur_pattern in patterns:
                raise VtmError(line_no, f"pattern {cur_pattern} redefined")
            cur_rows = []
            patterns[cur_pattern] = cur_rows

        else:
            # Must be a pattern row: N_CHANNELS whitespace-separated cells.
            if cur_pattern is None:
                raise VtmError(line_no, f"unexpected row data outside PATTERN block: '{raw}'")
            if len(tokens) != N_CHANNELS:
                raise VtmError(line_no, f"expected {N_CHANNELS} cells, got {len(tokens)}")
            cur_rows.append([parse_note_cell(t, line_no) for t in tokens])

    if order is None:
        raise VtmError(0, "missing ORDER directive")
    if not patterns:
        raise VtmError(0, "no PATTERN blocks defined")
    for p in order:
        if p not in patterns:
            raise VtmError(0, f"ORDER references undefined pattern {p}")
    if not (0 <= loop_pos < len(order)):
        raise VtmError(0, "LOOP index out of range of ORDER")

    n_instruments = (max(instruments) + 1) if instruments else 0
    for i in range(n_instruments):
        if i not in instruments:
            raise VtmError(0, f"instrument {i} referenced/implied but not defined")
    if n_instruments > 85:
        raise VtmError(0, "too many instruments (player limit is ~85, see vtm_format.md)")

    max_pattern_idx = max(patterns)
    n_patterns = max_pattern_idx + 1
    for p in range(n_patterns):
        if p not in patterns:
            raise VtmError(0, f"pattern index {p} skipped (patterns must be contiguous from 0)")
    if n_patterns > 85:
        raise VtmError(0, "too many patterns (player limit is ~85, see vtm_format.md)")

    # Validate instrument references inside patterns.
    for p_idx, rows in patterns.items():
        for row in rows:
            for note, instr in row:
                if instr is not None and instr >= n_instruments:
                    raise VtmError(0, f"pattern {p_idx} references undefined instrument {instr}")

    return {
        "tempo": tempo,
        "loop_pos": loop_pos,
        "title": title,
        "order": order,
        "instruments": [instruments[i] for i in range(n_instruments)],
        "patterns": [patterns[i] for i in range(n_patterns)],
    }


def build_binary(song):
    order = song["order"]
    instruments = song["instruments"]
    patterns = song["patterns"]

    title_bytes = song["title"].encode("ascii", errors="replace")
    order_bytes = bytes(order)
    instr_bytes = b"".join(struct.pack("<BBB", reg2, reg3, decay) for reg2, reg3, decay in instruments)

    # Pattern cell data, concatenated; remember each pattern's start offset
    # once we know where pattern data begins in the final file.
    pattern_cell_blobs = []
    for rows in patterns:
        blob = bytearray()
        for row in rows:
            for note, instr in row:
                instr_byte = 0xFF if instr is None else instr
                blob += bytes([note, instr_byte])
        pattern_cell_blobs.append(bytes(blob))

    header_size = 13
    pattern_table_size = 3 * len(patterns)
    data_start = (header_size + len(title_bytes) + len(order_bytes)
                  + len(instr_bytes) + pattern_table_size)

    pattern_table = bytearray()
    offset = data_start
    for rows, blob in zip(patterns, pattern_cell_blobs):
        pattern_table += struct.pack("<BH", len(rows), offset)
        offset += len(blob)

    header = struct.pack(
        "<4sBBBBBBxxB",
        b"VTM3",
        N_CHANNELS,
        song["tempo"],
        len(instruments),
        len(patterns),
        len(order),
        song["loop_pos"],
        len(title_bytes),
    )

    out = bytearray()
    out += header
    out += title_bytes
    out += order_bytes
    out += instr_bytes
    out += pattern_table
    for blob in pattern_cell_blobs:
        out += blob
    assert len(out) == offset, "internal offset accounting bug"
    return bytes(out)


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <song.vtms> <song.vtm>", file=sys.stderr)
        return 1
    with open(argv[1], "r") as f:
        text = f.read()
    try:
        song = compile_source(text)
        data = build_binary(song)
    except VtmError as e:
        print(f"{argv[1]}: {e}", file=sys.stderr)
        return 1
    with open(argv[2], "wb") as f:
        f.write(data)
    print(f"{argv[2]}: {len(data)} bytes, "
          f"{len(song['patterns'])} pattern(s), {len(song['instruments'])} instrument(s), "
          f"{len(song['order'])} order entries")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
