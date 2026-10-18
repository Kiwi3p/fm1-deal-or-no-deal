/* SPDX-License-Identifier: GPL-3.0-only */
/* 16-bit DOND sound, included by omni.c (audio ISR side): 4 FM voices of 4 operators, 3 square
 * voices and 1 noise voice at G16_RATE, and a row sequencer that loops a bed under one-shot
 * stings (the bed starts over after each sting). Plus the host's voice: a queue of 4-bit ADPCM
 * clips at G16_VOICE_RATE (an offer is stitched from number words), with the music ducked under
 * it. om_render upsamples both to the output rate. The main loop only posts requests
 * (om_music_bed, om_music_sting, om_voice_say); everything else runs here. */
#include "gen16.h"
#define GEN16_MUSIC_DATA
#include "gen16_music.h"
#define GEN16_VOICE_DATA
#include "gen16_voice.h"

#define G16_MODK 683565275.6f     /* 2^32 / 2 pi: a modulator output of 1.0 moves the phase 1 radian */
#define G16_FM_GAIN 0.20f
#define G16_PSG_GAIN 0.50f
#define G16_PSG_RELEASE 0.997f    /* per sample after a note off */
#define G16_VQ 24                 /* clips in one spoken phrase */
#define G16_VGAP 700u             /* voice samples of silence between clips (~65 ms) */
#define G16_VOICE_GAIN 0.90f
#define G16_DUCK 0.35f            /* music level under speech */
#define G16_DUCK_RATE 0.0008f     /* per output sample, ~30 ms */

typedef struct {
    uint32_t ph[4], inc[4];
    float env[4], fb0, fb1;
    uint8_t st[4];                /* 0 off, 1 attack, 2 decay to sustain, 3 release */
    const g16_fm_t *p;
} g16_fmv_t;

typedef struct {
    uint32_t ph, base, arp_left;
    float env, inc;
    uint16_t lfsr;
    uint8_t on, rel, alt;
    const g16_psg_t *p;
} g16_psgv_t;

static float g16_sin[1024];
static g16_fmv_t g16_fm[4];
static g16_psgv_t g16_psg[4];
static struct {
    const g16_song_t *s;
    const uint8_t *cur[8];
    uint8_t inst[8];
    uint32_t left, row;
} g16_seq;
static volatile int32_t g16_req_bed = -2, g16_req_sting = -1, g16_req_stop;
static int32_t g16_bed = -1;
static uint8_t g16_in_sting;
static float g16_prev, g16_cur;
static uint32_t g16_half;

static const int16_t G16_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t G16_INDEX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static volatile uint8_t g16_vreq[G16_VQ];
static volatile uint32_t g16_vreq_n, g16_vreq_seq, g16_vstop;
static uint32_t g16_vseen;
static struct {
    uint8_t q[G16_VQ];
    uint32_t qn, qi, off, n, pos, gap, sub;
    int32_t pred, idx;
    float s0, s1;
    uint8_t on;
} g16_v;
static float g16_duck_gain = 1.0f;

static void g16_init(void)
{
    int i;
    for (i = 0; i < 1024; i++)
        g16_sin[i] = fm_sin_turns((float)i * (1.0f / 1024.0f));
    for (i = 0; i < 4; i++) {
        g16_fm[i] = (g16_fmv_t){{0}};
        g16_psg[i] = (g16_psgv_t){0};
        g16_psg[i].lfsr = 1;
    }
    g16_seq.s = 0;
    g16_req_bed = -2;
    g16_req_sting = -1;
    g16_req_stop = 0;
    g16_bed = -1;
    g16_in_sting = 0;
    g16_prev = g16_cur = 0;
    g16_half = 0;
    g16_v.on = 0;
    g16_vstop = 0;
    g16_vseen = g16_vreq_seq;
    g16_duck_gain = 1.0f;
}

/* ---------------------------------------------------------------- FM */
static uint32_t g16_scale_inc(uint32_t inc, float mult)
{
    float f = (float)inc * mult;
    return f >= 2147483648.0f ? 2147483647u : (uint32_t)f;    /* cap at Nyquist */
}

