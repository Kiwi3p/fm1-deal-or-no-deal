/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

#define DOND_NCASES 26
#define DOND_UNUSED_KEY 26

enum dond_phase_t {
    DOND_TITLE,
    DOND_PICK,
    DOND_OPEN,
    DOND_FLASH,      /* brief reveal of just-opened amount */
    DOND_BANKER,
    DOND_DEAL_END,
    DOND_FINAL,      /* keep your case or swap with the last */
    DOND_REVEAL,
    DOND_CONFIRM     /* GLO: start over? */
};

typedef struct {
    enum dond_phase_t phase;
    uint32_t amounts[DOND_NCASES]; /* cents in each case slot 0..25 */
    uint8_t opened[DOND_NCASES];   /* 1 = opened (not yours) */
    int8_t yours;                  /* case index 0..25, -1 before pick */
    uint8_t round;                 /* 0-based open round */
    uint8_t need_open;             /* cases still required this round */
    uint8_t offers;                /* banker offers made */
    uint32_t offer_cents;
    uint32_t win_cents;
    int8_t last_opened;            /* case index just opened, -1 none */
    uint32_t last_amount;          /* amount of last opened case */
    uint8_t took_deal;             /* 1 if ended on banker deal */
    uint8_t swapped;               /* 1 if final swap chosen */
    enum dond_phase_t resume;      /* phase restored after cancel confirm */
    uint32_t rng;
} dond_t;

extern dond_t dond;

/* Classic US ladder, cents ascending for board display. */
extern const uint32_t DOND_LADDER[DOND_NCASES];

void dond_init(uint32_t seed);
void dond_new_game(uint32_t seed);

int dond_start(void);      /* TITLE -> PICK; the case is chosen afterwards, as on the show */

/* Input: case key 0..25. Returns 1 if state changed. */
int dond_pick_case(int case_i);
int dond_open_case(int case_i);

/* Advance past flash reveal into OPEN or BANKER. */
void dond_flash_done(void);

int dond_deal(void);     /* accept offer; 1 if accepted */
int dond_no_deal(void);  /* refuse offer; 1 if advanced */
int dond_keep(void);     /* final: keep yours */
int dond_swap(void);     /* final: take the other case */

/* REVEAL: 1 if the result beat the road not taken (the deal vs your case; keep/swap vs the other) */
int dond_good_outcome(void);

void dond_begin_confirm(void);
void dond_cancel_confirm(void);
void dond_confirm_yes(uint32_t seed);

uint32_t dond_closed_mask(void); /* bit k lit while case k still closed (and not unused) */
int dond_board_left(void);       /* unopened cases excluding yours */
uint32_t dond_your_amount(void);
uint32_t dond_other_amount(void); /* last closed board case; 0 if none */

/* Format cents into buf (e.g. "$1,000" / "$0.01"). Returns buf. */
char *dond_fmt_money(uint32_t cents, char *buf, unsigned buflen);

/* Round quota for round index (0..): 6,5,4,3,2, then 1 forever. */
int dond_round_quota(int round);

/* Banker offer from remaining closed amounts (including yours). */
uint32_t dond_calc_offer(void);
