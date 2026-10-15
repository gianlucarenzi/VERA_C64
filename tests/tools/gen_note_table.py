#!/usr/bin/env python3
"""gen_note_table.py — emit vtm_notes.inc, the 96-entry VERA PSG frequency-word
table used by vtm_player.s.

VERA PSG frequency word: output_frequency = (25MHz/512) / 2^17 * word, so
    word = round(hz * 2^17 / (25000000/512))

Note index 0 = C-0 .. 95 = B-7, equal temperament, A-4 = 440 Hz (matches the
worked example in the VERA Programmer's Reference: A4 -> word 1181).

Run once; the output is committed to the repo, it never needs to change.
"""
SAMPLE_RATE = 25_000_000 / 512.0  # 48828.125 Hz
NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]


def freq_word(hz):
    return max(0, min(65535, round(hz * (2 ** 17) / SAMPLE_RATE)))


def note_hz(idx):
    return 440.0 * (2 ** ((idx - 57) / 12.0))


def main():
    lines = [
        "; vtm_notes.inc — VERA PSG frequency-word table (generated, do not edit by hand).",
        "; Regenerate with: python3 tools/gen_note_table.py > vtm_notes.inc",
        "; Index 0 = C-0 ... 95 = B-7. One .word (freq_lo, freq_hi) per note.",
        "",
        "vtm_note_freq:",
    ]
    for idx in range(96):
        w = freq_word(note_hz(idx))
        name = NOTE_NAMES[idx % 12] + str(idx // 12)
        lines.append(f"    .word ${w:04X}                       ; {idx:2d} {name}")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
