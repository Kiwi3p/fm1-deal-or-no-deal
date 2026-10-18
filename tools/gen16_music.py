#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate build/gen/gen16_music.h: the DOND 16-bit soundtrack (original compositions) and patches.

Eight channels, Genesis-style: 0..3 FM (4-operator), 4..6 square (PSG), 7 noise. Songs are
written below as tracker rows, one token per 16th note per channel:
  C4 / F#3 / Bb2   note on        .   hold        -   note off
  NOTE:inst        note on with a different instrument (stays for later notes)
The header has the song enum for everyone; with GEN16_MUSIC_DATA defined (dsp/gen16.c only)
it also carries the note table, patches and song streams. Envelope rates are precomputed for
RATE so the audio callback never needs exp or pow.

Stream bytes per channel: 0 hold, 1 off, 2..129 note + 2, 130 + i select instrument i.
"""
import argparse, math
from pathlib import Path

RATE = 22050
NOTES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

# ---------------------------------------------------------------- instruments
# FM: algorithm (0 serial, 4 two pairs, 5 one-to-three, 7 additive), op1 feedback, and per operator
# (frequency multiple, level, attack ms, decay ms, sustain 0..1, release ms). In a pair the first
# operator is the modulator (level = index in radians), the second the carrier (level = loudness).
FM = {
    'brass':  (4, 0.3, [(1, 1.4, 20, 400, 0.6, 90), (1, 1.0, 15, 500, 0.75, 90),
                        (1.003, 1.1, 25, 400, 0.6, 90), (1, 0.7, 20, 500, 0.75, 90)]),
    'epiano': (4, 0.0, [(14, 0.5, 1, 150, 0.0, 100), (1, 1.0, 1, 1500, 0.25, 250),
                        (1, 0.7, 1, 800, 0.1, 200), (1, 0.6, 1, 1500, 0.25, 250)]),
    'bass':   (4, 0.4, [(1, 2.0, 1, 180, 0.35, 60), (1, 1.0, 1, 600, 0.6, 60),
                        (1, 0.0, 1, 1, 0.0, 1), (1, 0.0, 1, 1, 0.0, 1)]),
    'pad':    (4, 0.0, [(2, 0.5, 300, 1000, 0.6, 700), (1, 0.7, 350, 1000, 0.85, 700),
                        (1.005, 0.4, 300, 1000, 0.6, 700), (1, 0.6, 400, 1000, 0.85, 700)]),
    'bell':   (4, 0.0, [(3.5, 1.8, 1, 700, 0.0, 300), (1, 0.9, 1, 1200, 0.0, 400),
                        (7, 0.8, 1, 300, 0.0, 200), (2, 0.4, 1, 900, 0.0, 300)]),
}
# PSG: kind (0 square, 1 noise, 2 square with falling pitch), volume, decay ms,
# pitch fall (semitones over the decay, kind 2), arpeggio (semitones, ms per step; 0 = none)
PSG = {
    'sq':    (0, 0.30, 400, 0, 0, 0),
    'arp':   (0, 0.16, 140, 0, 0, 0),
    'tick':  (0, 0.18, 40, 0, 0, 0),
    'ring':  (0, 0.26, 6000, 0, 5, 25),
    'kick':  (2, 0.60, 120, 24, 0, 0),
    'hat':   (1, 0.16, 35, 0, 0, 0),
    'snare': (1, 0.30, 140, 0, 0, 0),
}


def bars(*b):
    """join bars of 16 tokens each, checking the count"""
    out = []
    for i, bar in enumerate(b):
        t = bar.split()
        if len(t) != 16:
            raise SystemExit(f'gen16 music: a bar has {len(t)} tokens, not 16: {bar!r}')
        out += t
    return out


def rest(n):
    return ['.'] * n


def stretch(tokens, n):
    """pad a stinger channel with holds to n rows"""
    return tokens + ['.'] * (n - len(tokens))


H, S, K = 'C6:hat', 'C5:snare', 'C3'
DRUM_BEAT = 'C6:hat . C6:hat . C5:snare . C6:hat . C6:hat . C6:hat . C5:snare . C6:hat .'

# ---------------------------------------------------------------- songs (all original)
# name: (bpm, loop, {channel: (instrument, tokens)})
SONGS = {
    # title: bright game-show fanfare, C - Am - F - G
    'THEME': (132, True, {
        0: ('brass', bars('G4 . C5 . E5 . G5 . . . E5 . C5 . E5 .',
                          'A5 . . . G5 . E5 . C5 . . . A4 . C5 .',
                          'F5 . A5 . C6 . . . A5 . F5 . A5 . G5 .',
                          'F5 . E5 . D5 . . . G5 . . . - . . .')),
        1: ('epiano', bars('. . E4 - . . E4 - . . E4 - . . E4 -', '. . E4 - . . E4 - . . E4 - . . E4 -',
                           '. . F4 - . . F4 - . . F4 - . . F4 -', '. . D4 - . . D4 - . . D4 - . . D4 -')),
        2: ('epiano', bars('. . G4 - . . G4 - . . G4 - . . G4 -', '. . A4 - . . A4 - . . A4 - . . A4 -',
                           '. . A4 - . . A4 - . . A4 - . . A4 -', '. . G4 - . . G4 - . . G4 - . . G4 -')),
        3: ('bass', bars('C2 . C3 . C2 . C3 . C2 . C3 . G2 . B2 .', 'A1 . A2 . A1 . A2 . A1 . A2 . E2 . G2 .',
                         'F1 . F2 . F1 . F2 . F1 . F2 . C2 . E2 .', 'G1 . G2 . G1 . G2 . G1 . G2 . D2 . F2 .')),
        4: ('arp', bars('C6 E6 G6 E6 C6 E6 G6 E6 C6 E6 G6 E6 C6 E6 G6 E6',
                        'A5 C6 E6 C6 A5 C6 E6 C6 A5 C6 E6 C6 A5 C6 E6 C6',
                        'F5 A5 C6 A5 F5 A5 C6 A5 F5 A5 C6 A5 F5 A5 C6 A5',
                        'G5 B5 D6 B5 G5 B5 D6 B5 G5 B5 D6 B5 G5 B5 D6 B5')),
        6: ('kick', bars(*['C3 . . . . . . . C3 . . . . . . .'] * 4)),
        7: ('hat', bars(*[DRUM_BEAT] * 4)),
    }),
    # picking a case: mellow loop, Fmaj7 - Em7 - Dm7 - Cmaj7
    'BED': (100, True, {
        1: ('pad', bars('A4 . . . . . . . . . . . . . . .', 'G4 . . . . . . . . . . . . . . .',
                        'F4 . . . . . . . . . . . . . . .', 'E4 . . . . . . . . . . . . . . .')),
        2: ('pad', bars('E5 . . . . . . . . . . . . . . .', 'D5 . . . . . . . . . . . . . . .',
                        'C5 . . . . . . . . . . . . . . .', 'B4 . . . . . . . . . . . . . . .')),
        3: ('bass', bars('F2 . . . C3 . . . F2 . . . A2 . C3 .', 'E2 . . . B2 . . . E2 . . . G2 . B2 .',
                         'D2 . . . A2 . . . D2 . . . F2 . A2 .', 'C2 . . . G2 . . . C2 . . . E2 . G2 .')),
        4: ('arp', bars('F5 . A5 . C6 . E6 . C6 . A5 . F5 . A5 .', 'E5 . G5 . B5 . D6 . B5 . G5 . E5 . G5 .',
                        'D5 . F5 . A5 . C6 . A5 . F5 . D5 . F5 .', 'C5 . E5 . G5 . B5 . G5 . E5 . C5 . E5 .')),
        6: ('kick', bars(*['C3 . . . . . . . C3 . . . . . . .'] * 4)),
        7: ('hat', bars(*['. . C6 . . . C6 . . . C6 . . . C6 .'] * 4)),
    }),
    # opening cases: driving D minor pulse, Dm - Dm - Bb - A
    'TENSION': (116, True, {
        1: ('pad', bars('A4 . . . . . . . . . . . . . . .', '. . . . . . . . . . . . . . . .',
                        'Bb4 . . . . . . . . . . . . . . .', 'C#5 . . . . . . . . . . . . . . .')),
        2: ('pad', bars('F4 . . . . . . . . . . . . . . .', '. . . . . . . . . . . . . . . .',
                        'D4 . . . . . . . . . . . . . . .', 'E4 . . . . . . . . . . . . . . .')),
        3: ('bass', bars('D2 . D3 . D2 . D3 . D2 . D3 . D2 . D3 .', 'D2 . D3 . D2 . D3 . D2 . D3 . D2 . D3 .',
                         'Bb1 . Bb2 . Bb1 . Bb2 . Bb1 . Bb2 . Bb1 . Bb2 .', 'A1 . A2 . A1 . A2 . A1 . A2 . A1 . A2 .')),
        4: ('tick', bars(*['D6 . . . D6 . . . D6 . . . D6 . . .'] * 4)),
        6: ('kick', bars(*['C3 . . . C3 . . . C3 . . . C3 . . .'] * 4)),
        7: ('hat', bars(*['. . C6 . . . C6 . . . C6 . . . C6 C6'] * 4)),
    }),
    # the banker and the final choice: slow suspense with a heartbeat, Am - F - Dm - E
    'DECISION': (80, True, {
        1: ('pad', bars('E4 . . . . . . . . . . . . . . .', 'F4 . . . . . . . . . . . . . . .',
                        'F4 . . . . . . . . . . . . . . .', 'E4 . . . . . . . . . . . . . . .')),
        2: ('pad', bars('C5 . . . . . . . . . . . . . . .', 'C5 . . . . . . . . . . . . . . .',
                        'D5 . . . . . . . . . . . . . . .', 'G#4 . . . . . . . . . . . . . . .')),
        3: ('bass', bars('A1 . . . . . . . . . . . . . . .', 'F1 . . . . . . . . . . . . . . .',
                         'D2 . . . . . . . . . . . . . . .', 'E2 . . . . . . . . . . . . . . .')),
        6: ('kick', bars(*['C3 . C3 . . . . . . . . . . . . .'] * 4)),
    }),
    # stings: one shot each, then the bed comes back
    'PHONE': (150, False, {                      # two trilling rings, then the offer chord
        5: ('ring', stretch(['E6'] + rest(7) + ['-'] + rest(3) + ['E6'] + rest(7) + ['-'] + rest(3), 36)),
        0: ('brass', stretch(rest(24) + ['D5'] + rest(9) + ['-'], 36)),
        1: ('brass', stretch(rest(24) + ['A4'] + rest(9) + ['-'], 36)),
        2: ('brass', stretch(rest(24) + ['F4'] + rest(9) + ['-'], 36)),
        3: ('bass', stretch(rest(24) + ['D2'] + rest(9) + ['-'], 36)),
        6: ('kick', stretch(rest(24) + ['C3'], 36)),
    }),
    'DEAL': (140, False, {                       # bright major fanfare
        0: ('brass', bars('C5 . E5 . G5 . C6 . . . . . . . . -')),
        1: ('brass', bars('E4 . G4 . C5 . E5 . . . . . . . . -')),
        2: ('epiano', bars('G4 . C5 . E5 . G5 . . . . . . . . -')),
        3: ('bass', bars('C2 . E2 . G2 . C3 . . . . . . . . -')),
        4: ('arp', bars('C6 E6 G6 C7 G6 E6 C6 E6 G6 C7 . . . . . .')),
        6: ('kick', bars('C3 . . . . . C3 . . . . . . . . .')),
        7: ('snare', bars('. . . . . . C5 . C5 C5 . . . . . .')),
    }),
    'NODEAL': (120, False, {                     # punchy minor: hit, hit, held
        0: ('brass', bars('E5 . - E5 . - D#5 . . . . . . . . -')),
        1: ('brass', bars('C5 . - C5 . - B4 . . . . . . . . -')),
        2: ('brass', bars('A4 . - A4 . - F#4 . . . . . . . . -')),
        3: ('bass', bars('A1 . - A1 . - B1 . . . . . . . . -')),
        6: ('kick', bars('C3 . . C3 . . C3 . . . . . . . . .')),
        7: ('snare', bars('C5 . . C5 . . C5 . . . . . . . . .')),
    }),
    'GOOD': (150, False, {                       # a small amount gone: rising chime
        0: ('bell', stretch(['C6', 'E6', 'G6', 'C7'] + rest(3) + ['-'], 8)),
        4: ('arp', stretch(rest(4) + ['G6', 'C7'], 8)),
    }),
    'OUCH': (110, False, {                       # a big amount gone: low sinking hits
        0: ('brass', stretch(['G3', '.', '.', 'F#3', '.', '.', 'F3'] + rest(4) + ['-'], 12)),
        3: ('bass', stretch(['G1', '.', '.', 'F#1', '.', '.', 'F1'] + rest(4) + ['-'], 12)),
        6: ('kick', stretch(['C3', '.', '.', 'C3', '.', '.', 'C3'], 12)),
    }),
    'WIN': (144, False, {                        # a good result: celebration
        0: ('brass', bars('C5 . E5 . G5 . E5 . G5 . C6 . . . . .', 'A5 . . . F5 . A5 . G5 . . . . . . -')),
        1: ('epiano', bars('. . E4 - . . E4 - . . G4 - . . G4 -', '. . F4 - . . F4 - . . E4 . . . . -')),
        2: ('epiano', bars('. . G4 - . . G4 - . . C5 - . . C5 -', '. . A4 - . . A4 - . . G4 . . . . -')),
        3: ('bass', bars('C2 . C3 . C2 . C3 . E2 . E3 . E2 . E3 .', 'F2 . F3 . F2 . F3 . C2 . . . . . . -')),
        4: ('arp', bars('C6 E6 G6 E6 C6 E6 G6 E6 C6 E6 G6 E6 C6 E6 G6 E6',
                        'F6 A6 C7 A6 F6 A6 C7 A6 G6 C7 E7 . . . . .')),
        6: ('kick', bars('C3 . . . C3 . . . C3 . . . C3 . . .', 'C3 . . . C3 . . . C3 . . . . . . .')),
        7: ('hat', bars(DRUM_BEAT, DRUM_BEAT)),
    }),
    'LOSE': (100, False, {                       # a bad result: four sinking notes
        0: ('brass', stretch(['G4'] + rest(3) + ['F#4'] + rest(3) + ['F4'] + rest(3) + ['E4'] + rest(7) + ['-'], 20)),
        3: ('bass', stretch(['C2'] + rest(3) + ['B1'] + rest(3) + ['Bb1'] + rest(3) + ['A1'] + rest(7) + ['-'], 20)),
    }),
}


# ---------------------------------------------------------------- encoding
def midi(tok):
    name = tok[0]
    if name not in NOTES:
        raise SystemExit(f'gen16 music: bad note {tok!r}')
    i, n = 1, NOTES[name]
    if i < len(tok) and tok[i] in '#b':
        n += 1 if tok[i] == '#' else -1
        i += 1
    return 12 * (int(tok[i:]) + 1) + n


def stream(tokens, inst, names):
    out = bytearray([130 + names.index(inst)])
    for tok in tokens:
        if tok == '.':
            out.append(0)
        elif tok == '-':
            out.append(1)
        else:
            if ':' in tok:
                tok, inst = tok.split(':')
                out.append(130 + names.index(inst))
            out.append(2 + midi(tok))
    return bytes(out)


def coef(ms):
    """per-sample multiplier that falls about 43 dB over ms"""
    return math.exp(-5.0 / max(1.0, ms * RATE / 1000.0))


def flt(x):
    t = f'{float(x):.9g}'
    return (t if any(c in t for c in '.e') else t + '.0') + 'f'


def generate():
    fm_names, psg_names = list(FM), list(PSG)
    data = ['/* phase increments per MIDI note at the synth rate (2^32 per cycle) */',
            'static const uint32_t G16_INC[128] = {' + ', '.join(
                f'{min(0xFFFFFFFF, round(440 * 2 ** ((n - 69) / 12) * 2 ** 32 / RATE))}u' for n in range(128)) + '};',
            'static const g16_fm_t G16_FM[] = {']
    for n in fm_names:
        alg, fb, ops = FM[n]
        o = ', '.join(f'{{{flt(m)}, {flt(lv)}, {flt(1.0 / max(1.0, a * RATE / 1000))}, {flt(coef(d))}, '
                      f'{flt(s)}, {flt(coef(r))}}}' for m, lv, a, d, s, r in ops)
        data.append(f'    {{{alg}, {flt(fb)}, {{{o}}}}},   /* {n} */')
    data += ['};', 'static const g16_psg_t G16_PSG[] = {']
    for n in psg_names:
        kind, vol, dec, fall, arp, arp_ms = PSG[n]
        sweep = 2 ** (-fall / 12 / max(1.0, dec * RATE / 1000)) if kind == 2 else 1.0
        data.append(f'    {{{kind}, {flt(vol)}, {flt(coef(dec))}, {flt(sweep)}, {flt(2 ** (arp / 12))}, '
                    f'{round(arp_ms * RATE / 1000)}u}},   /* {n} */')
    data.append('};')
    table, sizes = [], []
    for name, (bpm, loop, chans) in SONGS.items():
        rows = {len(t) for _, t in chans.values()}
        if len(rows) != 1:
            raise SystemExit(f'gen16 music: {name}: channels differ in length {sorted(rows)}')
        rows = rows.pop()
        ptrs, size = [], 0
        for ch in range(8):
            if ch not in chans:
                ptrs.append('0')
                continue
            inst, toks = chans[ch]
            names = fm_names if ch < 4 else psg_names
            b = stream(toks, inst, names)
            data.append(f'static const uint8_t g16_{name.lower()}_{ch}[{len(b)}] = {{' + ', '.join(map(str, b)) + '};')
            ptrs.append(f'g16_{name.lower()}_{ch}')
            size += len(b)
        row = round(RATE * 60 / (bpm * 4))
        table.append(f'    {{{row}u, {rows}u, {int(loop)}, {{{", ".join(ptrs)}}}}},   /* {name} */')
        sizes.append(f'{name}: {rows} rows at {bpm} BPM, {size} B')
    data += ['static const g16_song_t G16_SONGS[G16_NSONGS] = {', *table, '};']
    head = ['/* generated by tools/gen16_music.py: original 16-bit soundtrack */', '#pragma once',
            'enum {', *[f'    SONG_{n},' for n in SONGS], '    G16_NSONGS', '};',
            f'#define G16_RATE {RATE}', '#ifdef GEN16_MUSIC_DATA', *data, '#endif']
    return '\n'.join(head) + '\n', sizes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path)
    a = ap.parse_args()
    text, report = generate()
    if not a.out.exists() or a.out.read_text() != text:
        a.out.write_text(text)
    print('\n'.join(report))


if __name__ == '__main__':
    main()
