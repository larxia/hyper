/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca: the SEQ layer's actions (ui_layer.c: SEQ held, on every page). Included by ui_layer.c. (Dah) Only two, on
 * the selected track (the tools of 1.2 .. 1.5.5 gone: REVERSE, SHIFT, RANDOM, COOK, BEAT, EUCLID, DOUBLE, HALF and
 * the DRUM lane's):
 *   QUANTIZE   the PRESETS knob its amount (the track's P_SQNT: OFF 25 % 50 % 75 % ON, as it plays at once, seq.c
 *              seq_nudge); OCT+ APPLY writes the timing as it plays into the steps for good (each NUDGE scaled, ON: all
 *              gone) and QUANTIZE OFF (the steps play as written)
 *   CLEAR ALL  F3: the dialog (CLEAR PATTERN n?), OCT+ empties the pattern: every step and its automation (events and
 *              locks); its number, LEN and place in the song stay
 * KNOB 1..4 LEN DIV SWING GATE (ui_layer.c) */
static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX); }

/* QUANTIZE: the amount turned (PRESETS; OFF 25 % 50 % 75 % ON, the ends held), said */
static const uint8_t QZ_ORD[5] = {0, 2, 3, 4, 1};       /* (P_SQNT: 0 OFF, 1 ON, 2..4 25 % .. 75 %) */
static uint32_t qz_rank(const track_t *t)
{
    uint32_t o;
    for (o = 0; o < 5u && QZ_ORD[o] != (uint32_t)t->p[P_SQNT]; o++)
        ;
    return o < 5u ? o : 0u;
}
static void qz_turn(track_t *t, int32_t s)
{
    t->p[P_SQNT] = (int16_t)QZ_ORD[clamp((int32_t)qz_rank(t) + (s > 0 ? 1 : -1), 0, 4)];
    ui_say("QUANTIZE ", N_QNT[clamp(t->p[P_SQNT], 0, 4)]);
}
/* the steps inside LEN with a NUDGE (a note step) */
static uint32_t qz_nudged(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < tl_len(t); i++)
        n += step_nudge(ui_st(t, i)) != 0 && st_kind(ui_st(t, i)) == ST_NOTE;
    return n;
}
/* OCT+ APPLY: each NUDGE as it plays now written, QUANTIZE OFF; 0 = nothing to do (said why) */
static int qz_apply(track_t *t)
{
    uint32_t len = tl_len(t), i, n = 0;
    char b[24];
    if (!t->p[P_SQNT]) {
        ui_message("QUANTIZE IS OFF");
        return 0;
    }
    if (!qz_nudged(t)) {
        ui_message("NOTHING TO QUANTIZE");
        return 0;
    }
    for (i = 0; i < len; i++) {
        int32_t v = step_nudge(ui_st(t, i));
        if (!v)
            continue;
        step_set_nudge(ed_st(t, i), t->p[P_SQNT] == 1 ? 0 : v * (5 - t->p[P_SQNT]) / 4);
        n++;
    }
    t->p[P_SQNT] = 0;
    str_cpy(b, "QUANTIZED ", sizeof b);
    fmt_int(b + str_len(b), (int32_t)n);
    str_cpy(b + str_len(b), " STEPS", sizeof b - str_len(b));
    ui_message(b);
    return 1;
}
/* CLEAR ALL (the dialog's OCT+): every step of track t and its automation gone, the pattern kept */
static void tl_clear_all(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    pg_del(trk_index(t), ps_num(trk_index(t)));       /* (its other pages) */
    motion_clear(t);
    t->nheld = 0;
    ui_message("PATTERN CLEARED");
    ui.force = 1;
}

