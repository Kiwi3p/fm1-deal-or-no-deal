/* SPDX-License-Identifier: GPL-3.0-only */
/* Shapes of the generated 16-bit soundtrack (build/gen/gen16_music.h, tools/gen16_music.py). */
#pragma once
#include <stdint.h>

typedef struct {
    float mult, level;            /* frequency multiple; index (modulator) or loudness (carrier) */
    float att, dk, sus, rk;       /* per sample: attack step, decay pull, sustain level, release fall */
} g16_op_t;

typedef struct {
    uint8_t alg;                  /* 0 serial, 4 two pairs, 5 one-to-three, 7 additive */
    float fb;                     /* operator 1 self-feedback */
    g16_op_t op[4];
} g16_fm_t;

typedef struct {
    uint8_t kind;                 /* 0 square, 1 noise, 2 square with falling pitch */
    float vol, dk;                /* start level, per-sample decay */
    float sweep;                  /* per-sample pitch multiplier (kind 2) */
    float arp;                    /* alternate pitch ratio, every arp_n samples (0 = none) */
    uint32_t arp_n;
} g16_psg_t;

typedef struct {
    uint32_t row, rows;           /* samples per row (a 16th note), rows in the song */
    uint8_t loop;
    const uint8_t *ch[8];         /* 0..3 FM, 4..6 square, 7 noise; 0 = silent */
} g16_song_t;
