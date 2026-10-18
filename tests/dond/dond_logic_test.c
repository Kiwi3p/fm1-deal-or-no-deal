/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/src/app/dond.h"
#include "../../firmware/src/app/dond.c"

static void test_ladder_unique(void)
{
    int i, j;
    for (i = 0; i < DOND_NCASES; i++)
        for (j = i + 1; j < DOND_NCASES; j++)
            assert(DOND_LADDER[i] != DOND_LADDER[j]);
    assert(DOND_LADDER[0] == 1u);
    assert(DOND_LADDER[DOND_NCASES - 1] == 100000000u);
}

static void test_fmt(void)
{
    char b[32];
    assert(!strcmp(dond_fmt_money(1u, b, sizeof b), "$0.01"));
    assert(!strcmp(dond_fmt_money(100u, b, sizeof b), "$1"));
    assert(!strcmp(dond_fmt_money(100000u, b, sizeof b), "$1,000"));
    assert(!strcmp(dond_fmt_money(100000000u, b, sizeof b), "$1,000,000"));
}

static void test_round_quotas(void)
{
    assert(dond_round_quota(0) == 6);
    assert(dond_round_quota(1) == 5);
    assert(dond_round_quota(2) == 4);
    assert(dond_round_quota(3) == 3);
    assert(dond_round_quota(4) == 2);
    assert(dond_round_quota(5) == 1);
    assert(dond_round_quota(20) == 1);
}

static void test_shuffle_preserves_set(void)
{
    int i, j, found;
    dond_new_game(0x12345678u);
    for (i = 0; i < DOND_NCASES; i++) {
        found = 0;
        for (j = 0; j < DOND_NCASES; j++)
            if (dond.amounts[j] == DOND_LADDER[i])
                found = 1;
        assert(found);
    }
}

static void open_n(int n)
{
    int i, opened = 0;
    for (i = 0; i < DOND_NCASES && opened < n; i++) {
        if (i == dond.yours || dond.opened[i])
            continue;
        assert(dond_open_case(i));
        dond_flash_done();
        opened++;
    }
}

static void test_full_to_banker(void)
{
    dond_new_game(99u);
    assert(dond.phase == DOND_PICK);
    assert(dond_pick_case(3));
    assert(dond.yours == 3);
    assert(dond.phase == DOND_OPEN);
    assert(dond.need_open == 6);
    open_n(6);
    assert(dond.phase == DOND_BANKER);
    assert(dond.offers == 1);
    assert(dond.offer_cents > 0);
    assert(dond_board_left() == 19);
}

static void test_deal(void)
{
    dond_new_game(7u);
    dond_pick_case(0);
    open_n(6);
    assert(dond_deal());
    assert(dond.took_deal);
    assert(dond.phase == DOND_DEAL_END);
    assert(dond.win_cents == dond.offer_cents);
}

static void test_no_deal_advances(void)
{
    dond_new_game(11u);
    dond_pick_case(1);
    open_n(6);
    assert(dond_no_deal());
    assert(dond.phase == DOND_OPEN);
    assert(dond.need_open == 5);
}

static void test_offer_bounds(void)
{
    uint32_t offer;
    dond_new_game(42u);
    dond_pick_case(5);
    open_n(6);
    offer = dond.offer_cents;
    assert(offer >= 100u);
    assert(offer <= 100000000u);
}

static void test_title_does_not_pick(void)
{
    dond_init(5u);
    assert(dond.phase == DOND_TITLE);
    assert(!dond_pick_case(2));
    assert(dond.yours == -1);
    assert(dond_start());
    assert(dond.phase == DOND_PICK);
    assert(dond_pick_case(2));
    assert(dond.yours == 2);
}

static void test_deal_reveal_verdict(void)
{
    dond_new_game(7u);
    dond_pick_case(0);
    open_n(6);
    dond.offer_cents = dond_your_amount() + 1u;   /* the banker paid more than the case held */
    assert(dond_deal());
    assert(dond_keep());
    assert(dond.phase == DOND_REVEAL);
    assert(dond_good_outcome());
    dond_new_game(7u);
    dond_pick_case(0);
    open_n(6);
    dond.offer_cents = 0u;                        /* the case held more than the deal */
    assert(dond_deal());
    assert(dond_keep());
    assert(dond_good_outcome() == (dond_your_amount() == 0u));
}

int main(void)
{
    test_title_does_not_pick();
    test_deal_reveal_verdict();
    test_ladder_unique();
    test_fmt();
    test_round_quotas();
    test_shuffle_preserves_set();
    test_full_to_banker();
    test_deal();
    test_no_deal_advances();
    test_offer_bounds();
    puts("dond_logic_test: ok");
    return 0;
}