static void g16_fm_on(g16_fmv_t *v, const g16_fm_t *p, int note)
{
    int k;
    v->p = p;
    v->fb0 = v->fb1 = 0;
    for (k = 0; k < 4; k++) {
        v->inc[k] = g16_scale_inc(G16_INC[note], p->op[k].mult);
        v->ph[k] = 0;
        v->env[k] = 0;
        v->st[k] = 1;
    }
}

static void g16_fm_off(g16_fmv_t *v)
{
    int k;
    for (k = 0; k < 4; k++)
        if (v->st[k])
            v->st[k] = 3;
}

static inline void g16_env(g16_fmv_t *v, int k)
{
    const g16_op_t *o = &v->p->op[k];
    switch (v->st[k]) {
    case 1:
        v->env[k] += o->att;
        if (v->env[k] >= 1.0f) {
            v->env[k] = 1.0f;
            v->st[k] = 2;
        }
        break;
    case 2:
        v->env[k] = o->sus + (v->env[k] - o->sus) * o->dk;
        break;
    case 3:
        v->env[k] *= o->rk;
        if (v->env[k] < 1e-4f) {
            v->env[k] = 0;
            v->st[k] = 0;
        }
        break;
    }
}

static inline float g16_op(g16_fmv_t *v, int k, float mod)
{
    uint32_t ph;
    if (mod > 3.0f)
        mod = 3.0f;
    else if (mod < -3.0f)
        mod = -3.0f;
    ph = v->ph[k] + (uint32_t)(int32_t)(mod * G16_MODK);
    return g16_sin[ph >> 22] * v->env[k] * v->p->op[k].level;
}

static float g16_fm_sample(g16_fmv_t *v)
{
    float o1, out;
    int k;
    if (!v->p || !(v->st[0] | v->st[1] | v->st[2] | v->st[3]))
        return 0;
    for (k = 0; k < 4; k++)
        g16_env(v, k);
    o1 = g16_op(v, 0, (v->fb0 + v->fb1) * v->p->fb);
    v->fb1 = v->fb0;
    v->fb0 = o1;
    switch (v->p->alg) {
    case 0:
        out = g16_op(v, 3, g16_op(v, 2, g16_op(v, 1, o1)));
        break;
    case 5:
        out = g16_op(v, 1, o1) + g16_op(v, 2, o1) + g16_op(v, 3, o1);
        break;
    case 7:
        out = o1 + g16_op(v, 1, 0) + g16_op(v, 2, 0) + g16_op(v, 3, 0);
        break;
    default:                      /* 4: 1 -> 2, 3 -> 4 */
        out = g16_op(v, 1, o1) + g16_op(v, 3, g16_op(v, 2, 0));
        break;
    }
    for (k = 0; k < 4; k++)
        v->ph[k] += v->inc[k];
    return out;
}

/* ---------------------------------------------------------------- PSG */
static void g16_psg_on(g16_psgv_t *v, const g16_psg_t *p, int note)
{
    v->p = p;
    v->base = G16_INC[note];
    v->inc = (float)v->base * (p->kind == 1 ? 8.0f : 1.0f);   /* noise clocks 8x the note */
    if (v->inc > 4.0e9f)
        v->inc = 4.0e9f;
    v->env = p->vol;
    v->on = 1;
    v->rel = 0;
    v->alt = 0;
    v->arp_left = p->arp_n;
    v->ph = 0;
}

static float g16_psg_sample(g16_psgv_t *v)
{
    float out;
    if (!v->on)
        return 0;
    if (v->p->kind == 1) {
        uint32_t prev = v->ph;
        v->ph += (uint32_t)v->inc;
        if (v->ph < prev) {                                      /* 15-bit white noise */
            uint32_t bit = (v->lfsr ^ (v->lfsr >> 1)) & 1u;
            v->lfsr = (uint16_t)((v->lfsr >> 1) | (bit << 14));
        }
        out = (v->lfsr & 1u) ? 1.0f : -1.0f;
    } else {
        if (v->p->arp_n && !--v->arp_left) {                     /* trill between two pitches */
            v->arp_left = v->p->arp_n;
            v->alt ^= 1u;
            v->inc = (float)v->base * (v->alt ? v->p->arp : 1.0f);
        }
        if (v->p->kind == 2)
            v->inc *= v->p->sweep;
        v->ph += (uint32_t)v->inc;
        out = (v->ph & 0x80000000u) ? 1.0f : -1.0f;
    }
    out *= v->env;
    v->env *= v->rel ? G16_PSG_RELEASE : v->p->dk;
    if (v->env < 1e-4f)
        v->on = 0;
    return out;
}

