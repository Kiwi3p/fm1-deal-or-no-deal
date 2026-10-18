/* SPDX-License-Identifier: GPL-3.0-only */
/* Deal or No Deal audio: the 16-bit soundtrack (gen16.c), short synthesized SFX, MASTER gain. */
#include "omni.h"
#include "fastmath.h"
volatile uint32_t om_voice_active, om_voice_starts, om_voice_finished;
#include "gen16.c"

static const om_param_t params[P_NPARAMS] = {
    {"REVERB", 0, 100, 15}, {"DELAY", 0, 100, 0}, {"TIME ms", 80, 500, 240}, {"REPEATS", 0, 70, 25}
};
const om_param_t *om_param_info(int p) { return p >= 0 && p < P_NPARAMS ? &params[p] : 0; }
const char *om_sample_name(int k) { (void)k; return ""; }
int om_placeholder(void) { return 0; }

volatile uint32_t om_active_keys, om_triggers, om_dropped, om_bongo_hits, om_bongo_active;
volatile uint8_t om_seq_playing, om_seq_recording, om_seq_step;
volatile uint16_t om_seq_bpm;
volatile uint32_t om_seq_mask[FMO_STEPS], om_seq_ticks, om_seq_hit_dropped;
volatile uint32_t om_foot_hits, om_game_over_starts;

void om_seq_toggle_play(void) {}
void om_seq_toggle_record(void) {}
void om_seq_clear(void) {}
void om_seq_set_tempo(int bpm) { (void)bpm; }
int om_take_seq_hit(int *key) { (void)key; return 0; }
void om_bongo_enable(int enabled) { (void)enabled; om_bongo_active = 0; }
void om_trigger(int key, int velocity) { (void)key; (void)velocity; }
void om_note_off(int key) { (void)key; }
void om_footstep(int walking, unsigned step) { (void)walking; (void)step; }
int om_foot_busy(void) { return 0; }
void om_game_over(void) { om_game_over_starts++; }
int om_game_over_busy(void) { return 0; }

#define BARRIER() __asm__ volatile("" ::: "memory")

typedef struct {
    uint32_t remaining, age, length;
    float phase, hz0, hz1, gain;
    uint8_t kind; /* 0 tone sweep, 1 click noise, 2 multi-beep */
    uint8_t beeps, beep_i;
} sfx_t;

static sfx_t sfx;
static volatile uint32_t sfx_req, sfx_id, panic_pending;
static int par[P_NPARAMS];
static uint32_t noise;

static void sfx_start(int id)
{
    sfx = (sfx_t){0};
    sfx.kind = 0;
    sfx.gain = 0.12f;
    switch (id) {
    case SFX_CLICK:
        sfx.kind = 1;
        sfx.length = sfx.remaining = 900u;
        sfx.gain = 0.08f;
        break;
    case SFX_SMALL:
        sfx.hz0 = 660.0f;
        sfx.hz1 = 440.0f;
        sfx.length = sfx.remaining = 12000u;
        sfx.gain = 0.09f;
        break;
    case SFX_BIG:
        sfx.hz0 = 220.0f;
        sfx.hz1 = 110.0f;
        sfx.length = sfx.remaining = 22050u;
        sfx.gain = 0.14f;
        break;
    case SFX_BANKER:
        sfx.kind = 2;
        sfx.beeps = 3;
        sfx.beep_i = 0;
        sfx.hz0 = 880.0f;
        sfx.hz1 = 880.0f;
        sfx.length = sfx.remaining = 18000u;
        sfx.gain = 0.10f;
        break;
    case SFX_DEAL:
        sfx.hz0 = 392.0f;
        sfx.hz1 = 523.25f;
        sfx.length = sfx.remaining = 16000u;
        sfx.gain = 0.12f;
        break;
    case SFX_NODEAL:
        sfx.hz0 = 349.23f;
        sfx.hz1 = 261.63f;
        sfx.length = sfx.remaining = 14000u;
        sfx.gain = 0.11f;
        break;
    case SFX_REVEAL:
        sfx.hz0 = 523.25f;
        sfx.hz1 = 784.0f;
        sfx.length = sfx.remaining = 24000u;
        sfx.gain = 0.13f;
        break;
    default:
        sfx.remaining = 0;
        break;
    }
}

