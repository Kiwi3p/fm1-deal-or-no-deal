/* SPDX-License-Identifier: GPL-3.0-only */
/* Shapes of the generated 16-bit art tables (build/gen/gen16_art.h, tools/gen16_art.py). */
#pragma once
#include <stdint.h>

enum { DOND_BG_TITLE, DOND_BG_STAGE, DOND_BG_BOARD, DOND_BG_BANKER, DOND_NBG };

typedef struct {
    uint16_t w, h;
    const uint16_t *pal;           /* RGB565, byte-swapped like the canvas */
    const uint8_t *rle;            /* see cv_rle() */
} dond_img_t;

typedef struct {
    dond_img_t base;               /* frame 0; index 0 transparent */
    uint8_t nframes;
    struct { uint8_t x, y, w, h; } face;   /* the only rect that differs between frames */
    const uint8_t *const *frames;  /* per frame: the face rect, RLE */
} dond_host_t;

typedef struct {
    int8_t host;                   /* host row standing in this background, -1 none */
    int16_t x, y;
} dond_place_t;
