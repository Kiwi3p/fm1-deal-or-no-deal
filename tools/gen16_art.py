#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate build/gen/gen16_art.h: the 16-bit DOND scenes and host, drawn here from scratch.

All original art. Genesis rules, checked: every colour sits on the 9-bit grid (3 bits per
channel), a scene plus the host uses at most 61 colours on screen, the host at most 15 + clear.
Gradients use a 4x4 ordered dither. Same table format as before (dond_art_types.h): 8-bit
palette indices, RLE, palettes pre-swapped RGB565; host = base frame + same-size mouth patches.

  python3 tools/gen16_art.py build/gen/gen16_art.h [--preview DIR]   (--preview writes PNGs; needs Pillow)
"""
import argparse
from pathlib import Path

LEVEL = [0, 36, 73, 109, 146, 182, 219, 255]
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def g(r, gg, b):
    """a Genesis colour from 0..7 per channel"""
    return (LEVEL[r], LEVEL[gg], LEVEL[b])


class Img:
    def __init__(self, w, h, fill=None):
        self.w, self.h, self.px = w, h, [fill] * (w * h)

    def get(self, x, y):
        return self.px[y * self.w + x] if 0 <= x < self.w and 0 <= y < self.h else None

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for yy in range(int(cy - ry), int(cy + ry) + 1):
            for xx in range(int(cx - rx), int(cx + rx) + 1):
                if ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1.0:
                    self.set(xx, yy, c)

    def poly(self, pts, c):
        ys = [p[1] for p in pts]
        for yy in range(int(min(ys)), int(max(ys)) + 1):
            y = yy + 0.5
            xs = []
            for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for xx in range(round(a), round(b)):
                    self.set(xx, yy, c)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.set(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)

    def ramp(self, x0, y0, w, h, colours, horizontal=False):
        """dithered gradient through colours, top to bottom (or left to right)"""
        n = len(colours) - 1
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                t = (x - x0) / max(1, w - 1) if horizontal else (y - y0) / max(1, h - 1)
                pos = t * n
                i = min(int(pos), n - 1) if n else 0
                f = pos - i
                self.set(x, y, colours[i + 1] if n and f * 16 > BAYER[y % 4][x % 4] + 0.5 else colours[i])

    def shade(self, x0, y0, w, h, c, mask_colour=None):
        """50% checker of c over a region (light beams, glass), only on mask_colour if given"""
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                if (x + y) % 2 == 0 and (mask_colour is None or self.get(x, y) == mask_colour):
                    self.set(x, y, c)

    def blit(self, img, x, y):
        for yy in range(img.h):
            for xx in range(img.w):
                c = img.px[yy * img.w + xx]
                if c is not None:
                    self.set(x + xx, y + yy, c)

    def outline(self, c):
        edge = []
        for y in range(self.h):
            for x in range(self.w):
                if self.get(x, y) is None and any(self.get(x + dx, y + dy) not in (None, c)
                                                  for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    edge.append((x, y))
        for x, y in edge:
            self.set(x, y, c)

    def colours(self):
        return {c for c in self.px if c is not None}


# ------------------------------------------------------------------ the host (original character)
INK = g(0, 0, 1)
HOST = {
    '.': None, 'K': INK,
    'L': g(7, 5, 4), 'S': g(6, 4, 3), 'D': g(4, 2, 2), 'H': g(7, 7, 6),    # skin light/mid/dark, shine
    'G': g(2, 1, 1),                                                          # brows and goatee
    'W': g(7, 7, 7), 'M': g(3, 0, 1),                                        # teeth / whites, mouth
}
SUIT_L, SUIT_M, SUIT_D = g(2, 3, 6), g(1, 2, 5), g(1, 1, 3)
SHIRT, SHIRT_S, TIE_L, TIE_D, RED = g(7, 7, 7), g(5, 5, 6), g(7, 6, 1), g(5, 4, 0), g(6, 1, 1)

# 16 x 18 head: bald dome with a shine, bushy brows, big grin, short pointed goatee
HEAD = [
    '....KKKKKKKK....',
    '..KKLLLHHLLLKK..',
    '.KLLLLHHHLLLLSK.',
    '.KLLLLLHLLLLLSK.',
    '.KLLLLLLLLLLSSK.',
    '.KLGGGGLLGGGGSK.',
    '.KLLLLLLLLLLLSK.',
    'KKLWKLLLLLWKLSKK',
    'KDKLLLLDDLLLLKDK',
    'KDKLLLLLDLLLSKDK',
    '.KKLLLLDDLLLSKK.',
    '..KLMMMMMMMMSK..',
    '..KLMWWWWWWMSK..',
    '..KSLMMMMMMLSK..',
    '...KSLLGGLLSK...',
    '....KSSGGSSK....',
    '.....KKGGKK.....',
    '.......KK.......',
]
# mouth rows 11..13: closed, mid, wide, "O" (talk frames cycle 1..3)
MOUTHS = [
    ['..KLLLLLLLLLSK..', '..KLLMMMMMLLSK..', '..KSLLLLLLLLSK..'],
    ['..KLLMMMMMMLSK..', '..KLMWWWWWWMSK..', '..KSLLMMMMLLSK..'],
    ['..KLMMMMMMMMSK..', '..KLMWWWWWWMSK..', '..KSLMMMMMMLSK..'],
    ['..KLLLMMMLLLSK..', '..KLLMMMMMLLSK..', '..KSLLLMMLLLSK..'],
]


def host_frame(mouth):
    rows = HEAD[:11] + MOUTHS[mouth] + HEAD[14:]
    assert all(len(r) == 16 for r in rows) and len(rows) == 18
    img = Img(32, 64)
    # body first, head drawn over it
    img.rect(14, 16, 4, 4, HOST['S'])                                   # neck
    img.poly([(4, 20), (27, 20), (25, 45), (6, 45)], SUIT_M)            # double-breasted jacket
    img.ellipse(5, 23, 3, 4, SUIT_M)
    img.ellipse(26, 23, 3, 4, SUIT_M)
    for y in range(19, 46):                                              # light from the left
        for x in range(0, 32):
            if img.get(x, y) == SUIT_M:
                img.set(x, y, SUIT_L if x < 10 else SUIT_D if x > 22 else SUIT_M)
    img.poly([(12, 19), (20, 19), (16, 31)], SHIRT)                      # shirt V
    img.line(12, 20, 15, 31, SHIRT_S)
    img.poly([(15, 21), (17, 21), (18, 28), (16, 31), (14, 28)], TIE_L)  # gold tie
    img.line(17, 22, 17, 28, TIE_D)
    img.rect(14, 19, 4, 2, TIE_D)
    img.line(12, 19, 15, 33, SUIT_D)                                     # lapels
    img.line(20, 19, 17, 33, SUIT_D)
    for bx, by in ((12, 35), (19, 35), (12, 40), (19, 40)):              # buttons
        img.rect(bx, by, 2, 2, TIE_L)
    img.poly([(21, 26), (25, 26), (23, 24)], RED)                        # pocket square
    for x0 in (1, 26):                                                   # arms at the sides
        img.rect(x0, 22, 5, 21, SUIT_L if x0 == 1 else SUIT_D)
        img.ellipse(x0 + 2, 44, 2.5, 2.5, HOST['S'])
    img.rect(8, 45, 7, 15, SUIT_D)                                       # trousers
    img.rect(17, 45, 7, 15, SUIT_D)
    img.line(11, 46, 11, 59, SUIT_M)
    img.line(20, 46, 20, 59, SUIT_M)
    img.rect(6, 60, 9, 3, INK)                                           # shoes
    img.rect(17, 60, 9, 3, INK)
    img.set(8, 60, SUIT_M)
    img.set(19, 60, SUIT_M)
    head = Img(16, 18)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            head.set(x, y, HOST[ch])
    img.blit(head, 8, 0)
    img.outline(INK)
    return img


# ------------------------------------------------------------------ lettering (5 x 7, drawn here)
FONT = {
    'D': ['1110.', '1..1.', '1...1', '1...1', '1...1', '1..1.', '1110.'],
    'E': ['11111', '1....', '1....', '1111.', '1....', '1....', '11111'],
    'A': ['.111.', '1...1', '1...1', '11111', '1...1', '1...1', '1...1'],
    'L': ['1....', '1....', '1....', '1....', '1....', '1....', '11111'],
    'O': ['.111.', '1...1', '1...1', '1...1', '1...1', '1...1', '.111.'],
    'R': ['1111.', '1...1', '1...1', '1111.', '1.1..', '1..1.', '1...1'],
    'N': ['1...1', '11..1', '1.1.1', '1..11', '1...1', '1...1', '1...1'],
    ' ': ['.....'] * 7,
    '0': ['.111.', '1...1', '1..11', '1.1.1', '11..1', '1...1', '.111.'],
    '1': ['..1..', '.11..', '..1..', '..1..', '..1..', '..1..', '.111.'],
    '2': ['.111.', '1...1', '....1', '...1.', '..1..', '.1...', '11111'],
    '3': ['1111.', '....1', '....1', '.111.', '....1', '....1', '1111.'],
    '4': ['...1.', '..11.', '.1.1.', '1..1.', '11111', '...1.', '...1.'],
    '5': ['11111', '1....', '1111.', '....1', '....1', '1...1', '.111.'],
    '6': ['..11.', '.1...', '1....', '1111.', '1...1', '1...1', '.111.'],
    '7': ['11111', '....1', '...1.', '..1..', '.1...', '.1...', '.1...'],
    '8': ['.111.', '1...1', '1...1', '.111.', '1...1', '1...1', '.111.'],
    '9': ['.111.', '1...1', '1...1', '.1111', '....1', '...1.', '.11..'],
    '$': ['..1..', '.1111', '1.1..', '.111.', '..1.1', '1111.', '..1..'],
    ',': ['.....', '.....', '.....', '.....', '.....', '..1..', '.1...'],
    '.': ['.....', '.....', '.....', '.....', '.....', '.....', '..1..'],
}


FONT35 = {
    '0': ['111', '101', '101', '101', '111'], '1': ['010', '110', '010', '010', '111'],
    '2': ['111', '001', '111', '100', '111'], '3': ['111', '001', '011', '001', '111'],
    '4': ['101', '101', '111', '001', '001'], '5': ['111', '100', '111', '001', '111'],
    '6': ['111', '100', '111', '101', '111'], '7': ['111', '001', '010', '010', '010'],
    '8': ['111', '101', '111', '101', '111'], '9': ['111', '101', '111', '001', '111'],
    'K': ['101', '110', '100', '110', '101'], 'M': ['101', '111', '111', '101', '101'],
    '.': ['000', '000', '000', '000', '010'], '$': ['011', '110', '010', '011', '110'],
}


def lettering(img, text, x, y, scale, ramp, edge, shadow):
    """chunky gold letters: drop shadow, dark edge, dithered vertical ramp"""
    layer = Img(img.w, img.h)
    cx = x
    for ch in text:
        for r, row in enumerate(FONT[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    layer.rect(cx + c * scale, y + r * scale, scale, scale, 'X')
        cx += 6 * scale
    h = 7 * scale
    for yy in range(img.h):
        for xx in range(img.w):
            if layer.get(xx, yy) == 'X':
                img.set(xx + 2, yy + 2, shadow)
    for yy in range(img.h):
        for xx in range(img.w):
            if layer.get(xx, yy) == 'X':
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if layer.get(xx + dx, yy + dy) is None:
                        img.set(xx + dx, yy + dy, edge)
    fill = Img(img.w, img.h)
    fill.ramp(0, y, img.w, h, ramp)
    for yy in range(img.h):
        for xx in range(img.w):
            if layer.get(xx, yy) == 'X':
                img.set(xx, yy, fill.get(xx, yy))
    return cx - x


GOLD = [g(7, 7, 4), g(7, 6, 1), g(7, 5, 0), g(5, 3, 0)]


def sparkle(img, x, y, c):
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        img.set(x + dx, y + dy, c)


# ------------------------------------------------------------------ scenes, 240 x 160
def scene_title():
    img = Img(240, 160)
    img.ramp(0, 0, 240, 160, [g(0, 0, 2), g(1, 0, 3), g(2, 0, 3), g(3, 0, 3)])
    for ox in (40, 120, 200):                                            # spotlight beams
        beam = Img(240, 160)
        beam.poly([(ox - 4, 0), (ox + 4, 0), (ox + 46, 160), (ox - 46, 160)], 'B')
        for y in range(160):
            for x in range(240):
                if beam.get(x, y) == 'B' and (x + y) % 2 == 0:
                    img.set(x, y, g(3, 1, 5))
    for i, (x, y) in enumerate([(14, 12), (98, 8), (170, 22), (226, 10), (60, 140), (150, 132), (110, 150)]):
        sparkle(img, x, y, g(7, 7, 7) if i % 2 else g(7, 7, 3))
    img.ramp(0, 148, 240, 12, [g(2, 0, 3), g(5, 2, 4)])                  # stage lip glow
    w = 4 * 6 * 4 - 4
    lettering(img, 'DEAL', 14, 22, 4, GOLD, g(2, 1, 0), g(1, 0, 1))
    img.rect(14, 56, w, 18, g(5, 0, 1))                                  # red band
    img.rect(14, 56, w, 2, g(7, 2, 2))
    img.rect(14, 72, w, 2, g(3, 0, 0))
    lettering(img, 'OR NO', 23, 59, 2, [g(7, 7, 7), g(6, 6, 7)], g(2, 0, 0), g(3, 0, 0))
    lettering(img, 'DEAL', 14, 80, 4, GOLD, g(2, 1, 0), g(1, 0, 1))
    return img


def scene_stage():
    img = Img(240, 160)
    img.ramp(0, 0, 240, 112, [g(0, 0, 2), g(0, 1, 3), g(1, 1, 4)])
    for x in range(12, 240, 40):                                         # light columns
        img.shade(x, 16, 6, 96, g(2, 2, 6))
    for x in range(4, 240, 8):                                           # marquee bulbs
        img.rect(x, 6, 3, 3, g(7, 7, 4) if (x // 8) % 2 else g(6, 4, 1))
    img.rect(0, 12, 240, 2, g(5, 3, 0))
    for y in range(112, 160):                                            # perspective floor
        depth = (y - 108) / 52
        for x in range(240):
            u = (x - 120) / depth
            v = 1.0 / depth
            img.set(x, y, g(4, 2, 1) if (int(u // 24) + int(v * 6)) % 2 else g(3, 1, 0))
    img.rect(0, 110, 240, 2, g(7, 5, 1))
    for n, y in TIERS:                                                   # tier risers under each row
        x0 = CASE_XY[0][0] + (CASE_PITCH // 2 if n == 8 else 0) - 2
        w = n * CASE_PITCH + 2
        img.rect(x0, y + CASE_H + 1, w, 3, g(5, 3, 0))
        img.ramp(x0, y + CASE_H + 4, w, 5, [g(2, 0, 3), g(1, 0, 2)])
    img.ellipse(YOUR_XY[0] + CASE_W // 2, YOUR_XY[1] + CASE_H + 3, 16, 4, g(2, 1, 0))   # spot under yours
    return img


# briefcases on stage: three tiers of 9, 9 and 8, numbered left to right, top to bottom
CASE_W, CASE_H, CASE_PITCH = 20, 14, 22
TIERS = [(9, 22), (9, 48), (8, 74)]                                    # (cases, top y)
CASE_XY = [(2 + c * CASE_PITCH + (CASE_PITCH // 2 if n == 8 else 0), y) for n, y in TIERS for c in range(n)]
YOUR_XY = (176, 136)                                                     # your case, on the floor by the host
BOARD_X = (10, 90)                                                       # bar columns (ui.c draws amounts)


def scene_board():
    img = Img(240, 160)
    img.ramp(0, 0, 240, 160, [g(0, 0, 1), g(0, 0, 2), g(1, 0, 2)])
    img.rect(4, 4, 170, 152, g(5, 3, 0))                                 # gold frame
    img.rect(6, 6, 166, 148, g(7, 5, 1))
    img.rect(8, 8, 162, 144, g(0, 0, 1))
    for col, x0 in enumerate(BOARD_X):
        for row in range(13):
            y = 12 + row * 11
            img.rect(x0, y, 76, 9, g(2, 1, 0))
            img.rect(x0 + 1, y + 1, 74, 7, g(1, 0, 0))
    for i, (x, y) in enumerate([(186, 14), (222, 30), (204, 8)]):
        sparkle(img, x, y, g(7, 7, 3))
    return img


def scene_banker():
    img = Img(240, 160)
    img.ramp(0, 0, 240, 160, [g(0, 0, 1), g(1, 0, 2), g(1, 0, 1)])
    img.rect(18, 14, 136, 92, g(3, 3, 4))                                # window frame
    img.ramp(22, 18, 128, 84, [g(0, 1, 3), g(0, 2, 4), g(0, 1, 2)])
    img.ellipse(86, 54, 14, 16, g(0, 0, 0))                              # the banker, in shadow
    img.poly([(52, 102), (60, 76), (112, 76), (120, 102)], g(0, 0, 0))
    img.rect(80, 68, 12, 10, g(0, 0, 0))
    img.shade(22, 18, 128, 84, g(1, 3, 5), g(0, 2, 4))                   # glass glare
    img.line(30, 96, 70, 22, g(2, 4, 6))
    img.line(36, 96, 76, 22, g(2, 4, 6))
    img.rect(0, 112, 240, 48, g(3, 1, 0))                                # desk
    img.ramp(0, 112, 240, 48, [g(4, 2, 0), g(3, 1, 0), g(2, 1, 0)])
    img.rect(0, 110, 240, 3, g(5, 3, 1))
    img.rect(40, 122, 34, 14, g(6, 1, 1))                                # the phone
    img.rect(44, 118, 26, 6, g(7, 2, 2))
    img.rect(42, 124, 30, 2, g(4, 0, 0))
    for k in range(3):
        img.rect(48 + k * 7, 128, 4, 3, g(7, 7, 7))
    img.poly([(150, 112), (158, 70), (166, 112)], g(2, 2, 2))            # lamp
    img.ellipse(158, 68, 10, 6, g(6, 6, 2))
    return img


SCENES = [('TITLE', scene_title, (178, 86)), ('STAGE', scene_stage, (206, 84)),
          ('BOARD', scene_board, (194, 84)), ('BANKER', scene_banker, (186, 84))]


# ------------------------------------------------------------------ encoding (as dond_art_types.h)
def rle(idx):
    out, i, n = bytearray(), 0, len(idx)
    while i < n:
        r = 1
        while i + r < n and r < 129 and idx[i + r] == idx[i]:
            r += 1
        if r >= 2:
            out += bytes((r + 126, idx[i]))
            i += r
            continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and idx[j + 1] == idx[j]):
            j += 1
        j = max(j, i + 1)
        out.append(j - i - 1)
        out += bytes(idx[i:j])
        i = j
    return bytes(out)


def be565(c):
    v = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
    return ((v >> 8) | (v << 8)) & 0xFFFF


def c_bytes(name, data):
    rows = [', '.join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f'static const uint8_t {name}[{len(data)}] = {{\n    ' + ',\n    '.join(rows) + '\n};\n'


def c_pal(name, pal):
    return f'static const uint16_t {name}[{len(pal)}] = {{' + ', '.join(f'0x{be565(c):04X}' for c in pal) + '};\n'


def build():
    frames = [host_frame(m) for m in range(4)]
    hpal = sorted(set().union(*(f.colours() for f in frames)))
    if len(hpal) > 15:
        raise SystemExit(f'gen16 art: host uses {len(hpal)} colours, the limit is 15 + clear')
    hidx = {c: i + 1 for i, c in enumerate(hpal)}
    out, report, total = [], [], 0
    for i, (name, draw, (hx, hy)) in enumerate(SCENES):
        img = draw()
        pal = sorted(img.colours())
        if len(set(pal) | set(hpal)) > 61:
            raise SystemExit(f'gen16 art: {name} + host use {len(set(pal) | set(hpal))} colours, the limit is 61')
        if any(c is None for c in img.px):
            raise SystemExit(f'gen16 art: {name} has unpainted pixels')
        idx = {c: k for k, c in enumerate(pal)}
        data = rle([idx[c] for c in img.px])
        out.append(c_pal(f'bg{i}_pal', pal) + c_bytes(f'bg{i}_rle', data))
        total += len(data) + 2 * len(pal)
        report.append(f'{name}: {len(pal)} colours (+{len(set(hpal) - set(pal))} host), {len(data)} B')
    w, h = frames[0].w, frames[0].h
    ix = [[0 if c is None else hidx[c] for c in f.px] for f in frames]
    diff = [k for k in range(w * h) if any(f[k] != ix[0][k] for f in ix[1:])]
    fx0, fy0 = min(k % w for k in diff), min(k // w for k in diff)
    fx1, fy1 = max(k % w for k in diff) + 1, max(k // w for k in diff) + 1
    base = rle(ix[0])
    out.append(c_pal('host_pal', [(0, 0, 0)] + hpal) + c_bytes('host0_base', base))
    size = len(base)
    for f, fi in enumerate(ix):
        patch = rle([fi[y * w + x] for y in range(fy0, fy1) for x in range(fx0, fx1)])
        out.append(c_bytes(f'host0_f{f}', patch))
        size += len(patch)
    out.append('static const uint8_t *const host0_frames[4] = {host0_f0, host0_f1, host0_f2, host0_f3};\n')
    total += size + 32
    report.append(f'host: {len(hpal)} colours, {w}x{h}, mouth {fx1 - fx0}x{fy1 - fy0}, {size} B')
    report.append(f'total art: {total} B')
    hdr = ['/* generated by tools/gen16_art.py: original 16-bit art */', '#pragma once', '#define DOND_ART 1',
           '#include "app/dond_art_types.h"', *out,
           'static const dond_img_t DOND_BG[DOND_NBG] = {',
           *[f'    {{240, 160, bg{i}_pal, bg{i}_rle}},' for i in range(len(SCENES))], '};',
           'static const dond_host_t DOND_HOST[1] = {',
           f'    {{{{{w}, {h}, host_pal, host0_base}}, 4, {{{fx0}, {fy0}, {fx1 - fx0}, {fy1 - fy0}}}, host0_frames}},',
           '};', 'static const dond_place_t DOND_PLACE[DOND_NBG] = {',
           *[f'    {{0, {hx}, {hy}}},' for _, _, (hx, hy) in SCENES], '};',
           f'#define DOND_BOARD_X0 {BOARD_X[0]}', f'#define DOND_BOARD_X1 {BOARD_X[1]}',
           '/* 5 x 7 board font, ASCII 32..95: one byte per row, bit 4 = leftmost pixel */',
           'static const uint8_t DOND_FONT57[64][7] = {',
           *['    {' + ', '.join(str(int(r.replace('.', '0'), 2)) for r in FONT.get(chr(c), ['.....'] * 7)) + '},'
             for c in range(32, 96)], '};',
           '/* 3 x 5 case font, ASCII 32..95: one byte per row, bit 2 = leftmost pixel */',
           'static const uint8_t DOND_FONT35[64][5] = {',
           *['    {' + ', '.join(str(int(r, 2)) for r in FONT35.get(chr(c), ['000'] * 5)) + '},'
             for c in range(32, 96)], '};',
           f'#define DOND_CASE_W {CASE_W}', f'#define DOND_CASE_H {CASE_H}',
           f'#define DOND_YOUR_X {YOUR_XY[0]}', f'#define DOND_YOUR_Y {YOUR_XY[1]}',
           'static const uint8_t DOND_CASE_XY[26][2] = {' + ', '.join(f'{{{x}, {y}}}' for x, y in CASE_XY) + '};']
    return '\n'.join(hdr) + '\n', report, frames


def preview(dirpath, frames):
    from PIL import Image
    dirpath.mkdir(parents=True, exist_ok=True)
    for name, draw, (hx, hy) in SCENES:
        img = draw()
        img.blit(frames[0], hx, hy)
        pil = Image.new('RGB', (240, 160))
        pil.putdata(img.px)
        pil.resize((480, 320), Image.NEAREST).save(dirpath / f'{name.lower()}.png')
    sheet = Image.new('RGB', (4 * 34, 64), (255, 0, 255))
    for k, f in enumerate(frames):
        for y in range(64):
            for x in range(32):
                c = f.get(x, y)
                if c is not None:
                    sheet.putpixel((k * 34 + x, y), c)
    sheet.resize((sheet.width * 4, 256), Image.NEAREST).save(dirpath / 'host.png')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path)
    ap.add_argument('--preview', type=Path)
    a = ap.parse_args()
    text, report, frames = build()
    if not a.out.exists() or a.out.read_text() != text:
        a.out.write_text(text)
    if a.preview:
        preview(a.preview, frames)
    print('\n'.join(report))


if __name__ == '__main__':
    main()