/* ---------------------------------------------------------------- sequencer */
static void g16_all_off(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        g16_fm_off(&g16_fm[i]);
        g16_psg[i].rel = 1;
    }
}

static void g16_start(int32_t song)
{
    int c;
    g16_all_off();
    if (song < 0 || song >= G16_NSONGS) {
        g16_seq.s = 0;
        return;
    }
    g16_seq.s = &G16_SONGS[song];
    for (c = 0; c < 8; c++) {
        g16_seq.cur[c] = g16_seq.s->ch[c];
        g16_seq.inst[c] = 0;
    }
    g16_seq.row = 0;
    g16_seq.left = 1;                                            /* row 0 plays on the next sample */
}

static void g16_row(void)
{
    int c;
    for (c = 0; c < 8; c++) {
        const uint8_t *p = g16_seq.cur[c];
        uint32_t b;
        if (!p)
            continue;
        while (*p >= 130u)
            g16_seq.inst[c] = (uint8_t)(*p++ - 130u);
        b = *p++;
        g16_seq.cur[c] = p;
        if (b == 1u) {
            if (c < 4)
                g16_fm_off(&g16_fm[c]);
            else
                g16_psg[c - 4].rel = 1;
        } else if (b >= 2u) {
            if (c < 4)
                g16_fm_on(&g16_fm[c], &G16_FM[g16_seq.inst[c]], (int)b - 2);
            else
                g16_psg_on(&g16_psg[c - 4], &G16_PSG[g16_seq.inst[c]], (int)b - 2);
        }
    }
}

static void g16_tick(void)
{
    if (!g16_seq.s || --g16_seq.left)
        return;
    if (g16_seq.row == g16_seq.s->rows) {
        if (g16_seq.s->loop) {
            g16_start((int32_t)(g16_seq.s - G16_SONGS));
        } else {                                                 /* a sting ended: back to the bed */
            g16_in_sting = 0;
            g16_start(g16_bed);
        }
        if (!g16_seq.s)
            return;
    }
    g16_row();
    g16_seq.row++;
    g16_seq.left = g16_seq.s->row;
}

/* requests from the main loop, taken once per render block */
static void g16_requests(void)
{
    int32_t b = g16_req_bed, s = g16_req_sting;
    if (g16_req_stop) {
        g16_req_stop = 0;
        g16_bed = -1;
        g16_in_sting = 0;
        g16_start(-1);
    }
    if (s >= 0) {
        g16_req_sting = -1;
        g16_in_sting = 1;
        g16_start(s);
    }
    if (b != -2) {
        g16_req_bed = -2;
        if (b != g16_bed) {
            g16_bed = b;
            if (!g16_in_sting)
                g16_start(g16_bed);
        }
    }
}

static float g16_sample(void)
{
    float s = 0;
    int i;
    g16_tick();
    for (i = 0; i < 4; i++)
        s += g16_fm_sample(&g16_fm[i]) * G16_FM_GAIN;
    for (i = 0; i < 4; i++)
        s += g16_psg_sample(&g16_psg[i]) * G16_PSG_GAIN;
    return s;
}

/* one output sample at twice G16_RATE: every other call runs the synth, the one between is the
 * midpoint of the last two */
static float g16_out(void)
{
    g16_half ^= 1u;
    if (g16_half) {
        g16_prev = g16_cur;
        g16_cur = g16_sample();
        return 0.5f * (g16_prev + g16_cur);
    }
    return g16_cur;
}

/* ---------------------------------------------------------------- voice */
static int g16_vnext(void)
{
    while (g16_v.qi < g16_v.qn) {
        uint32_t c = g16_v.q[g16_v.qi++];
        if (c >= VO_COUNT || !G16_CLIPS[c].n)
            continue;                                            /* an empty clip is just skipped */
        g16_v.off = G16_CLIPS[c].off;
        g16_v.n = G16_CLIPS[c].n;
        g16_v.pos = 0;
        g16_v.pred = 0;
        g16_v.idx = 0;
        return 1;
    }
    return 0;
}