/* HYPER BEATS (SEQ + OCT+ on a DRUM track): the next factory beat written onto track t, four bars (LEN 64, DIV 1/16),
 * the pattern and its automation cleared first. Each press the next, round:
 *   HYPERPOP    the kick hard and skipping (1, 8, 11), snare and clap on 2 and 4, 8th hats with 16th bursts, a roll
 *               into the next bar
 *   DANCE-POP   four on the floor, the clap on 2 and 4, open hats on the off-beats, soft 16th hats
 *   HALF-TIME   the kick on 1, the snare on 3, 8th hats with triplet bursts
 *   ELECTROPOP  kick 1, 7, 11, snare on 2 and 4, 16th hats accented on the off-beat 8ths, the impact (BELL) on bar 1
 *   DIGICORE    trap: kicks 1 and 11 and a pickup, the snare on 3, 16th hats rolling (x3, x4), open hat on the last
 *   GLITCH      broken: kicks 1, 4, 11; snare 5 and 13 with x2, clicks (RIM) on odd 16ths, hats in bursts
 * Velocities and ratchets set; lanes: KICK SNARE CLAP CH OH TOM RIM BELL (eng_drum.c DRUM_LANE_NOTE) */
#define HB_N 6u
static const char *const HB_NAME[HB_N] = {"HYPERPOP", "DANCE-POP", "HALF-TIME", "ELECTROPOP", "DIGICORE", "GLITCH"};
static uint8_t hx_beat_next;
static void hb_hit(track_t *t, uint32_t s, uint32_t lanes, uint32_t vel)
{
    step_t *x = &t->step[s % NSTEP];
    x->hit |= (uint8_t)lanes;
    x->time = ST_NOTE;
    if (vel > x->vel)
        x->vel = (uint8_t)vel;
}
static void hb_rat(track_t *t, uint32_t s, uint32_t r) { step_set_ratchet(&t->step[s % NSTEP], r); }
static void hx_beat_write(track_t *t)
{
    uint32_t k = hx_beat_next++ % HB_N, b, i;
    char msg[40];
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    pg_del(trk_index(t), ps_num(trk_index(t)));
    motion_clear(t);
    t->nheld = 0;
    t->p[P_SLEN] = 64;
    t->p[P_SDIV] = 2;
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 0:                                             /* HYPERPOP */
            hb_hit(t, s0, 1u << 0, 127); hb_hit(t, s0 + 7u, 1u << 0, 96); hb_hit(t, s0 + 10u, 1u << 0, 112);
            hb_hit(t, s0 + 4u, 1u << 1 | 1u << 2, 120); hb_hit(t, s0 + 12u, 1u << 1 | 1u << 2, 124);
            for (i = 0; i < 16u; i += 2u)
                hb_hit(t, s0 + i, 1u << 3, i % 4u ? 60 : 78);
            hb_rat(t, s0 + 6u, 2);
            hb_rat(t, s0 + 14u, b == 3u ? 4u : 2u);
            if (b & 1u)
                hb_hit(t, s0 + 14u, 1u << 4, 76);
            if (b == 3u)
                for (i = 12; i < 16u; i++) {
                    hb_hit(t, s0 + i, 1u << 1, 70 + (i - 12u) * 16u);
                    hb_rat(t, s0 + i, i - 11u);
                }
            break;
        case 1:                                             /* DANCE-POP */
            for (i = 0; i < 16u; i += 4u)
                hb_hit(t, s0 + i, 1u << 0, 124);
            hb_hit(t, s0 + 4u, 1u << 2, 116);
            hb_hit(t, s0 + 12u, 1u << 2 | 1u << 1, 120);
            for (i = 2; i < 16u; i += 4u)
                hb_hit(t, s0 + i, 1u << 4, 84);
            for (i = 1; i < 16u; i += 2u)
                hb_hit(t, s0 + i, 1u << 3, 52);
            break;
        case 2:                                             /* HALF-TIME */
            hb_hit(t, s0, 1u << 0, 124);
            hb_hit(t, s0 + 8u, 1u << 1 | 1u << 2, 122);
            for (i = 0; i < 16u; i += 2u)
                hb_hit(t, s0 + i, 1u << 3, i & 2u ? 56 : 72);
            hb_rat(t, s0 + 6u, 3);
            hb_rat(t, s0 + 14u, 3);
            break;
        case 3:                                             /* ELECTROPOP */
            hb_hit(t, s0, 1u << 0, 124); hb_hit(t, s0 + 6u, 1u << 0, 100); hb_hit(t, s0 + 10u, 1u << 0, 110);
            hb_hit(t, s0 + 4u, 1u << 1, 116);
            hb_hit(t, s0 + 12u, 1u << 1 | 1u << 2, 122);
            for (i = 0; i < 16u; i++)
                hb_hit(t, s0 + i, 1u << 3, i % 4u == 2u ? 84 : 48);
            if (!b)
                hb_hit(t, 0, 1u << 7, 110);
            break;
        case 4:                                             /* DIGICORE */
            hb_hit(t, s0, 1u << 0, 127); hb_hit(t, s0 + 10u, 1u << 0, 110);
            if (b & 1u)
                hb_hit(t, s0 + 15u, 1u << 0, 90);
            hb_hit(t, s0 + 8u, 1u << 1 | 1u << 2, 124);
            for (i = 0; i < 16u; i++)
                hb_hit(t, s0 + i, 1u << 3, i % 2u ? 50 : 70);
            hb_rat(t, s0 + 3u, 3);
            hb_rat(t, s0 + 11u, b == 3u ? 4u : 3u);
            hb_hit(t, s0 + 14u, 1u << 4, 80);
            break;
        default:                                            /* GLITCH */
            hb_hit(t, s0, 1u << 0, 127); hb_hit(t, s0 + 3u, 1u << 0, 100); hb_hit(t, s0 + 10u, 1u << 0, 112);
            hb_hit(t, s0 + 4u, 1u << 1, 116); hb_hit(t, s0 + 12u, 1u << 1, 120);
            hb_rat(t, s0 + 12u, 2);
            for (i = 1; i < 16u; i += 2u)
                if ((i + b) % 3u)
                    hb_hit(t, s0 + i, 1u << 6, 60 + i * 3u);
            hb_hit(t, s0 + 6u, 1u << 3, 80); hb_rat(t, s0 + 6u, 4);
            hb_hit(t, s0 + 14u, 1u << 3, 76); hb_rat(t, s0 + 14u, 3);
            break;
        }
    }
    str_cpy(msg, HB_NAME[k], sizeof msg);
    str_cpy(msg + str_len(msg), " BEAT", sizeof msg - str_len(msg));
    ui_message(msg);
    ui.force = 1;
}

