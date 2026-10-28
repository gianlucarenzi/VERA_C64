#!/usr/bin/env python3
"""pacman_snd.py - the sounds of the Pac-Man arcade, ripped by pacman_rip.lua
in MAME, as SID data for test_pacman.c.

    python3 pacman_snd.py pacman_rip.log pacman_scenes.log gen/pacsnd.s gen/pacsnd.h

The intermission song comes from the log of the scenes (pacman_scenes.lua):
one period of it, from the start of intermission 1; the scenes start it again.

Every clip is the frame by frame state of one voice of the Namco WSG
(frequency, volume, waveform), 60 frames per second as on the arcade.  A frame
is 3 bytes: SID frequency low, high, and control: waveform bits (high nibble,
$10 triangle, $20 sawtooth) | volume (low nibble, 0 = silent).  The player
sets the volume as the sustain level of the envelope.

The WSG plays 32 4-bit samples per period at f = reg * 96000 / 2^20 Hz.  The
8 waveforms of the PROM 82s126.1m are played by the SID waveform closest in
harmonics, some an octave or more up where the PROM wave repeats inside its 32
samples:

    0  sine                      triangle
    1  harmonics 1, 2, 4         sawtooth
    2  mostly harmonic 2         sawtooth x2
    3  harmonics 1, 4, 8         sawtooth
    4  mostly harmonic 8         triangle x8
    5  harmonic 16, AM           triangle x16
    6  triangle                  triangle
    7  two ramps                 sawtooth x2

The continuous sounds (sirens, frightened ghosts, eyes) loop: one period,
found after the start transient, is kept.  The tuning of the arcade (about a
quarter tone below A = 440 Hz) is kept as it is.
"""

import sys

SID_CLOCK = 985248          # PAL C64
WSG_RATE = 96000            # 3.072 MHz / 32
TRI, SAW = 0x10, 0x20
WAVES = {0: (TRI, 1), 1: (SAW, 1), 2: (SAW, 2), 3: (SAW, 1),
         4: (TRI, 8), 5: (TRI, 16), 6: (TRI, 1), 7: (SAW, 2)}

# request bytes in the log: $4E9C $4EAC $4EBC $4ECC $4EDC $4EEC
REQ_V0, REQ_V1, REQ_V2, SONG_V0, SONG_V1 = range(5)
SONG_PERIOD = 320           # frames of the intermission song (as pacman_scene.py)


