/* SPDX-License-Identifier: GPL-3.0-only */
#include "dond.h"

dond_t dond;

const uint32_t DOND_LADDER[DOND_NCASES] = {
    1u, 100u, 500u, 1000u, 2500u, 5000u, 7500u, 10000u,
    20000u, 30000u, 40000u, 50000u, 75000u, 100000u,
    500000u, 1000000u, 2500000u, 5000000u, 7500000u, 10000000u,
    20000000u, 30000000u, 40000000u, 50000000u, 75000000u, 100000000u
};

static uint32_t rng_next(void)
{
    uint32_t x = dond.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    dond.rng = x ? x : 0xA5A5F00Du;
    return dond.rng;
}

int dond_round_quota(int round)
{
    static const uint8_t q[] = {6, 5, 4, 3, 2};
    if (round < 0)
        return 1;
    if (round < (int)(sizeof q / sizeof q[0]))
        return (int)q[round];
    return 1;
}

char *dond_fmt_money(uint32_t cents, char *buf, unsigned buflen)
{
    char tmp[20];
    unsigned n = 0, i, j, dollars, rem;
    if (!buf || buflen < 4u) {
        if (buf && buflen)
            buf[0] = 0;
        return buf;
    }
    if (cents < 100u) {
        /* $0.01 .. $0.99 */
        if (buflen < 6u) {
            buf[0] = 0;
            return buf;
        }
        buf[0] = '$';
        buf[1] = '0';
        buf[2] = '.';
        buf[3] = (char)('0' + (cents / 10u) % 10u);
        buf[4] = (char)('0' + cents % 10u);
        buf[5] = 0;
        return buf;
    }
    dollars = cents / 100u;
    rem = cents % 100u;
    if (rem == 0u) {
        do {
            tmp[n++] = (char)('0' + dollars % 10u);
            dollars /= 10u;
            if (dollars && n % 4u == 3u)
                tmp[n++] = ',';
        } while (dollars && n < sizeof tmp);
        if (buflen < n + 2u) {
            buf[0] = 0;
            return buf;
        }
        buf[0] = '$';
        for (i = 0, j = n; j; i++, j--)
            buf[1 + i] = tmp[j - 1u];
        buf[1 + n] = 0;
        return buf;
    }
    /* rare non-ladder fractional dollars */
    return dond_fmt_money(cents - rem, buf, buflen);
}

static void shuffle_amounts(void)
{
    int i, j;
    uint32_t t;
    for (i = 0; i < DOND_NCASES; i++)
        dond.amounts[i] = DOND_LADDER[i];
    for (i = DOND_NCASES - 1; i > 0; i--) {
        j = (int)(rng_next() % (uint32_t)(i + 1));
        t = dond.amounts[i];
        dond.amounts[i] = dond.amounts[j];
        dond.amounts[j] = t;
    }
}

void dond_init(uint32_t seed)
{
    dond_new_game(seed);
    dond.phase = DOND_TITLE;
    dond.resume = DOND_TITLE;
}

void dond_new_game(uint32_t seed)
{
    int i;
    dond.rng = seed ? seed : 0xC0FFEEu;
    shuffle_amounts();
    for (i = 0; i < DOND_NCASES; i++)
        dond.opened[i] = 0;
    dond.yours = -1;
    dond.round = 0;
    dond.need_open = (uint8_t)dond_round_quota(0);
    dond.offers = 0;
    dond.offer_cents = 0;
    dond.win_cents = 0;
    dond.last_opened = -1;
    dond.last_amount = 0;
    dond.took_deal = 0;
    dond.swapped = 0;
    dond.resume = DOND_PICK;
    dond.phase = DOND_PICK;
}

int dond_board_left(void)
{
    int i, n = 0;
    for (i = 0; i < DOND_NCASES; i++)
        if (i != dond.yours && !dond.opened[i])
            n++;
    return n;
}

uint32_t dond_closed_mask(void)
{
    uint32_t m = 0;
    int i;
    for (i = 0; i < DOND_NCASES; i++)
        if (!dond.opened[i] && i != dond.yours)
            m |= 1u << i;
    if (dond.yours >= 0 && !dond.opened[dond.yours])
        m |= 1u << dond.yours;
    return m;
}

uint32_t dond_your_amount(void)
{
    if (dond.yours < 0)
        return 0;
    return dond.amounts[dond.yours];
}

uint32_t dond_other_amount(void)
{
    int i;
    for (i = 0; i < DOND_NCASES; i++)
        if (i != dond.yours && !dond.opened[i])
            return dond.amounts[i];
    return 0;
}