/* HYPER CHORDS (SEQ + OCT+): the next mood's progression written onto track t, four bars of a chord each, held (TIEs),
 * voiced to move the least (hyper.c hx_voice) over the track's ROOT; the track's SCALE set to the mood's, LEN 64, DIV
 * 1/16; the pattern's other steps and automation cleared first (as CLEAR ALL). Not on a DRUM track. Each press the
 * next mood (EUPHORIC .. MELANCHOLIC, round), said. On a DRUM track: the factory beats (hx_beat_write) */
static uint8_t hx_mood_next;
static void hx_chords_write(track_t *t)
{
    uint32_t m = hx_mood_next++ % HX_NMOOD, b, i, n = 0, root = (uint32_t)clamp(t->p[P_ROOT], 0, 11);
    uint8_t v[4] = {0, 0, 0, 0};
    char msg[40];
    if (drum_track(t)) {                                /* (a DRUM track: the factory beats instead) */
        hx_beat_write(t);
        return;
    }
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    pg_del(trk_index(t), ps_num(trk_index(t)));
    motion_clear(t);
    t->nheld = 0;
    t->p[P_SLEN] = 64;
    t->p[P_SDIV] = 2;                                   /* 1/16 */
    t->p[P_SCALE] = HX_MOOD_SCALE[m];
    for (b = 0; b < 4u; b++) {
        step_t *s = &t->step[b * 16u];
        n = hx_voice(m, b, root, v, n, v);
        for (i = 0; i < n; i++)
            s->note[i] = v[i];
        s->n = (uint8_t)n;
        s->time = ST_NOTE;
        s->vel = (uint8_t)(b ? 88 : 96);
        for (i = 1; i < 16u; i++)
            t->step[b * 16u + i].time = ST_TIE;
    }
    str_cpy(msg, HX_MOOD[m], sizeof msg);
    str_cpy(msg + str_len(msg), " CHORDS", sizeof msg - str_len(msg));
    ui_message(msg);
    ui.force = 1;
}
