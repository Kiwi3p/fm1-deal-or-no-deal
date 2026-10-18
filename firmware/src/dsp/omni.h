/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#define FMO_KEYS 27
#define FMO_VOICES 8
enum { P_REVERB, P_DELAY, P_TIME, P_FEEDBACK, P_NPARAMS };
typedef struct { const char *name; int16_t lo, hi, def; } om_param_t;
const om_param_t *om_param_info(int p);
void om_init(void);
void om_set(int p, int value);
void om_trigger(int key, int velocity);
void om_note_off(int key);
void om_bongo_enable(int enabled);
extern volatile uint32_t om_bongo_hits, om_bongo_active;
void om_panic(void);
void om_game_over(void);
int om_game_over_busy(void);
extern volatile uint32_t om_game_over_starts;

/* Phase-1 Deal or No Deal synthesized one-shots. */
enum {
    SFX_CLICK = 0,
    SFX_SMALL,
    SFX_BIG,
    SFX_BANKER,
    SFX_DEAL,
    SFX_NODEAL,
    SFX_REVEAL,
    SFX_COUNT
};
void om_sfx(int id);

/* 16-bit soundtrack (SONG_* in gen16_music.h): a looping bed, -1 = silence (no restart if it is
 * already the bed); a one-shot sting over it, after which the bed starts again */
void om_music_bed(int song);
void om_music_sting(int song);
int om_sfx_busy(void);

/* the host's voice (VO_* / W_* in gen16_voice.h): say a phrase of clips in order, replacing any
 * phrase still playing; music ducks under it */
void om_voice_say(const uint8_t *clips, int n);
int om_voice_play(int clip);
int om_voice_busy(void);
void om_voice_stop(void);
extern volatile uint32_t om_voice_active, om_voice_starts, om_voice_finished;
unsigned om_voice_frames(int clip);
void om_footstep(int walking, unsigned step);
int om_foot_busy(void);
extern volatile uint32_t om_foot_hits;
void om_render(int32_t *out, uint32_t n, uint32_t gain_q12);
const char *om_sample_name(int key);
int om_placeholder(void);
extern volatile uint32_t om_active_keys, om_triggers, om_dropped;

#define FMO_STEPS 16
void om_seq_toggle_play(void);
void om_seq_toggle_record(void);
void om_seq_clear(void);
void om_seq_set_tempo(int bpm);
int om_take_seq_hit(int *key);
extern volatile uint8_t om_seq_playing, om_seq_recording, om_seq_step;
extern volatile uint16_t om_seq_bpm;
extern volatile uint32_t om_seq_mask[FMO_STEPS], om_seq_ticks, om_seq_hit_dropped;
