#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate build/gen/gen16_voice.h: the host's robot voice (original lines) and offer number words.

Each line is spoken by espeak-ng (build-time only: brew install espeak-ng), then given the 16-bit
treatment: silence trimmed, band-limited to CUTOFF and resampled to RATE, driven, quantised to 8 bits like
a Genesis DAC sample, and stored as 4-bit IMA ADPCM (RATE / 2 bytes per second). Renders are cached
in build/gen/voice-cache by text and settings. Without espeak-ng, missing lines come out empty
and the game simply stays quiet there. With GEN16_VOICE_DATA defined (dsp/gen16.c) the header
carries the data; everyone else sees only the enum.
"""
import argparse, array, hashlib, math, shutil, subprocess, sys, wave
from pathlib import Path

RATE = 11025
CUTOFF = 5000.0                                                 # keeps s / k / ch: 8 kHz lost them
DRIVE = 1.1                                                     # lifts consonants before the 8-bit step
# Klatt formant voice, a touch slow with word gaps: the clearest of the settings measured with a
# speech recogniser after this whole chain (24% word error, against 35%+ for the others)
VOICE = ['-v', 'en-us+klatt', '-s', '125', '-p', '45', '-g', '2']

LINES = {                                                       # all original wording
    'WELCOME': 'Welcome to Deal or No Deal!',
    'PICK': 'Pick your case.',
    'YOURCASE': 'Great choice!',
    'OPEN_1': 'Open one case.', 'OPEN_2': 'Open two cases.', 'OPEN_3': 'Open three cases.',
    'OPEN_4': 'Open four cases.', 'OPEN_5': 'Open five cases.', 'OPEN_6': 'Open six cases.',
    'NICE': 'Nice!', 'GREAT_PICK': 'Great pick!',
    'OUCH': 'Ouch!', 'HURTS': 'Ooh, that hurts.',
    'CALLING': 'The banker is calling.',
    'OFFERS': 'The banker offers',
    'DOND': 'Deal, or no deal?',
    'DEAL': "Deal! Let's open your case.",
    'NODEAL': 'No deal!',
    'TWO_LEFT': 'Two cases left. Keep yours, or swap?',
    'GOOD_DEAL': 'Good deal!', 'BAD_DEAL': 'Oh no. Bad deal.',
    'WELL_CHOSEN': 'You chose well!', 'WRONG_CASE': 'Ooh, wrong case!',
    'YOU_WIN': 'You win',
}
ONES = ['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve',
        'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen']
TENS = ['twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']
WORDS = {**{f'W_{i + 1}': w for i, w in enumerate(ONES)}, **{f'W_{(i + 2) * 10}': w for i, w in enumerate(TENS)},
         'W_HUNDRED': 'hundred', 'W_THOUSAND': 'thousand', 'W_MILLION': 'million', 'W_DOLLARS': 'dollars'}
CLIPS = {**{f'VO_{k}': v for k, v in LINES.items()}, **WORDS}

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
        88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
        3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
        13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def speak(text, espeak, cache):
    """espeak-ng -> 16-bit treatment -> int16 at RATE, cached"""
    key = hashlib.sha256(repr((text, VOICE, RATE, CUTOFF, DRIVE, 3)).encode()).hexdigest()[:16]
    hit = cache / f'{key}.pcm'
    if hit.exists():
        return list(array.array('h', hit.read_bytes()))
    if not espeak:
        return None
    raw = cache / f'{key}.wav'
    subprocess.run([espeak, *VOICE, '-w', str(raw), text], check=True, capture_output=True)
    with wave.open(str(raw)) as w:
        src_rate = w.getframerate()
        x = array.array('h', w.readframes(w.getnframes()))
    if sys.byteorder != 'little':
        x.byteswap()
    x = [v / 32768.0 for v in x]
    peak = max(abs(v) for v in x) or 1.0
    on = [i for i, v in enumerate(x) if abs(v) > peak * 0.02]
    x = x[max(0, on[0] - src_rate // 100):on[-1] + src_rate // 50]
    # windowed-sinc low-pass at CUTOFF, sampled at RATE
    ratio, fc, radius = src_rate / RATE, CUTOFF / src_rate, 12
    out = []
    for j in range(int(len(x) / ratio)):
        pos = j * ratio
        mid, acc = int(pos), 0.0
        for k in range(max(0, mid - radius), min(len(x), mid + radius + 1)):
            d = pos - k
            sinc = 2 * fc if d == 0 else math.sin(2 * math.pi * fc * d) / (math.pi * d)
            acc += x[k] * sinc * (0.5 + 0.5 * math.cos(math.pi * d / (radius + 1)))
        out.append(acc)
    peak = max(abs(v) for v in out) or 1.0
    out = [max(-1.0, min(1.0, DRIVE * v / peak)) for v in out]         # level, then a little drive
    out = [round(v * 127) * 256 for v in out]                           # 8-bit DAC grain
    hit.write_bytes(array.array('h', out).tobytes())
    raw.unlink()
    return out


def adpcm(pcm):
    """IMA ADPCM, low nibble first; predictor and index start at 0 for every clip"""
    pred, idx, out, nib = 0, 0, bytearray(), []
    for s in pcm:
        step, diff, code = STEP[idx], s - pred, 0
        if diff < 0:
            code, diff = 8, -diff
        delta = step >> 3
        for bit, part in ((4, step), (2, step >> 1), (1, step >> 2)):
            if diff >= part:
                code |= bit
                diff -= part
                delta += part
        pred = max(-32768, min(32767, pred - delta if code & 8 else pred + delta))
        idx = max(0, min(88, idx + INDEX[code & 7]))
        nib.append(code)
    nib += [0] * (len(nib) % 2)
    for a, b in zip(nib[::2], nib[1::2]):
        out.append(a | (b << 4))
    return bytes(out)


def generate(cache):
    espeak = shutil.which('espeak-ng')
    cache.mkdir(parents=True, exist_ok=True)
    blob, rows, missing, secs = bytearray(), [], [], 0.0
    for name, text in CLIPS.items():
        pcm = speak(text, espeak, cache)
        if pcm is None:
            missing.append(name)
            rows.append(f'    {{0u, 0u}},   /* {name}: missing */')
            continue
        rows.append(f'    {{{len(blob)}u, {len(pcm)}u}},   /* {name} */')
        blob += adpcm(pcm)
        secs += len(pcm) / RATE
    body = ',\n    '.join(', '.join(str(b) for b in blob[i:i + 32]) for i in range(0, len(blob), 32))
    text = '\n'.join([
        '/* generated by tools/gen16_voice.py: the host\'s robot voice, original lines */', '#pragma once',
        'enum {', *[f'    {n},' for n in CLIPS], '    VO_COUNT', '};', f'#define G16_VOICE_RATE {RATE}',
        '#ifdef GEN16_VOICE_DATA',
        f'static const uint8_t G16_VOICE[{max(1, len(blob))}] = {{\n    {body or "0"}\n}};',
        'static const struct { uint32_t off, n; } G16_CLIPS[VO_COUNT] = {', *rows, '};', '#endif']) + '\n'
    report = [f'voice: {len(CLIPS) - len(missing)} clips, {secs:.1f} s, {len(blob)} B ADPCM at {RATE} Hz']
    if missing:
        report.append(f'voice: espeak-ng not found, {len(missing)} clips empty (brew install espeak-ng)')
    return text, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path)
    a = ap.parse_args()
    text, report = generate(a.out.parent / 'voice-cache')
    if not a.out.exists() or a.out.read_text() != text:
        a.out.write_text(text)
    print('\n'.join(report))


if __name__ == '__main__':
    main()