void om_sfx(int id)
{
    if (id < 0 || id >= SFX_COUNT)
        return;
    sfx_id = (uint32_t)id;
    BARRIER();
    sfx_req = 1;
}

int om_sfx_busy(void) { return sfx_req || sfx.remaining; }

void om_set(int p, int value)
{
    if (p >= 0 && p < P_NPARAMS)
        par[p] = value < params[p].lo ? params[p].lo : value > params[p].hi ? params[p].hi : value;
}

void om_panic(void)
{
    panic_pending = 1;
    g16_req_stop = 1;
    g16_vstop = 1;
    sfx_req = 0;
    sfx.remaining = 0;
}

void om_init(void)
{
    int i;
    panic_pending = 0;
    sfx_req = 0;
    sfx.remaining = 0;
    om_triggers = om_dropped = om_bongo_hits = om_bongo_active = 0;
    om_voice_active = om_voice_starts = om_voice_finished = 0;
    om_foot_hits = om_game_over_starts = 0;
    om_active_keys = 0;
    noise = 0xA341316Cu;
    g16_init();
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = params[i].def;
    om_seq_playing = om_seq_recording = om_seq_step = 0;
    om_seq_bpm = 100;
    om_seq_ticks = om_seq_hit_dropped = 0;
    for (i = 0; i < FMO_STEPS; i++)
        om_seq_mask[i] = 0;
}

static float limit(float x) { return x / (1.0f + fm_fabsf(x)); }

static float sfx_sample(void)
{
    float env, hz, t, out;
    uint32_t pos, seg, local, seg_len;
    if (!sfx.remaining)
        return 0;
    pos = sfx.length - sfx.remaining;
    sfx.remaining--;
    if (sfx.kind == 1) {
        noise = noise * 1664525u + 1013904223u;
        env = sfx.remaining > 700u ? (float)(900u - sfx.remaining) / 200.0f
                                   : (float)sfx.remaining / 700.0f;
        if (env > 1.0f)
            env = 1.0f;
        return ((float)(noise >> 8) * (1.0f / 8388608.0f) - 1.0f) * env * sfx.gain;
    }
    if (sfx.kind == 2) {
        seg_len = sfx.length / (sfx.beeps ? sfx.beeps : 1u);
        if (!seg_len)
            seg_len = 1;
        seg = pos / seg_len;
        local = pos % seg_len;
        if (seg >= sfx.beeps)
            return 0;
        if (local > seg_len * 3u / 5u)
            return 0;
        t = (float)local / (float)(seg_len * 3u / 5u);
        env = t < 0.1f ? t / 0.1f : (t > 0.85f ? (1.0f - t) / 0.15f : 1.0f);
        hz = 660.0f + 110.0f * (float)seg;
        sfx.phase += hz / 44100.0f;
        if (sfx.phase >= 1.0f)
            sfx.phase -= 1.0f;
        return fm_sin_turns(sfx.phase) * env * sfx.gain;
    }
    t = (float)pos / (float)(sfx.length ? sfx.length : 1u);
    hz = sfx.hz0 + (sfx.hz1 - sfx.hz0) * t;
    env = t < 0.05f ? t / 0.05f : (t > 0.8f ? (1.0f - t) / 0.2f : 1.0f);
    sfx.phase += hz / 44100.0f;
    if (sfx.phase >= 1.0f)
        sfx.phase -= 1.0f;
    out = fm_sin_turns(sfx.phase) * env * sfx.gain;
    return out;
}

void om_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    uint32_t i;
    float master = (float)(gain_q12 > 4096u ? 4096u : gain_q12) * (1.0f / 4096.0f);
    if (panic_pending) {
        panic_pending = 0;
        sfx.remaining = 0;
        sfx_req = 0;
    }
    if (sfx_req) {
        sfx_start((int)sfx_id);
        sfx_req = 0;
    }
    g16_requests();
    g16_vrequests();
    for (i = 0; i < n; i++) {
        float s = limit(sfx_sample() + g16_mix()) * master;
        out[2 * i] = (int32_t)(s * 8388607.0f);
        out[2 * i + 1] = (int32_t)(s * 8388607.0f);
    }
    om_active_keys = 0;
}
