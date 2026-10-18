/* SPDX-License-Identifier: GPL-3.0-only */
/* Deal or No Deal Phase 1: full rules, simple text UI, key = case. */
/* Host script key aliases (w0../b0..); kept for omni_host after this include. */
static const uint8_t WHITE_K[16] __attribute__((unused)) = {
    0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] __attribute__((unused)) = {
    1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

#define PAPER RGB(236, 232, 220)
#define INK RGB(20, 24, 34)
#define GOLD RGB(232, 176, 48)
#define MINT RGB(72, 200, 140)
#define RED RGB(220, 72, 64)
#define BIG_CUTOFF 2500000u /* $25,000+ counts as a "big" sting */

#define BOARD_MS 2800u      /* how long the full board stays up at its set points */
#define FLASH_MS 2400u      /* a case reveal: the big case opens, then the board shows the knockout */
#define OPEN_ANIM_MS 1000u  /* big case: closed, cracking, open (a third each up to LID_OPEN_MS) */
#define LID_OPEN_MS 660u

/* 16-bit scenes, host and board font, generated from scratch by tools/gen16_art.py */
#include "gen16_art.h"
/* the soundtrack's song ids (tools/gen16_music.py; the data itself lives in dsp/gen16.c) */
#include "gen16_music.h"
/* the host's robot voice clip ids (tools/gen16_voice.py) */
#include "gen16_voice.h"
#if DOND_ART
#define SCENE_H 160         /* scene 0..159, info strip 160..239 */
#else
#define SCENE_H 156         /* Phase 1: header 0..23, canvas 24..179, footer 180..239 */
#endif

static struct {
    uint32_t keys, buttons, last_frame, flash_until, deadline, talk_until, board_until;
    uint32_t flash_start, reveal_start;
    uint8_t stung;          /* the lid sting of the current reveal has played */
    uint8_t board, confirm_yes, redraw;
    enum dond_phase_t seen_phase;
    const char *message;
    char sub[32];
    char money[20];
} ui;

static uint32_t button_lights(void)
{
    uint32_t m = (1u << B_GLO) | (1u << B_HOME);
    if (dond.phase == DOND_BANKER || dond.phase == DOND_FINAL)
        m |= (1u << B_PLAY) | (1u << B_SEL);
    else if (dond.phase == DOND_CONFIRM)
        m |= (1u << B_SEL);
    else if (dond.phase == DOND_DEAL_END || dond.phase == DOND_REVEAL || dond.phase == DOND_TITLE)
        m |= (1u << B_PLAY);
    return m;
}

static uint32_t key_lights(void)
{
    uint32_t m;
    if (dond.phase == DOND_TITLE || dond.phase == DOND_PICK)
        m = (1u << DOND_NCASES) - 1u;
    else
        m = dond_closed_mask();
    return m & ~((1u << DOND_UNUSED_KEY));
}

static void centered(int y, const felucca_font_t *font, const char *s, uint16_t c)
{
    char fit[48];
    int n = 0;
    if (!s)
        s = "";
    while (s[n] && n < 47) {
        fit[n] = s[n];
        n++;
    }
    fit[n] = 0;
    while (n && text_w(font, fit) > 224)
        fit[--n] = 0;
    cv_text((240 - text_w(font, fit)) / 2, y, font, fit, c);
}

int ui_dirty(void) { return 0; }

void ui_say(const char *a, const char *b)
{
    int i;
    ui.message = a;
    for (i = 0; i < 31 && b && b[i]; i++)
        ui.sub[i] = b[i];
    ui.sub[i] = 0;
    ui.deadline = plat_ms() + 1800u;
    ui.redraw = 1;
}

void ui_init(void)
{
    uint32_t now = plat_ms();
    ui.keys = ui.buttons = 0;
    ui.message = 0;
    ui.sub[0] = 0;
    ui.board = ui.confirm_yes = 0;
    ui.redraw = 1;
    ui.seen_phase = dond.phase;
    ui.last_frame = now - 84u;
    ui.flash_until = now;
    ui.deadline = now;
    palette_set(1); /* AMBER */
    om_music_bed(SONG_THEME);
    plat_glow(0);
    plat_play_red(0);
    plat_leds(button_lights(), key_lights());
}

/* ---- the host's voice: lines are clip ids, offers and winnings are stitched from number words */
static const uint8_t ONES_W[20] = {0, W_1, W_2, W_3, W_4, W_5, W_6, W_7, W_8, W_9, W_10, W_11, W_12,
                                   W_13, W_14, W_15, W_16, W_17, W_18, W_19};
static const uint8_t TENS_W[10] = {0, 0, W_20, W_30, W_40, W_50, W_60, W_70, W_80, W_90};

static int words_999(uint8_t *o, int n, uint32_t v)
{
    if (v >= 100u) {
        o[n++] = ONES_W[v / 100u];
        o[n++] = W_HUNDRED;
        v %= 100u;
    }
    if (v >= 20u) {
        o[n++] = TENS_W[v / 10u];
        v %= 10u;
    }
    if (v)
        o[n++] = ONES_W[v];
    return n;
}

/* "fifty three thousand six hundred dollars"; nothing under a dollar */
static int words_money(uint8_t *o, int n, uint32_t cents)
{
    uint32_t d = cents / 100u;
    if (!d)
        return n;
    if (d >= 1000000u) {
        n = words_999(o, n, d / 1000000u);
        o[n++] = W_MILLION;
        d %= 1000000u;
    }
    if (d >= 1000u) {
        n = words_999(o, n, d / 1000u);
        o[n++] = W_THOUSAND;
        d %= 1000u;
    }
    n = words_999(o, n, d);
    o[n++] = W_DOLLARS;
    return n;
}

/* what the host says on arriving in a phase (from prev) */
static int phase_lines(uint8_t *o, enum dond_phase_t prev)
{
    int n = 0;
    switch (dond.phase) {
    case DOND_PICK:
        if (prev == DOND_TITLE)
            o[n++] = VO_WELCOME;
        o[n++] = VO_PICK;
        break;
    case DOND_OPEN:
        if (prev == DOND_PICK)
            o[n++] = VO_YOURCASE;
        else if (prev == DOND_BANKER)
            o[n++] = VO_NODEAL;
        if ((prev == DOND_PICK || prev == DOND_BANKER) && dond.need_open >= 1 && dond.need_open <= 6)
            o[n++] = (uint8_t)(VO_OPEN_1 + dond.need_open - 1);
        break;
    case DOND_BANKER:
        o[n++] = VO_CALLING;
        o[n++] = VO_OFFERS;
        n = words_money(o, n, dond.offer_cents);
        o[n++] = VO_DOND;
        break;
    case DOND_DEAL_END:
        o[n++] = VO_DEAL;
        break;
    case DOND_FINAL:
        if (prev == DOND_BANKER)
            o[n++] = VO_NODEAL;
        o[n++] = VO_TWO_LEFT;
        break;
    default:
        break;
    }
    return n;
}

/* the looping bed under each phase; FLASH and CONFIRM keep whatever is playing */
static int phase_bed(enum dond_phase_t ph)
{
    switch (ph) {
    case DOND_TITLE:
        return SONG_THEME;
    case DOND_PICK:
        return SONG_BED;
    case DOND_OPEN:
        return SONG_TENSION;
    case DOND_BANKER:
    case DOND_FINAL:
        return SONG_DECISION;
    case DOND_DEAL_END:
    case DOND_REVEAL:
        return -1;
    default:
        return -2;
    }
}

static void watch_phase_sfx(void)
{
    uint32_t t = plat_ms();
    uint8_t line[24];
    int n;
    if (ui.seen_phase != dond.phase) {
        uint32_t now = t;
        int bed = phase_bed(dond.phase);
        if (bed != -2)
            om_music_bed(bed);
        n = phase_lines(line, ui.seen_phase);
        if (n)
            om_voice_say(line, n);
        /* the full board at its set points: after the pick, before every offer, at the last two */
        if ((dond.phase == DOND_OPEN && ui.seen_phase == DOND_PICK) || dond.phase == DOND_BANKER ||
            dond.phase == DOND_FINAL)
            ui.board_until = now + BOARD_MS;
        if (dond.phase == DOND_REVEAL) {
            ui.reveal_start = now;
            ui.stung = 0;
        }
        if (dond.phase == DOND_BANKER)
            om_music_sting(SONG_PHONE);
    }
    /* the sting lands when the lid opens: a chime (small amount) or a low hit (big), win or lose */
    if (!ui.stung && dond.phase == DOND_FLASH && t - ui.flash_start >= LID_OPEN_MS) {
        int big = dond.last_amount >= BIG_CUTOFF, alt = dond.last_opened & 1;
        om_music_sting(big ? SONG_OUCH : SONG_GOOD);
        line[0] = (uint8_t)(big ? (alt ? VO_OUCH : VO_HURTS) : (alt ? VO_NICE : VO_GREAT_PICK));
        om_voice_say(line, 1);
        ui.stung = 1;
    }
    if (!ui.stung && dond.phase == DOND_REVEAL && t - ui.reveal_start >= LID_OPEN_MS) {
        int good = dond_good_outcome();
        om_music_sting(good ? SONG_WIN : SONG_LOSE);
        line[0] = (uint8_t)(dond.took_deal ? (good ? VO_GOOD_DEAL : VO_BAD_DEAL)
                                           : (good ? VO_WELL_CHOSEN : VO_WRONG_CASE));
        line[1] = VO_YOU_WIN;
        n = words_money(line, 2, dond.win_cents);
        om_voice_say(line, n);
        ui.stung = 1;
    }
    ui.seen_phase = dond.phase;
}

/* "#7" / "#26" */
static char *case_label(char *buf, int k)
{
    int n = k + 1, i = 0;
    buf[i++] = '#';
    if (n >= 10)
        buf[i++] = (char)('0' + n / 10);
    buf[i++] = (char)('0' + n % 10);
    buf[i] = 0;
    return buf;
}

/* a then b into buf (n bytes) */
static char *join(char *buf, unsigned n, const char *a, const char *b)
{
    unsigned i = 0;
    while (*a && i + 1 < n)
        buf[i++] = *a++;
    while (*b && i + 1 < n)
        buf[i++] = *b++;
    buf[i] = 0;
    return buf;
}

/* "ROUND 2 - 19 CASES LEFT": where the player is, so a round never feels endless */
static char *round_line(char *buf, unsigned n)
{
    char r[12] = "ROUND ", t[24];
    int i = 6, left = dond_board_left(), rd = dond.round + 1;
    if (rd >= 10)
        r[i++] = (char)('0' + rd / 10 % 10);
    r[i++] = (char)('0' + rd % 10);
    r[i] = 0;
    i = 0;
    t[i++] = ' ';
    t[i++] = '-';
    t[i++] = ' ';
    if (left >= 10)
        t[i++] = (char)('0' + left / 10);
    t[i++] = (char)('0' + left % 10);
    join(t + i, sizeof t - (unsigned)i, left == 1 ? " CASE LEFT" : " CASES LEFT", "");
    return join(buf, n, r, t);
}

/* the end of the game, as on the show: what your case held and whether you chose well */
static void reveal_text(const char **title, uint32_t *amount, char *other, unsigned n, const char **verdict)
{
    char m[20];
    int good = dond_good_outcome();
    if (dond.took_deal) {
        *title = "YOUR CASE HAD";
        *amount = dond_your_amount();
        dond_fmt_money(dond.win_cents, m, sizeof m);
        join(other, n, "YOU SOLD IT FOR ", m);
        *verdict = good ? "GOOD DEAL!" : "BAD DEAL!";
    } else {
        *title = dond.swapped ? "YOU SWAPPED AND WIN" : "YOU KEPT IT AND WIN";
        *amount = dond.win_cents;
        dond_fmt_money(dond.swapped ? dond_your_amount() : dond_other_amount(), m, sizeof m);
        join(other, n, dond.swapped ? "YOUR OLD CASE HAD " : "THE OTHER CASE HAD ", m);
        *verdict = good ? "GOOD CHOICE!" : "WRONG CASE!";
    }
}

static int amount_opened(uint32_t cents)
{
    int i;
    for (i = 0; i < DOND_NCASES; i++) {
        if (dond.amounts[i] != cents)
            continue;
        if (i == dond.yours)
            return 0;
        return dond.opened[i];
    }
    return 0;
}

static void draw_money_board(void)
{
    int col, row;
    char buf[20];
    cv_begin(240, SCENE_H, PAPER);
    centered(2, &FONT_XS, "AMOUNT BOARD", INK);
    for (col = 0; col < 2; col++) {
        for (row = 0; row < 13; row++) {
            int idx = col * 13 + row;
            uint32_t cents = DOND_LADDER[idx];
            int gone = amount_opened(cents);
            int y = 16 + row * 10;
            int x = col ? 124 : 8;
            dond_fmt_money(cents, buf, sizeof buf);
            cv_text(x, y, &FONT_XS, buf, gone ? C_DIM : (cents >= BIG_CUTOFF ? GOLD : INK));
        }
    }
    cv_blit(0, DOND_ART ? 0 : 24);
}

#if DOND_ART
static void fmt_open_more(char *line);

static int board_up(uint32_t now)
{
    return dond.phase != DOND_CONFIRM && (ui.board || (int32_t)(now - ui.board_until) < 0);
}

static int scene_bg(uint32_t now)
{
    if (board_up(now))
        return DOND_BG_BOARD;
    switch (dond.phase) {
    case DOND_TITLE:
        return DOND_BG_TITLE;
    case DOND_FLASH:              /* the case opens on stage, then the board shows the knockout */
        return now - ui.flash_start < OPEN_ANIM_MS ? DOND_BG_STAGE : DOND_BG_BOARD;
    case DOND_BANKER:
    case DOND_CONFIRM:
        return DOND_BG_BANKER;
    default:                      /* PICK, OPEN, DEAL_END, FINAL, REVEAL */
        return DOND_BG_STAGE;
    }
}

/* the board case still closed at the end (the swap target) */
static int other_case(void)
{
    int i;
    for (i = 0; i < DOND_NCASES; i++)
        if (i != dond.yours && !dond.opened[i])
            return i;
    return -1;
}

/* the case the reveal opens: yours, unless you swapped */
static int reveal_case(void)
{
    return dond.swapped ? other_case() : dond.yours;
}

/* lid state of the big case, t ms after it started: 0 closed, 1 cracking, 2 open */
static int lid(uint32_t t)
{
    return t < LID_OPEN_MS / 2u ? 0 : t < LID_OPEN_MS ? 1 : 2;
}

/* face frame: 0 at rest, a hopping talk frame while talking */
static int host_frame(uint32_t now, int nframes)
{
    if (nframes < 2 || ((int32_t)(now - ui.talk_until) >= 0 && !om_voice_busy()))
        return 0;
    return 1 + (int)((now / 110u) * 7u % (uint32_t)(nframes - 1));
}

static uint32_t text_w57(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n ? n * 6u - 1u : 0u;
}

/* 5 x 7 text in the board font (digits, $ , . and capitals), sc x scale */
static void text57(int32_t x, int32_t y, const char *s, uint16_t c, int32_t sc)
{
    for (; *s; s++, x += 6 * sc) {
        uint32_t ch = (uint8_t)*s, r, b;
        if (ch < 32u || ch > 95u)
            continue;
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (DOND_FONT57[ch - 32u][r] & (16u >> b))
                    cv_rect(x + (int32_t)b * sc, y + (int32_t)r * sc, sc, sc, c);
    }
}

static uint32_t text_w35(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n ? n * 4u - 1u : 0u;
}

/* 3 x 5 text in the case font (digits, K M . $), sc x scale */
static void text35(int32_t x, int32_t y, const char *s, uint16_t c, int32_t sc)
{
    for (; *s; s++, x += 4 * sc) {
        uint32_t ch = (uint8_t)*s, r, b;
        if (ch < 32u || ch > 95u)
            continue;
        for (r = 0; r < 5u; r++)
            for (b = 0; b < 3u; b++)
                if (DOND_FONT35[ch - 32u][r] & (4u >> b))
                    cv_rect(x + (int32_t)b * sc, y + (int32_t)r * sc, sc, sc, c);
    }
}

/* short amount for inside a case: ".01" "750" "25K" "1M" */
static char *case_amount(uint32_t cents, char *buf)
{
    uint32_t d = cents / 100u;
    char t[8], suffix = 0;
    int n = 0, i = 0;
    if (cents < 100u) {
        buf[0] = '.';
        buf[1] = (char)('0' + cents / 10u);
        buf[2] = (char)('0' + cents % 10u);
        buf[3] = 0;
        return buf;
    }
    if (d >= 1000000u) {
        d /= 1000000u;
        suffix = 'M';
    } else if (d >= 1000u) {
        d /= 1000u;
        suffix = 'K';
    }
    do {
        t[n++] = (char)('0' + d % 10u);
        d /= 10u;
    } while (d);
    while (n)
        buf[i++] = t[--n];
    if (suffix)
        buf[i++] = suffix;
    buf[i] = 0;
    return buf;
}

#define CASE_EDGE RGB(0, 0, 36)
#define CASE_HI RGB(219, 219, 219)
#define CASE_MID RGB(182, 182, 182)
#define CASE_LO RGB(109, 109, 146)
#define CASE_GOLD RGB(255, 182, 36)
#define CASE_GOLD_DK RGB(146, 109, 0)
#define CASE_VELVET RGB(109, 0, 36)

/* a briefcase at sc x scale: silver with a gold number plate; open (lid 2) shows the amount on
 * red velvet; lid 1 is the gold crack of light; yours gets a gold edge */
static void draw_case(int32_t x, int32_t y, int32_t sc, int k, int lid_state, int yours)
{
    char b[8];
    int32_t w = DOND_CASE_W * sc, h = DOND_CASE_H * sc;
    uint16_t edge = yours ? RGB(255, 219, 73) : CASE_EDGE;
    if (lid_state == 2) {
        cv_rect(x, y, w, 4 * sc, edge);                               /* lid, flipped up */
        cv_rect(x + sc, y + sc, w - 2 * sc, 2 * sc, CASE_LO);
        cv_rect(x, y + 4 * sc, w, h - 4 * sc, edge);
        cv_rect(x + sc, y + 5 * sc, w - 2 * sc, h - 6 * sc, CASE_VELVET);
        case_amount(dond.amounts[k], b);
        text35(x + (w - (int32_t)text_w35(b) * sc) / 2, y + 7 * sc, b, RGB(255, 219, 73), sc);
        return;
    }
    cv_rect(x + 7 * sc, y, 6 * sc, 3 * sc, edge);                     /* handle */
    cv_rect(x + 8 * sc, y + sc, 4 * sc, sc, CASE_LO);
    cv_rect(x, y + 2 * sc, w, h - 2 * sc, edge);                      /* body */
    cv_rect(x + sc, y + 3 * sc, w - 2 * sc, h - 4 * sc, CASE_MID);
    cv_rect(x + sc, y + 3 * sc, w - 2 * sc, sc, CASE_HI);
    cv_rect(x + sc, y + h - 2 * sc, w - 2 * sc, sc, CASE_LO);
    if (lid_state == 1)
        cv_rect(x + sc, y + 4 * sc, w - 2 * sc, sc, RGB(255, 255, 146));
    cv_rect(x + 3 * sc, y + 4 * sc, 14 * sc, 9 * sc, CASE_GOLD_DK);   /* number plate */
    cv_rect(x + 4 * sc, y + 5 * sc, 12 * sc, 7 * sc, CASE_GOLD);
    case_label(b, k);
    text57(x + (w - ((int32_t)text_w57(b + 1)) * sc) / 2, y + 5 * sc, b + 1, CASE_EDGE, sc);
}

/* all 26 on their tiers (yours on the floor by the host) */
static void draw_stage_cases(void)
{
    int i;
    if (dond.phase == DOND_TITLE)
        return;
    for (i = 0; i < DOND_NCASES; i++)
        if (i != dond.yours)
            draw_case(DOND_CASE_XY[i][0], DOND_CASE_XY[i][1], 1, i, dond.opened[i] ? 2 : 0, 0);
    if (dond.yours >= 0)
        draw_case(DOND_YOUR_X, DOND_YOUR_Y, 1, dond.yours, 0, 1);
}

/* the big case centre stage, t ms into its opening, with sparkles once it is open */
static void draw_big_case(int k, uint32_t t, uint32_t now)
{
    int l = lid(t), i;
    static const int8_t SP[6][2] = {{-10, 6}, {86, 2}, {-6, 50}, {90, 46}, {40, -10}, {20, 66}};
    if (k < 0)
        return;
    cv_rect(40, 26, 120, 84, RGB(0, 0, 36));                         /* spotlight panel */
    cv_rect(42, 28, 116, 80, RGB(36, 0, 73));
    draw_case(60, 40, 4, k, l, k == dond.yours);
    if (l == 2)
        for (i = 0; i < 6; i++)
            if ((now / 150u + (uint32_t)i) % 3u) {
                int32_t x = 60 + SP[i][0], y = 40 + SP[i][1];
                uint16_t c = i % 2 ? RGB(255, 255, 255) : RGB(255, 219, 73);
                cv_rect(x - 1, y, 3, 1, c);
                cv_rect(x, y - 1, 1, 3, c);
            }
}

/* amounts in the board's bars: in play = lit gold, gone = dark, the one just opened blinks */
static void draw_board_bars(uint32_t now)
{
    char buf[16];
    int i;
    for (i = 0; i < DOND_NCASES; i++) {
        uint32_t cents = DOND_LADDER[i];
        int gone = amount_opened(cents), x = i < 13 ? DOND_BOARD_X0 : DOND_BOARD_X1, y = 12 + (i % 13) * 11;
        int fresh = dond.phase == DOND_FLASH && cents == dond.last_amount;
        int32_t w;
        dond_fmt_money(cents, buf, sizeof buf);
        w = (int32_t)text_w57(buf);
        (void)w;
        if (fresh && (now / 180u) % 2u)
            cv_rect(x + 1, y + 1, 74, 7, RED);
        else if (!gone)
            cv_rect(x + 1, y + 1, 74, 7, cents >= BIG_CUTOFF ? RGB(255, 219, 73) : RGB(255, 182, 36));
        text57(x + 38 - w / 2, y + 1, buf, gone && !fresh ? RGB(109, 73, 36) : RGB(0, 0, 36), 1);
    }
}

static void draw_scene(uint32_t now)
{
    int bg = scene_bg(now);
    const dond_place_t *p = &DOND_PLACE[bg];
    cv_begin(240, SCENE_H, INK);
    cv_rle(0, 0, 240, SCENE_H, DOND_BG[bg].pal, DOND_BG[bg].rle, 0);
    if (p->host >= 0) {
        const dond_host_t *h = &DOND_HOST[p->host];
        int f = host_frame(now, h->nframes);
        cv_rle(p->x, p->y, h->base.w, h->base.h, h->base.pal, h->base.rle, 1);
        if (f)
            cv_rle(p->x + h->face.x, p->y + h->face.y, h->face.w, h->face.h, h->base.pal, h->frames[f], 1);
    }
    if (bg == DOND_BG_BOARD)
        draw_board_bars(now);
    else if (bg == DOND_BG_STAGE) {
        draw_stage_cases();
        if (dond.phase == DOND_FLASH)
            draw_big_case(dond.last_opened, now - ui.flash_start, now);
        else if (dond.phase == DOND_REVEAL)
            draw_big_case(reveal_case(), now - ui.reveal_start, now);
        else if (dond.phase == DOND_DEAL_END)
            draw_big_case(dond.yours, 0, now);
    }
    cv_blit(0, 0);
}

/* the 240 x 80 strip under the scene: headline, detail, controls, transient message */
static void draw_strip(void)
{
    const char *head = 0, *detail = 0, *keys = "HOME BOARD  GLO NEW";
    const felucca_font_t *hf = &FONT_S;
    uint16_t hc = PAPER;
    char line[40], a[32], you[8];
    cv_begin(240, 80, INK);
    switch (dond.phase) {
    case DOND_TITLE:
        head = "DEAL OR NO DEAL";
        hc = GOLD;
        detail = "PRESS ANY KEY TO START";
        break;
    case DOND_PICK:
        head = "PICK YOUR CASE";
        detail = "KEYS 1-26: THE ONE YOU KEEP";
        break;
    case DOND_OPEN:
        fmt_open_more(line);
        head = line;
        detail = round_line(a, sizeof a);
        break;
    case DOND_FLASH:
        detail = dond.last_opened >= 0 ? case_label(you, dond.last_opened) : 0;
        if (plat_ms() - ui.flash_start < LID_OPEN_MS) {      /* no spoilers before the lid opens */
            head = "OPENING...";
            break;
        }
        dond_fmt_money(dond.last_amount, ui.money, sizeof ui.money);
        head = ui.money;
        hf = &FONT_M;
        hc = dond.last_amount >= BIG_CUTOFF ? RED : PAPER;
        break;
    case DOND_BANKER:
        dond_fmt_money(dond.offer_cents, ui.money, sizeof ui.money);
        head = ui.money;
        hf = &FONT_M;
        hc = GOLD;
        detail = "THE BANKER OFFERS";
        keys = "PLAY DEAL  SEL NO DEAL";
        break;
    case DOND_DEAL_END:
        dond_fmt_money(dond.win_cents, ui.money, sizeof ui.money);
        head = ui.money;
        hf = &FONT_M;
        hc = MINT;
        detail = "DEAL! NOW OPEN YOUR CASE";
        keys = "PLAY OPEN CASE";
        break;
    case DOND_FINAL:
        head = "FINAL CASE";
        detail = "KEEP YOURS OR SWAP?";
        keys = "PLAY KEEP  SEL SWAP";
        break;
    case DOND_REVEAL: {
        const char *title, *verdict;
        uint32_t amount;
        reveal_text(&title, &amount, line, sizeof line, &verdict);
        detail = title;
        if (plat_ms() - ui.reveal_start < LID_OPEN_MS) {
            head = "OPENING...";
            line[0] = 0;
            break;
        }
        dond_fmt_money(amount, ui.money, sizeof ui.money);
        head = ui.money;
        hf = &FONT_M;
        hc = dond_good_outcome() ? MINT : RED;
        keys = verdict;
        break;
    }
    case DOND_CONFIRM:
        head = "NEW GAME?";
        detail = ui.confirm_yes ? "> YES     NO" : "  YES   > NO";
        keys = "SELECT KNOB  SEL OK";
        break;
    }
    centered(hf == &FONT_M ? 2 : 6, hf, head, hc);
    if (detail)
        centered(30, &FONT_XS, detail, GOLD);
    if (dond.yours >= 0) {
        case_label(you, dond.yours);
        cv_text(8, 48, &FONT_XS, "YOU", C_DIM);
        cv_text(34, 48, &FONT_XS, you, PAPER);
    }
    cv_text(232 - text_w(&FONT_XS, keys), 48, &FONT_XS, keys,
            dond.phase == DOND_REVEAL ? (dond_good_outcome() ? MINT : RED) : C_DIM);
    if (ui.message && (int32_t)(plat_ms() - ui.deadline) < 0)
        centered(64, &FONT_XS, ui.message, MINT);
    else if (dond.phase == DOND_REVEAL)
        centered(64, &FONT_XS, line, PAPER);
    cv_blit(0, 160);
}
#endif

static void draw_header(void)
{
    char right[24];
    cv_begin(240, 24, INK);
    cv_text(8, 5, &FONT_S, "DEAL OR NO DEAL", GOLD);
    if (dond.yours >= 0) {
        right[0] = 'Y';
        right[1] = 'O';
        right[2] = 'U';
        right[3] = ' ';
        right[4] = '#';
        if (dond.yours + 1 >= 10) {
            right[5] = (char)('0' + (dond.yours + 1) / 10);
            right[6] = (char)('0' + (dond.yours + 1) % 10);
            right[7] = 0;
        } else {
            right[5] = (char)('0' + dond.yours + 1);
            right[6] = 0;
        }
        cv_text(232 - text_w(&FONT_XS, right), 7, &FONT_XS, right, PAPER);
    }
    cv_blit(0, 0);
}

static void draw_footer(const char *a, const char *b)
{
    cv_begin(240, 60, INK);
    if (a)
        centered(8, &FONT_XS, a, PAPER);
    if (b)
        centered(28, &FONT_XS, b, GOLD);
    if (ui.message && (int32_t)(plat_ms() - ui.deadline) < 0)
        centered(44, &FONT_XS, ui.message, MINT);
    cv_blit(0, 180);
}

static void fmt_open_more(char *line)
{
    if (dond.need_open >= 10) {
        line[0] = 'O';
        line[1] = 'P';
        line[2] = 'E';
        line[3] = 'N';
        line[4] = ' ';
        line[5] = (char)('0' + dond.need_open / 10);
        line[6] = (char)('0' + dond.need_open % 10);
        line[7] = ' ';
        line[8] = 'M';
        line[9] = 'O';
        line[10] = 'R';
        line[11] = 'E';
        line[12] = 0;
    } else {
        line[0] = 'O';
        line[1] = 'P';
        line[2] = 'E';
        line[3] = 'N';
        line[4] = ' ';
        line[5] = (char)('0' + dond.need_open);
        line[6] = ' ';
        line[7] = 'M';
        line[8] = 'O';
        line[9] = 'R';
        line[10] = 'E';
        line[11] = 0;
    }
}

static void draw_main_play(void)
{
    char line[40];
    cv_begin(240, 156, PAPER);
    switch (dond.phase) {
    case DOND_TITLE:
        centered(40, &FONT_M, "DEAL OR NO DEAL", INK);
        centered(80, &FONT_XS, "PRESS ANY KEY", INK);
        centered(100, &FONT_XS, "TO START", INK);
        break;
    case DOND_PICK:
        centered(48, &FONT_M, "PICK YOUR CASE", INK);
        centered(90, &FONT_XS, "KEYS 1-26: THE ONE YOU KEEP", INK);
        break;
    case DOND_OPEN:
        centered(28, &FONT_S, "OPEN A CASE", INK);
        fmt_open_more(line);
        centered(64, &FONT_XS, line, INK);
        centered(84, &FONT_XS, round_line(line, sizeof line), INK);
        centered(110, &FONT_XS, "LIT KEYS ARE CLOSED", C_DIM);
        break;
    case DOND_FLASH:
        centered(24, &FONT_XS, "CASE OPENED", INK);
        dond_fmt_money(dond.last_amount, ui.money, sizeof ui.money);
        centered(64, &FONT_M, ui.money, dond.last_amount >= BIG_CUTOFF ? RED : INK);
        break;
    case DOND_BANKER:
        centered(16, &FONT_XS, "BANKER OFFER", INK);
        dond_fmt_money(dond.offer_cents, ui.money, sizeof ui.money);
        centered(52, &FONT_M, ui.money, GOLD);
        centered(96, &FONT_XS, "PLAY: DEAL", MINT);
        centered(116, &FONT_XS, "SEL: NO DEAL", RED);
        break;
    case DOND_DEAL_END:
        centered(20, &FONT_S, "DEAL!", MINT);
        dond_fmt_money(dond.win_cents, ui.money, sizeof ui.money);
        centered(60, &FONT_M, ui.money, GOLD);
        centered(110, &FONT_XS, "PLAY: OPEN YOUR CASE", INK);
        break;
    case DOND_FINAL:
        centered(16, &FONT_S, "FINAL CASE", INK);
        centered(48, &FONT_XS, "PLAY: KEEP YOURS", MINT);
        centered(72, &FONT_XS, "SEL: SWAP", RED);
        centered(104, &FONT_XS, "OTHER CASE STILL CLOSED", C_DIM);
        break;
    case DOND_REVEAL: {
        const char *title, *verdict;
        uint32_t amount;
        reveal_text(&title, &amount, line, sizeof line, &verdict);
        centered(16, &FONT_S, title, INK);
        dond_fmt_money(amount, ui.money, sizeof ui.money);
        centered(48, &FONT_M, ui.money, GOLD);
        centered(86, &FONT_XS, line, INK);
        centered(106, &FONT_S, verdict, dond_good_outcome() ? MINT : RED);
        centered(136, &FONT_XS, "GLO: NEW GAME", C_DIM);
        break;
    }
    case DOND_CONFIRM:
        centered(24, &FONT_M, "NEW GAME?", INK);
        centered(70, &FONT_XS, ui.confirm_yes ? "> YES   NO" : "  YES  > NO", GOLD);
        centered(110, &FONT_XS, "SELECT KNOB / SEL", C_DIM);
        break;
    }
    cv_blit(0, 24);
}

void ui_frame(void)
{
    uint32_t now = plat_ms();
    if (dond.phase == DOND_FLASH && (int32_t)(now - ui.flash_until) >= 0) {
        dond_flash_done();
        watch_phase_sfx();
        ui.redraw = 1;
    }
    watch_phase_sfx();
#if DOND_ART
    draw_scene(now);
    draw_strip();
#else
    draw_header();
    if (ui.board && dond.phase != DOND_CONFIRM)
        draw_money_board();
    else
        draw_main_play();
    switch (dond.phase) {
    case DOND_BANKER:
        draw_footer("HOME: BOARD", "PLAY DEAL / SEL NO");
        break;
    case DOND_OPEN:
        draw_footer("HOME: BOARD", "GLO: NEW GAME");
        break;
    case DOND_FINAL:
        draw_footer("PLAY KEEP / SEL SWAP", "GLO: NEW GAME");
        break;
    case DOND_REVEAL:
    case DOND_DEAL_END:
        draw_footer("GLO: NEW GAME", "HOME: BOARD");
        break;
    default:
        draw_footer("HOME: BOARD", "GLO: NEW GAME");
        break;
    }
#endif
    plat_leds(button_lights(), key_lights());
    ui.redraw = 0;
    ui.last_frame = now;
}

static void start_flash(uint32_t now)
{
    ui.flash_until = now + FLASH_MS;
    ui.flash_start = now;
    ui.stung = 0;
    ui.board = 0;
    ui.redraw = 1;
}

void ui_input_only(void)
{
    uint32_t now = plat_ms(), keys = plat_keys(), down = keys & ~ui.keys;
    uint32_t buttons = plat_buttons(), pressed = buttons & ~ui.buttons, pkt;
    int32_t enc;
    int k;
    char num[8];

    for (k = 0; k < 64 && plat_midi_in(&pkt); k++) {
    }

    if ((down || pressed) && (int32_t)(now - ui.board_until) < 0)
        ui.board_until = now;           /* any press skips the board; the press still counts */

    if (dond.phase == DOND_FLASH && (int32_t)(now - ui.flash_until) >= 0) {
        dond_flash_done();
        watch_phase_sfx();
    }

    enc = plat_enc(EN_SELECT);
    if (dond.phase == DOND_CONFIRM && enc) {
        ui.confirm_yes = enc > 0 ? 1 : 0;
        ui.redraw = 1;
    }
    for (k = 0; k < NE; k++)
        if (k != EN_SELECT)
            (void)plat_enc(k);

    if (pressed & (1u << B_HOME)) {
        ui.board = (uint8_t)!ui.board;
        ui.redraw = 1;
    }

    if (pressed & (1u << B_GLO)) {
        if (dond.phase == DOND_CONFIRM)
            dond_cancel_confirm();
        else {
            dond_begin_confirm();
            ui.confirm_yes = 0;
            ui.board = 0;
        }
        ui.redraw = 1;
    }

    if (dond.phase == DOND_CONFIRM) {
        if (pressed & (1u << B_SEL)) {
            if (ui.confirm_yes) {
                om_panic();
                dond_confirm_yes(now ^ 0xBEEFu);
                om_sfx(SFX_CLICK);
                ui_say("NEW GAME", "PICK YOUR CASE");
            } else
                dond_cancel_confirm();
            ui.redraw = 1;
        }
        ui.keys = keys;
        ui.buttons = buttons;
        plat_leds(button_lights(), key_lights());
        return;
    }

    if (dond.phase == DOND_FLASH && (down & ((1u << DOND_NCASES) - 1u))) {
        dond_flash_done();      /* a press during the reveal skips it instead of vanishing */
        watch_phase_sfx();
        ui.redraw = 1;
    }

    if (dond.phase == DOND_BANKER) {
        if (pressed & (1u << B_PLAY)) {
            dond_deal();
            om_music_sting(SONG_DEAL);
            ui.board = 0;
            ui.redraw = 1;
        } else if (pressed & (1u << B_SEL)) {
            dond_no_deal();
            om_music_sting(SONG_NODEAL);
            ui.board = 0;
            ui.redraw = 1;
        }
    } else if (dond.phase == DOND_FINAL) {
        if (pressed & (1u << B_PLAY)) {
            dond_keep();
            ui.redraw = 1;
        } else if (pressed & (1u << B_SEL)) {
            dond_swap();
            ui.redraw = 1;
        }
    } else if (dond.phase == DOND_DEAL_END) {
        if ((pressed & (1u << B_PLAY)) || down) {
            dond_keep();
            ui.redraw = 1;
        }
    } else if (dond.phase == DOND_TITLE) {
        if (down || (pressed & (1u << B_PLAY))) {   /* starting is not choosing: the pick comes next */
            dond_start();
            om_sfx(SFX_CLICK);
            ui.redraw = 1;
        }
    } else if (dond.phase == DOND_PICK || dond.phase == DOND_OPEN) {
        for (k = 0; k < DOND_NCASES; k++) {
            if (!(down & (1u << k)))
                continue;
            if (dond.phase == DOND_PICK) {
                if (dond_pick_case(k)) {
                    om_sfx(SFX_CLICK);
                    ui_say("YOUR CASE", case_label(num, k));
                    ui.redraw = 1;
                }
            } else if (dond_open_case(k)) {
                om_sfx(SFX_CLICK);
                start_flash(now);
            }
            break;
        }
    } else if (dond.phase == DOND_REVEAL) {
        if (pressed & (1u << B_PLAY)) {
            dond_begin_confirm();
            ui.confirm_yes = 1;
            ui.redraw = 1;
        }
    }

    watch_phase_sfx();
    ui.keys = keys;
    ui.buttons = buttons;
    plat_leds(button_lights(), key_lights());
}