static float g16_vdecode(void)
{
    uint32_t b = G16_VOICE[g16_v.off + (g16_v.pos >> 1)], c = (g16_v.pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t st = G16_STEP[g16_v.idx], d = st >> 3;
    if (c & 4u)
        d += st;
    if (c & 2u)
        d += st >> 1;
    if (c & 1u)
        d += st >> 2;
    g16_v.pred += (c & 8u) ? -d : d;
    if (g16_v.pred > 32767)
        g16_v.pred = 32767;
    else if (g16_v.pred < -32768)
        g16_v.pred = -32768;
    g16_v.idx += G16_INDEX[c & 7u];
    if (g16_v.idx < 0)
        g16_v.idx = 0;
    else if (g16_v.idx > 88)
        g16_v.idx = 88;
    g16_v.pos++;
    return (float)g16_v.pred * (1.0f / 32768.0f);
}

static void g16_vrequests(void)
{
    uint32_t seq = g16_vreq_seq, i;
    if (g16_vstop) {
        g16_vstop = 0;
        g16_v.on = 0;
        om_voice_active = 0;
    }
    if (seq == g16_vseen)
        return;
    g16_vseen = seq;
    g16_v.qn = g16_vreq_n > G16_VQ ? G16_VQ : g16_vreq_n;
    for (i = 0; i < g16_v.qn; i++)
        g16_v.q[i] = g16_vreq[i];
    g16_v.qi = 0;
    g16_v.sub = 0;
    g16_v.gap = 0;
    g16_v.s0 = g16_v.s1 = 0;
    g16_v.on = (uint8_t)g16_vnext();
    om_voice_active = g16_v.on;
    if (g16_v.on)
        om_voice_starts++;
}

/* one output sample: a new voice sample every 4th (11,025 -> 44,100 Hz), linear between */
static float g16_voice_out(void)
{
    float out;
    if (!g16_v.on)
        return 0;
    if (!g16_v.sub) {
        g16_v.s0 = g16_v.s1;
        if (g16_v.gap) {
            g16_v.gap--;
            g16_v.s1 = 0;
        } else if (g16_v.pos < g16_v.n) {
            g16_v.s1 = g16_vdecode();
        } else if (g16_vnext()) {
            g16_v.gap = G16_VGAP;
            g16_v.s1 = 0;
        } else {
            g16_v.s1 = 0;
            if (g16_v.s0 == 0.0f) {                              /* the phrase is over */
                g16_v.on = 0;
                om_voice_active = 0;
                om_voice_finished++;
                return 0;
            }
        }
    }
    out = g16_v.s0 + (g16_v.s1 - g16_v.s0) * (float)g16_v.sub * 0.25f;
    g16_v.sub = (g16_v.sub + 1u) & 3u;
    return out;
}

/* music, ducked while the host speaks, plus the voice */
static float g16_mix(void)
{
    float target = g16_v.on ? G16_DUCK : 1.0f;
    g16_duck_gain += (target - g16_duck_gain) * G16_DUCK_RATE;
    return g16_out() * g16_duck_gain + g16_voice_out() * G16_VOICE_GAIN;
}

void om_voice_say(const uint8_t *clips, int n)
{
    int i;
    if (n > G16_VQ)
        n = G16_VQ;
    for (i = 0; i < n; i++)
        g16_vreq[i] = clips[i];
    g16_vreq_n = (uint32_t)(n < 0 ? 0 : n);
    __asm__ volatile("" ::: "memory");                           /* the phrase, then the go */
    g16_vreq_seq++;
}

int om_voice_play(int clip)
{
    uint8_t c = (uint8_t)clip;
    if (clip < 0 || clip >= VO_COUNT)
        return 0;
    om_voice_say(&c, 1);
    return 1;
}

int om_voice_busy(void) { return om_voice_active || g16_vreq_seq != g16_vseen; }
void om_voice_stop(void) { g16_vstop = 1; }
unsigned om_voice_frames(int clip) { return clip >= 0 && clip < VO_COUNT ? G16_CLIPS[clip].n * 4u : 0u; }

void om_music_bed(int song) { g16_req_bed = song; }
void om_music_sting(int song) { g16_req_sting = song; }
