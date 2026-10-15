#!/usr/bin/env python3
"""vgm2vtms.py — convert a VGM (Video Game Music) log of an AY-3-8910 /
YM2149 PSG (the sound chip in the MSX, and many other 8-bit systems) into
a .vtms tracker source for vtm_compile.py / vtm_player.s.

Scope and known limitations (read before trusting the output):
  - Only AY8910/YM2149 register writes (VGM command 0xA0) are interpreted.
    Any other sound chip logged in the same file (YM2151 FM, SN76489,
    Konami SCC, ...) is correctly skipped over (so parsing doesn't desync)
    but not decoded — many MSX rips drive their melodic content through
    SCC's 5-channel wavetable synth rather than the AY/PSG, in which case
    converting only the AY part will produce a mostly-empty result.
  - AY can mix tone AND noise on the same channel; a VERA PSG voice is one
    waveform at a time. When both are enabled, tone wins and the noise
    layer is dropped.
  - AY's hardware envelope mode (volume register bit 4) is approximated
    as a fixed volume — the real envelope shape/period/repeat behaviour
    isn't reproduced.
  - One output row = one video frame (60 Hz by default, 50 Hz with --pal).
    This matches how most AY music-routines actually tick, so it avoids
    guessing a musical grid — at the cost of producing verbose,
    non-hand-authored-looking patterns.
  - Volume (0-15 on AY) and pitch (arbitrary Hz) are both quantised: volume
    linearly to VERA's 0-63 range, pitch to the nearest of VERA's 96 equal-
    tempered notes. Neither chip's real curve matches these exactly.
  - Long songs are split across multiple PATTERN blocks (each capped at
    255 rows, the format's per-pattern limit — see vtm_format.md) chained
    by ORDER; the VGM loop point (if present) becomes a pattern boundary
    so LOOP can reference it exactly.

Multiple input files are concatenated in the order given — e.g. a game's
separate "intro" and "main theme" rips — with only the LAST file's own
loop point (or its start, if it has none) used as the final LOOP target;
earlier files play once, straight through.

Usage:
  python3 vgm2vtms.py song.vgm song.vtms [--pal]
  python3 vgm2vtms.py song.vgz song.vtms          (gzip-compressed VGM ok)
  python3 vgm2vtms.py intro.vgm theme.vgm out.vtms [--pal]
"""
import sys
import gzip
import struct
import math

N_CHANNELS = 4          # VTM format channel count (AY has 3; 4th unused)
AY_CHANNELS = 3
VGM_SAMPLE_RATE = 44100.0
FRAME_HZ_NTSC = 60
FRAME_HZ_PAL = 50
MAX_ROWS_PER_PATTERN = 255
NOTE_OFF = "OFF"
HOLD = "..."

VERA_HW_RATE = 25000000.0 / 512.0   # 48828.125 Hz, see vtm_format.md


def note_index_for_hz(hz):
    """Nearest of VERA's 96 notes (0=C-0..95=B-7), A-4=440Hz, clamped."""
    if hz <= 0:
        return None
    n = 12 * math.log2(hz / 440.0) + 57
    idx = int(round(n))
    return max(0, min(95, idx))


def note_name(idx):
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    return f"{names[idx % 12]}{idx // 12}"


# ---------------------------------------------------------------------------
# VGM parsing
# ---------------------------------------------------------------------------

class VgmError(Exception):
    pass