uint32_t dond_calc_offer(void)
{
    /* Ladder sum fits in uint32 (≈ $3.4M). Avoid 64-bit div (no __udivdi3). */
    uint32_t sum = 0, avg, offer;
    int i, n = 0;
    unsigned factor_pct;
    for (i = 0; i < DOND_NCASES; i++) {
        if (dond.opened[i])
            continue;
        sum += dond.amounts[i];
        n++;
    }
    if (n <= 0)
        return 0;
    avg = sum / (uint32_t)n;
    /* Climb from ~35% EV toward ~92% as offers accumulate. */
    factor_pct = 35u + (unsigned)dond.offers * 8u;
    if (factor_pct > 92u)
        factor_pct = 92u;
    /* avg <= 1e8; divide first so avg * factor stays in uint32. */
    offer = (avg / 100u) * factor_pct;
    if (offer < 1u)
        offer = 1u;
    /* Round to friendly display amounts. */
    if (offer >= 10000000u) /* >= $100,000 */
        offer = (offer / 100000u) * 100000u;
    else if (offer >= 1000000u) /* >= $10,000 */
        offer = (offer / 10000u) * 10000u;
    else
        offer = (offer / 100u) * 100u;
    if (offer < 100u)
        offer = 100u;
    return offer;
}

static void enter_banker(void)
{
    dond.offer_cents = dond_calc_offer();
    dond.offers++;
    dond.phase = DOND_BANKER;
}

int dond_start(void)
{
    if (dond.phase != DOND_TITLE)
        return 0;
    dond.phase = DOND_PICK;
    return 1;
}

int dond_pick_case(int case_i)
{
    if (dond.phase != DOND_PICK)
        return 0;
    if (case_i < 0 || case_i >= DOND_NCASES)
        return 0;
    dond.yours = (int8_t)case_i;
    dond.round = 0;
    dond.need_open = (uint8_t)dond_round_quota(0);
    dond.phase = DOND_OPEN;
    return 1;
}

int dond_open_case(int case_i)
{
    if (dond.phase != DOND_OPEN)
        return 0;
    if (case_i < 0 || case_i >= DOND_NCASES)
        return 0;
    if (case_i == dond.yours || dond.opened[case_i])
        return 0;
    dond.opened[case_i] = 1;
    dond.last_opened = (int8_t)case_i;
    dond.last_amount = dond.amounts[case_i];
    if (dond.need_open)
        dond.need_open--;
    dond.phase = DOND_FLASH;
    return 1;
}

void dond_flash_done(void)
{
    if (dond.phase != DOND_FLASH)
        return;
    if (dond.need_open == 0) {
        enter_banker();
        return;
    }
    dond.phase = DOND_OPEN;
}

int dond_deal(void)
{
    if (dond.phase != DOND_BANKER)
        return 0;
    dond.took_deal = 1;
    dond.win_cents = dond.offer_cents;
    dond.phase = DOND_DEAL_END;
    return 1;
}

int dond_no_deal(void)
{
    int left;
    if (dond.phase != DOND_BANKER)
        return 0;
    left = dond_board_left();
    if (left <= 1) {
        dond.phase = DOND_FINAL;
        return 1;
    }
    dond.round++;
    dond.need_open = (uint8_t)dond_round_quota((int)dond.round);
    if (dond.need_open > (uint8_t)left)
        dond.need_open = (uint8_t)left;
    dond.phase = DOND_OPEN;
    return 1;
}

static void finish_reveal(uint32_t amount)
{
    dond.win_cents = amount;
    dond.phase = DOND_REVEAL;
}

int dond_keep(void)
{
    if (dond.phase != DOND_FINAL && dond.phase != DOND_DEAL_END)
        return 0;
    if (dond.phase == DOND_DEAL_END) {
        dond.phase = DOND_REVEAL;
        return 1;
    }
    dond.swapped = 0;
    finish_reveal(dond_your_amount());
    return 1;
}

int dond_swap(void)
{
    if (dond.phase != DOND_FINAL)
        return 0;
    dond.swapped = 1;
    finish_reveal(dond_other_amount());
    return 1;
}

int dond_good_outcome(void)
{
    if (dond.phase != DOND_REVEAL)
        return 0;
    if (dond.took_deal)
        return dond.win_cents >= dond_your_amount();
    return dond.win_cents >= (dond.swapped ? dond_your_amount() : dond_other_amount());
}

void dond_begin_confirm(void)
{
    if (dond.phase == DOND_CONFIRM)
        return;
    dond.resume = dond.phase;
    dond.phase = DOND_CONFIRM;
}

void dond_cancel_confirm(void)
{
    if (dond.phase != DOND_CONFIRM)
        return;
    dond.phase = dond.resume;
}

void dond_confirm_yes(uint32_t seed)
{
    dond_new_game(seed);
}