class Rip:
    def __init__(self, path, marks):
        self.frames = {}
        self.marks = {}
        for line in open(path):
            p = line.split()
            if p[0] == 'S':                 # pacman_scenes.lua: sprites and states first
                p = p[:2] + p[7:]
            if p[0] == 'M':
                self.marks.setdefault(p[2], int(p[1]))
            elif p[0] in 'FS':
                n = [int(c, 16) for c in p[2]]
                f = int(p[1])
                v0 = n[0] | n[1] << 4 | n[2] << 8 | n[3] << 12 | n[4] << 16
                v1 = n[6] << 4 | n[7] << 8 | n[8] << 12 | n[9] << 16
                v2 = n[11] << 4 | n[12] << 8 | n[13] << 12 | n[14] << 16
                self.frames[f] = ([(v0, n[5], int(p[3][0])),
                                   (v1, n[10], int(p[3][1])),
                                   (v2, n[15], int(p[3][2]))],
                                  [int(x, 16) for x in p[4:10]])
        for name in marks:
            if name not in self.marks:
                sys.exit('pacman_snd.py: no marker "%s" in %s' % (name, path))

    def voice(self, f, v):
        freq, vol, wave = self.frames[f][0][v]
        return (freq, vol if freq else 0, wave)

    def req(self, f, r):
        return self.frames[f][1][r]

    def check(self, f, r, mask, what):
        if not self.req(f, r) & mask:
            sys.exit('pacman_snd.py: frame %d: request $%02x, not %s: '
                     'the capture went out of step' % (f, self.req(f, r), what))

    def one_shot(self, f, v, maxlen=200):
        """From frame f until the voice is silent again after sounding."""
        s, sounded = [], False
        for t in range(f, f + maxlen):
            x = self.voice(t, v)
            if x[1]:
                sounded = True
            elif sounded:
                break
            if sounded:
                s.append(x)
        return s

    def loop(self, f, v, length, skip=40):
        """One period of a continuous sound, after the start transient."""
        s = [self.voice(t, v) for t in range(f + skip, f + length)]
        for p in range(1, len(s) // 2):
            if all(s[i] == s[i + p] for i in range(len(s) - p)):
                return s[:p]
        sys.exit('pacman_snd.py: no period at frame %d' % f)


def sid_frame(x):
    freq, vol, wave = x
    if not vol:
        return (0, 0, 0)
    ctrl, mult = WAVES[wave]
    hz = freq * WSG_RATE / 2 ** 20 * mult
    reg = round(hz * 2 ** 24 / SID_CLOCK)
    while reg > 0xFFFF:
        reg //= 2
    return (reg & 0xFF, reg >> 8, ctrl | vol)


def main():
    if len(sys.argv) != 5:
        sys.exit('usage: pacman_snd.py RIP.log SCENES.log OUT.s OUT.h')
    rip = Rip(sys.argv[1], ('intro', 'dot1', 'dot2', 'fruit', 'ghost', 'xlife',
                            'siren2', 'siren3', 'siren4', 'siren5', 'fright', 'eyes'))
    mk = rip.marks
    clips = []      # (name, frames, loops)

    # intro: the song on voices 0 and 1, until both songs end
    t, intro0, intro1 = mk['intro'], [], []
    while rip.req(t, SONG_V0) or rip.req(t, SONG_V1):
        intro0.append(rip.voice(t, 0))
        intro1.append(rip.voice(t, 1))
        t += 1
    clips += [('INTRO0', intro0, 0), ('INTRO1', intro1, 0)]

    for name, bit in (('dot1', 1), ('dot2', 2), ('fruit', 4), ('ghost', 8)):
        rip.check(mk[name], REQ_V2, bit, name)
        clips.append((name.upper(), rip.one_shot(mk[name], 2), 0))
    rip.check(mk['xlife'], REQ_V0, 1, 'xlife')
    clips.append(('XLIFE', rip.one_shot(mk['xlife'], 0), 0))

    # death: the game plays $10, then $20 twice; it is the first request
    # $10 on voice 2 after the intro, until the voice is silent for good
    t = mk['intro']
    while rip.req(t, REQ_V2) != 0x10:
        t += 1
    end = t
    while rip.req(end, REQ_V2) in (0x10, 0x20) or rip.req(end + 1, REQ_V2) == 0x20:
        end += 1
    clips.append(('DEATH', [rip.voice(f, 2) for f in range(t, end)], 0))

    # siren 1: the game plays it while Pac-Man stands, after the extra life
    t = mk['xlife'] + 200
    rip.check(t, REQ_V1, 1, 'siren1')
    clips.append(('SIREN1', rip.loop(t, 1, 180), 1))
    for k in range(2, 6):
        name = 'siren%d' % k
        rip.check(mk[name] + 2, REQ_V1, 1 << (k - 1), name)
        clips.append((name.upper(), rip.loop(mk[name], 1, 140), 1))
    rip.check(mk['fright'] + 2, REQ_V1, 0x20, 'fright')
    clips.append(('FRIGHT', rip.loop(mk['fright'], 1, 220), 1))
    rip.check(mk['eyes'] + 2, REQ_V1, 0x40, 'eyes')
    clips.append(('EYES', rip.loop(mk['eyes'], 1, 300), 1))

    # the intermission song: request $02 on voices 0 and 1, one period
    scn = Rip(sys.argv[2], ('act1',))
    t = scn.marks['act1']
    scn.check(t, SONG_V0, 2, 'the intermission song')
    for v in (0, 1):
        song = [scn.voice(f, v) for f in range(t, t + 2 * SONG_PERIOD - 8)]
        if song[:SONG_PERIOD - 8] != song[SONG_PERIOD:]:
            sys.exit('pacman_snd.py: the intermission song does not repeat after %d frames' % SONG_PERIOD)
        clips.append(('INTER%d' % v, song[:SONG_PERIOD], 0))

    with open(sys.argv[3], 'w') as s:
        s.write('; pacsnd.s - generated by pacman_snd.py from the Pac-Man arcade\n'
                '; sounds ripped in MAME (pacman_rip.lua): do not edit.\n'
                '; 3 bytes per frame: SID frequency lo, hi, waveform | volume\n\n'
                '.export _snd_ptr_lo, _snd_ptr_hi, _snd_len, _snd_len_hi, _snd_loop\n'
                '.rodata\n\n')
        for name, frames, _ in clips:
            s.write('snd_%s:\n' % name.lower())
            data = [b for x in frames for b in sid_frame(x)]
            for i in range(0, len(data), 24):
                s.write('        .byte ' + ','.join('$%02x' % b for b in data[i:i + 24]) + '\n')
        s.write('\n_snd_ptr_lo:  .byte ' + ','.join('<snd_%s' % c[0].lower() for c in clips))
        s.write('\n_snd_ptr_hi:  .byte ' + ','.join('>snd_%s' % c[0].lower() for c in clips))
        s.write('\n_snd_len:     .byte ' + ','.join('<%d' % len(c[1]) for c in clips))
        s.write('\n_snd_len_hi:  .byte ' + ','.join('>%d' % len(c[1]) for c in clips))
        s.write('\n_snd_loop:    .byte ' + ','.join('%d' % c[2] for c in clips) + '\n')

    with open(sys.argv[4], 'w') as h:
        h.write('/* pacsnd.h - generated by pacman_snd.py: do not edit. */\n'
                '#ifndef PACSND_H\n#define PACSND_H\n\n')
        for i, (name, frames, loops) in enumerate(clips):
            h.write('#define SND_%-8s %2d   /* %3d frames%s */\n'
                    % (name, i, len(frames), ', loops' if loops else ''))
        h.write('#define SND_COUNT    %2d\n\n' % len(clips))
        h.write('extern const unsigned char snd_ptr_lo[], snd_ptr_hi[];\n'
                'extern const unsigned char snd_len[], snd_len_hi[], snd_loop[];\n\n#endif\n')

    for name, frames, loops in clips:
        print('%-7s %3d frames%s' % (name, len(frames), ', loops' if loops else ''))


if __name__ == '__main__':
    main()