def load_vgm_bytes(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    if raw[:4] != b"Vgm ":
        raise VgmError("not a VGM file (missing 'Vgm ' magic)")
    return raw


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def parse_header(buf):
    version = u32(buf, 0x08)

    data_offset = 0x40
    if version >= 0x150 and len(buf) >= 0x38:
        rel = u32(buf, 0x34)
        if rel != 0:
            data_offset = 0x34 + rel

    ay_clock = u32(buf, 0x74) if len(buf) >= 0x78 else 0

    loop_rel = u32(buf, 0x1C) if len(buf) >= 0x20 else 0
    loop_offset = (0x1C + loop_rel) if loop_rel != 0 else None

    return {
        "version": version,
        "data_offset": data_offset,
        "ay_clock": ay_clock,
        "loop_offset": loop_offset,
    }


def parse_gd3(buf, gd3_offset):
    try:
        if buf[gd3_offset:gd3_offset + 4] != b"Gd3 ":
            return None
        length = u32(buf, gd3_offset + 8)
        data = buf[gd3_offset + 12:gd3_offset + 12 + length]
        # Decode the whole blob first, THEN split on NUL — splitting the raw
        # bytes on b"\x00\x00" instead misaligns whenever a field is empty
        # (two adjacent terminators = an odd run of zero bytes relative to a
        # 2-byte search pattern), corrupting the following field.
        text = data.decode("utf-16-le", errors="replace")
        parts = text.split("\x00")
        track_name = parts[0] if parts else ""
        return track_name.strip()
    except Exception:
        return None


def parse_commands(buf, data_offset, loop_offset=None):
    """Walk the VGM command stream once, returning:
       events:      list of (sample_pos, reg, value) for every AY8910 write
       end_sample:  total elapsed sample_pos (VGM's native 44100Hz clock)
       loop_sample: sample_pos at which byte offset == loop_offset, or None
    Raises VgmError on any command this parser doesn't know the length of,
    rather than silently guessing and desyncing.
    """
    pos = data_offset
    sample_pos = 0
    events = []
    loop_sample = None
    n = len(buf)

    while pos < n:
        if loop_offset is not None and pos == loop_offset:
            loop_sample = sample_pos
        op = buf[pos]

        if op == 0xA0:
            reg, val = buf[pos + 1], buf[pos + 2]
            if reg < 16:
                events.append((sample_pos, reg, val))
            pos += 3
        elif op == 0x61:
            sample_pos += struct.unpack_from("<H", buf, pos + 1)[0]
            pos += 3
        elif op == 0x62:
            sample_pos += 735
            pos += 1
        elif op == 0x63:
            sample_pos += 882
            pos += 1
        elif 0x70 <= op <= 0x7F:
            sample_pos += (op & 0x0F) + 1
            pos += 1
        elif 0x80 <= op <= 0x8F:
            sample_pos += (op & 0x0F)
            pos += 1
        elif op == 0x66:
            break
        elif op == 0x67:
            size = struct.unpack_from("<I", buf, pos + 3)[0]
            pos += 7 + size
        elif op == 0x68:
            pos += 12
        elif op == 0x4F or op == 0x50:
            pos += 2
        elif 0x51 <= op <= 0x5F:
            pos += 3
        elif op == 0x90:
            pos += 5
        elif op == 0x91:
            pos += 5
        elif op == 0x92:
            pos += 6
        elif op == 0x93:
            pos += 11
        elif op == 0x94:
            pos += 2
        elif op == 0x95:
            pos += 5
        elif 0xA1 <= op <= 0xAF:
            pos += 3               # second-chip "aa dd" writes (v1.51+)
        elif 0xB0 <= op <= 0xBF:
            pos += 3               # "aa dd" writes for chips added in v1.60/1.70
        elif 0xD0 <= op <= 0xD6:
            pos += 4               # "pp aa dd" writes, e.g. 0xD2 = Konami SCC1
        elif op == 0xE0:
            pos += 5
        else:
            raise VgmError(
                f"unsupported VGM command 0x{op:02X} at file offset 0x{pos:X} — "
                "this parser only knows the core v1.10-ish command set plus "
                "AY8910 writes; the file likely uses a chip/feature this tool "
                "doesn't decode."
            )

    return events, sample_pos, loop_sample


# ---------------------------------------------------------------------------
# AY register state -> per-channel note events
# ---------------------------------------------------------------------------

def derive_channel_state(regs, ch):
    """regs: 16-byte AY shadow register array. ch: 0=A,1=B,2=C.
    Returns (waveform, hz, ay_vol4, envelope_mode) or None if silent."""
    fine = regs[2 * ch]
    coarse = regs[2 * ch + 1] & 0x0F
    tone_period = (coarse << 8) | fine

    noise_period = regs[6] & 0x1F

    mixer = regs[7]
    tone_enabled = not ((mixer >> ch) & 1)
    noise_enabled = not ((mixer >> (ch + 3)) & 1)

    vol_reg = regs[8 + ch]
    ay_vol4 = vol_reg & 0x0F
    envelope_mode = bool((vol_reg >> 4) & 1)

    if not (tone_enabled or noise_enabled):
        return None
    if ay_vol4 == 0 and not envelope_mode:
        return None

    ay_clock = regs[16]  # stashed by caller (index 16 = clock, not a real AY reg)

    if tone_enabled:
        period = max(tone_period, 1)
        hz = ay_clock / (16.0 * period)
        waveform = "PULSE"
    else:
        period = max(noise_period, 1)
        hz = ay_clock / (16.0 * period)
        waveform = "NOISE"

    return (waveform, hz, ay_vol4, envelope_mode)


def build_channel_timeline(events, ay_clock, total_samples, frame_hz):
    """Returns per-channel list of (frame_index, waveform, hz, ay_vol4,
    envelope_mode) — one entry per state change, deduplicated."""
    regs = [0] * 17
    regs[16] = ay_clock

    per_channel_raw = [[] for _ in range(AY_CHANNELS)]
    last_state = [None] * AY_CHANNELS

    def emit(ch, sample_pos):
        st = derive_channel_state(regs, ch)
        if st != last_state[ch]:
            frame = round(sample_pos * frame_hz / VGM_SAMPLE_RATE)
            per_channel_raw[ch].append((frame, st))
            last_state[ch] = st

    for sample_pos, reg, val in events:
        regs[reg] = val
        if reg in (0, 1):
            emit(0, sample_pos)
        elif reg in (2, 3):
            emit(1, sample_pos)
        elif reg in (4, 5):
            emit(2, sample_pos)
        elif reg == 6:
            for ch in range(AY_CHANNELS):
                emit(ch, sample_pos)
        elif reg == 7:
            for ch in range(AY_CHANNELS):
                emit(ch, sample_pos)
        elif reg == 8:
            emit(0, sample_pos)
        elif reg == 9:
            emit(1, sample_pos)
        elif reg == 10:
            emit(2, sample_pos)
        # registers 11-13 (envelope period/shape) intentionally not tracked:
        # envelope-mode channels use a fixed approximated volume (see module
        # docstring), so their exact timing doesn't affect our output.

    return per_channel_raw


# ---------------------------------------------------------------------------
# .vtms emission
# ---------------------------------------------------------------------------

def build_vtms(songs, source_names):
    """songs: list of {'per_channel_raw', 'total_frames', 'loop_frame'} dicts,
    concatenated in order. Only the LAST song's own loop_frame (or its start,
    if it has none) becomes the final LOOP target; earlier songs play once."""
    instruments = {}   # (waveform, ay_vol4) -> index
    instrument_lines = []

    def instrument_for(waveform, ay_vol4):
        key = (waveform, ay_vol4)
        if key in instruments:
            return instruments[key]
        idx = len(instruments)
        instruments[key] = idx
        vera_vol = round(ay_vol4 * 63 / 15)
        pw = 32 if waveform == "PULSE" else 48
        instrument_lines.append(
            f"INSTRUMENT {idx} WAVE={waveform} PW={pw} VOL={vera_vol} PAN=LR"
        )
        return idx

    total_frames = sum(s["total_frames"] for s in songs)
    cells = [[HOLD] * total_frames for _ in range(N_CHANNELS)]

    song_start_frames = []
    offset = 0
    for s in songs:
        song_start_frames.append(offset)
        for ch in range(AY_CHANNELS):
            for frame, st in s["per_channel_raw"][ch]:
                global_frame = offset + frame
                if global_frame >= total_frames:
                    continue
                if st is None:
                    cells[ch][global_frame] = NOTE_OFF
                else:
                    waveform, hz, ay_vol4, envelope_mode = st
                    idx = note_index_for_hz(hz)
                    if idx is None:
                        cells[ch][global_frame] = NOTE_OFF
                        continue
                    vol4 = ay_vol4 if not envelope_mode else 12   # fixed approximation
                    instr = instrument_for(waveform, vol4)
                    cells[ch][global_frame] = f"{note_name(idx)}:{instr}"
        offset += s["total_frames"]
    # channel 3 (unused) stays all-hold

    last_start = song_start_frames[-1]
    last_loop = songs[-1]["loop_frame"]
    loop_frame = last_start + last_loop if last_loop is not None else last_start

    # Split into patterns at 255-row boundaries, forcing a boundary at every
    # song join and at the final loop point so LOOP can reference it exactly.
    boundaries = sorted(set([0, loop_frame, total_frames] + song_start_frames))
    pattern_ranges = []
    for i in range(len(boundaries) - 1):
        start, end = boundaries[i], boundaries[i + 1]
        while start < end:
            chunk_end = min(start + MAX_ROWS_PER_PATTERN, end)
            pattern_ranges.append((start, chunk_end))
            start = chunk_end

    lines = []
    lines.append(f"; {' + '.join(source_names)} — converted from VGM "
                 "(AY-3-8910/YM2149) by vgm2vtms.py")
    lines.append("; Review vgm2vtms.py's docstring for what this conversion approximates")
    lines.append("; (no FM, no tone+noise mixing, no real hardware envelope shapes).")
    lines.append("")
    title_text = " + ".join(source_names).replace('"', "'")[:255]
    lines.append(f'TITLE "{title_text}"')
    lines.append("TEMPO 1")
    loop_pattern = 0
    for i, (start, _) in enumerate(pattern_ranges):
        if start == loop_frame:
            loop_pattern = i
            break
    lines.append(f"LOOP {loop_pattern}")
    lines.append("")
    lines.extend(instrument_lines)
    lines.append("")

    for p_idx, (start, end) in enumerate(pattern_ranges):
        lines.append(f"PATTERN {p_idx}")
        for row in range(start, end):
            lines.append("  ".join(cells[ch][row] for ch in range(N_CHANNELS)))
        lines.append("")

    lines.append("ORDER " + " ".join(str(i) for i in range(len(pattern_ranges))))
    lines.append("")

    if len(instruments) > 85:
        print(f"warning: {len(instruments)} instruments generated, "
              f"exceeds the player's ~85 limit (see vtm_format.md)", file=sys.stderr)
    if len(pattern_ranges) > 85:
        print(f"warning: {len(pattern_ranges)} patterns generated, "
              f"exceeds the player's ~85 limit (see vtm_format.md)", file=sys.stderr)

    return "\n".join(lines)


def load_song(in_path, frame_hz):
    """Parse one VGM file into {'per_channel_raw', 'total_frames',
    'loop_frame', 'source_name'}. Raises VgmError on failure."""
    buf = load_vgm_bytes(in_path)
    header = parse_header(buf)
    if header["ay_clock"] == 0:
        raise VgmError("no AY8910/YM2149 clock in header — "
                        "this file doesn't appear to use that chip")

    events, end_sample, loop_sample = parse_commands(
        buf, header["data_offset"], header["loop_offset"]
    )
    if not events:
        raise VgmError("no AY8910 register writes found")

    loop_frame = None
    if loop_sample is not None:
        loop_frame = round(loop_sample * frame_hz / VGM_SAMPLE_RATE)

    per_channel_raw = build_channel_timeline(events, header["ay_clock"], end_sample, frame_hz)
    total_frames = round(end_sample * frame_hz / VGM_SAMPLE_RATE) + 1

    title = None
    gd3_rel = u32(buf, 0x14) if len(buf) >= 0x18 else 0
    if gd3_rel:
        title = parse_gd3(buf, 0x14 + gd3_rel)

    return {
        "per_channel_raw": per_channel_raw,
        "total_frames": total_frames,
        "loop_frame": loop_frame,
        "source_name": title or in_path,
    }


def main(argv):
    flags = {a for a in argv[1:] if a.startswith("--")}
    paths = [a for a in argv[1:] if not a.startswith("--")]
    if len(paths) < 2:
        print(f"usage: {argv[0]} <song.vgm|song.vgz> [more.vgm ...] <out.vtms> [--pal]",
              file=sys.stderr)
        return 1
    in_paths, out_path = paths[:-1], paths[-1]
    frame_hz = FRAME_HZ_PAL if "--pal" in flags else FRAME_HZ_NTSC

    songs = []
    for in_path in in_paths:
        try:
            songs.append(load_song(in_path, frame_hz))
        except VgmError as e:
            print(f"{in_path}: {e}", file=sys.stderr)
            return 1

    vtms_text = build_vtms(songs, [s["source_name"] for s in songs])

    with open(out_path, "w") as f:
        f.write(vtms_text)

    total_frames = sum(s["total_frames"] for s in songs)
    n_patterns = vtms_text.count("PATTERN ")
    print(f"{out_path}: {total_frames} rows across {n_patterns} pattern(s), "
          f"{frame_hz} rows/sec, from {len(songs)} input file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
