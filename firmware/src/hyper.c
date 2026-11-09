/* SPDX-License-Identifier: GPL-3.0-only
 * HYPER: the hyperpop performance tools that act on the sequence (included by seq.c, before seq_play). Held on the FX
 * layer's white keys (perform.c PF_ROLL PF_CHAOS PF_FILLX; GLITCH and PUNCH are audio effects there):
 *   ROLL   while held, every DRUM track rolls its hats: a closed hat on every step, played twice in it (1/32), the
 *          beat before the bar four times (1/64) with a snare rising under it. Steps with a kick keep their kick
 *          single (no machine-gun kick), the hat added
 *   CHAOS  while held, steps vary: on DRUM tracks a step may double (x2, x3 in the bar's last beat), lose its hats,
 *          swap the closed hat for the open one, or gain a ghost snare off the beat; on the other tracks a note may jump
 *          an octave up, double, or rest. How much: KNOB 4 (DEPTH) while held, untouched 50 %. Controlled: each choice
 *          is a hash of the track, the step and the track's loop count (cond_loop), never rng(): the same loop plays the
 *          same variation every time it comes round, and the downbeat kick is never taken away
 *   FILL   a press arms it; the next bar's last beat (steps 13..16 of the bar, as each track counts its steps) plays
 *          a fill on every DRUM track: snare hits x1 x2 x3 x4 rising, toms on 13 and 15, the hats out; steps marked
 *          FILL (SEQ > DETAIL's conditions) play too. Then it disarms itself
 * Nothing is written into the pattern: seq_play hands seq_step a changed copy of the step (hx_buf), which the step's
 * ratchet repeats read again (seq_ratchet). With none of the three held or armed, every step plays exactly as before
 * (the original pointer). */

#define HX_KICK 0u
#define HX_SNARE 1u
#define HX_CH 3u
#define HX_OH 4u
#define HX_TOM 5u
#define HX_LANE(i) (1u << (i))

static step_t hx_buf[NTRK];                 /* the changed step each track plays now */
static uint16_t hx_at[NTRK];                /* .. its index + 1 (0: the track plays its own step) */
static volatile uint8_t hx_fill;            /* FILL: 0 off, 1 armed, 2 playing (the bar's last beat) */

/* a small integer hash (lowbias32): the same inputs, the same choice */
static uint32_t hx_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
/* CHAOS's amount 0..100: KNOB 4 while the effect is held (perform.c perf_k[3]), untouched 50 */
static uint32_t hx_chaos_amt(void)
{
    int32_t k = perf_k[3];
    return k > 0 ? (uint32_t)(k > 100 ? 100 : k) : 50u;
}
/* the FILL state, once a block. hx_tick before the tracks' steps: a press (perform.c hx_fill_req) arms it; it plays
 * from the first step of a bar's last beat a track plays armed (hx_step); hx_tock after the transport's clock moved
 * (seq.c click_tick): back in the next bar's first three beats, done */
static void hx_tick(void)
{
    if (hx_fill_req) {
        hx_fill_req = 0;
        if (!hx_fill && song.playing)
            hx_fill = 1;
    }
    if (!song.playing)
        hx_fill = 0;
}
static void hx_tock(void)
{
    if (hx_fill == 2u && clk_step < 12u)
        hx_fill = 0;
}
/* the bar's position of step idx (each track counts its own steps; 16 a bar at 1/16) */
static uint32_t hx_pos(uint32_t idx) { return idx & 15u; }

static void hx_roll(step_t *s, uint32_t p)
{
    s->hit |= (uint8_t)HX_LANE(HX_CH);
    if (p >= 12u) {                               /* the beat before the bar: x4, a snare rising */
        s->hit |= (uint8_t)HX_LANE(HX_SNARE);
        s->vel = (uint8_t)(64u + (p - 12u) * 18u);
    } else if (!s->vel) {
        s->vel = 84;
    }
    if ((s->hit >> HX_KICK) & 1u)
        step_set_ratchet(s, 1);                   /* (a kick stays single) */
    else
        step_set_ratchet(s, p >= 12u ? 4u : 2u);
}

static void hx_fill_drum(step_t *s, uint32_t p)
{
    uint32_t q = p - 12u;                         /* 0..3 */
    s->hit &= (uint8_t)~(HX_LANE(HX_CH) | HX_LANE(HX_OH));
    if (q)                                        /* (the kick keeps the beat's first step) */
        s->hit &= (uint8_t)~HX_LANE(HX_KICK);
    s->hit |= (uint8_t)HX_LANE(HX_SNARE);
    if (!(q & 1u))
        s->hit |= (uint8_t)HX_LANE(HX_TOM);
    s->vel = (uint8_t)(72u + q * 18u);
    step_set_ratchet(s, (s->hit >> HX_KICK) & 1u ? 1u : q + 1u);
    s->probability = 0;                           /* (a fill always plays) */
}

static int hx_chaos_drum(step_t *s, uint32_t p, uint32_t h, uint32_t amt)
{
    int kick = (s->hit >> HX_KICK) & 1u, ch = 0;
    if (s->hit && (h & 127u) < amt * 30u / 100u * 128u / 100u && !kick) {      /* double (x3 in the last beat) */
        step_set_ratchet(s, p >= 12u ? 3u : 2u);
        ch = 1;
    }
    h = hx_hash(h);
    if ((s->hit & (HX_LANE(HX_CH) | HX_LANE(HX_OH))) && (h & 127u) < amt * 25u / 100u * 128u / 100u) {   /* hats out */
        s->hit &= (uint8_t)~(HX_LANE(HX_CH) | HX_LANE(HX_OH));
        ch = 1;
    }
    h = hx_hash(h);
    if (((s->hit >> HX_CH) & 1u) && (h & 127u) < amt * 15u / 100u * 128u / 100u) {   /* closed -> open */
        s->hit = (uint8_t)((s->hit & ~HX_LANE(HX_CH)) | HX_LANE(HX_OH));
        ch = 1;
    }
    h = hx_hash(h);
    if (!kick && (p & 1u) && (h & 127u) < amt * 20u / 100u * 128u / 100u) {   /* a ghost snare off the beat */
        if (!s->hit)
            s->vel = 48;
        s->hit |= (uint8_t)HX_LANE(HX_SNARE);
        ch = 1;
    }
    if (kick && !(p & 3u))
        s->hit |= (uint8_t)HX_LANE(HX_KICK);      /* (never: the downbeat's kick stays) */
    return ch;
}

static int hx_chaos_notes(step_t *s, uint32_t p, uint32_t h, uint32_t amt)
{
    uint32_t i;
    if (st_kind(s) != ST_NOTE || !s->n)
        return 0;
    if ((h & 127u) < amt * 10u / 100u * 128u / 100u && (p & 3u)) {       /* a rest (never on a beat) */
        s->time = ST_REST;
        s->n = 0;
        return 1;
    }
    h = hx_hash(h);
    if ((h & 127u) < amt * 20u / 100u * 128u / 100u) {                    /* an octave up */
        for (i = 0; i < s->n && i < 4u; i++)
            s->note[i] = (uint8_t)(s->note[i] + 12u <= 127u ? s->note[i] + 12u : s->note[i]);
        h = hx_hash(h);
        if ((h & 127u) < amt * 25u / 100u * 128u / 100u && step_ratchet(s) == 1u)
            step_set_ratchet(s, 2);
        return 1;
    }
    h = hx_hash(h);
    if ((h & 127u) < amt * 25u / 100u * 128u / 100u && step_ratchet(s) == 1u) {   /* doubled */
        step_set_ratchet(s, 2);
        return 1;
    }
    return 0;
}

/* the step track t plays at idx: s itself, or its changed copy while a HYPER tool acts on it */
static __attribute__((noinline)) const step_t *hx_step(track_t *t, uint32_t idx, const step_t *s)
{
    uint32_t k = trk_index(t) % NTRK, act = perf_act, p = hx_pos(idx), changed = 0;
    int drum = drum_track(t);
    step_t *c = &hx_buf[k];
    hx_at[k] = 0;
    if (hx_fill == 1u && p >= 12u)                /* FILL armed: the bar's last beat starts it */
        hx_fill = 2;
    if (!(act & (PF_BIT(PF_ROLL) | PF_BIT(PF_CHAOS))) && hx_fill != 2u)
        return s;
    if (st_kind(s) == ST_TIE)
        return s;
    *c = *s;
    if (drum && st_kind(c) != ST_NOTE) {          /* (an empty drum step: a NOTE step with no hits, to add to) */
        c->time = ST_NOTE;
        c->n = 0;
        c->hit = 0;
        c->vel = 0;
        c->flags &= (uint8_t)~SF_RATCH;
        c->probability = 0;
    }
    if (drum && hx_fill == 2u && p >= 12u) {
        hx_fill_drum(c, p);
        changed = 1;
    } else if (drum && (act & PF_BIT(PF_ROLL))) {
        hx_roll(c, p);
        changed = 1;
    }
    if (act & PF_BIT(PF_CHAOS)) {
        uint32_t h = hx_hash(k * 0x9E3779B9u ^ idx * 0x85EBCA6Bu ^ (uint32_t)t->cond_loop * 0xC2B2AE35u ^ 0x48595052u);
        changed |= (uint32_t)(drum ? hx_chaos_drum(c, p, h, hx_chaos_amt()) : hx_chaos_notes(c, p, h, hx_chaos_amt()));
    }
    if (!changed)
        return s;
    if (drum && !c->hit && !c->n)                 /* (everything taken away: a rest) */
        c->time = ST_REST;
    hx_at[k] = (uint16_t)(idx + 1u);
    return c;
}
/* seq_ratchet: the step it repeats (the changed copy if seq_play played one) */
static const step_t *hx_rat_step(track_t *t, uint32_t idx, const step_t *s)
{
    uint32_t k = trk_index(t) % NTRK;
    return hx_at[k] == idx + 1u ? &hx_buf[k] : s;
}
/* FILL's conditions: steps marked FILL play while it plays too */
static int hx_filling(void) { return hx_fill == 2u; }

/* ------------------------------------------------------------------ HYPER CHORDS: the mood generator ---
 * Eight moods, each four chords (a bar each): a root in semitones above the track's ROOT and a quality. SEQ + OCT+
 * (ui_tools.c hx_chords_write) writes the next mood onto the selected track, the factory song (main.c) its chords.
 * The voicing: four notes at most (a step holds four), each chord's inversion and octave picked to move the least
 * from the chord before (the sum of the distances, low to high), the first chord centred near E4. Pure: no state. */
enum { HQ_MAJ, HQ_MIN, HQ_MAJ7, HQ_MIN7, HQ_DOM7, HQ_ADD9, HQ_MADD9, HQ_SUS2, HQ_SUS4, HQ_MAJ9, HQ_MIN9, HQ_N };
static const int8_t HQ_IV[HQ_N][4] = {   /* the intervals (-1: none) */
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 4, 7, 14}, {0, 3, 7, 14},
    {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 4, 11, 14}, {0, 3, 10, 14}};
static const char *const HQ_NAME[HQ_N] = {"", "m", "maj7", "m7", "7", "add9", "madd9", "sus2", "sus4", "maj9", "m9"};
#define HX_NMOOD 8u
static const char *const HX_MOOD[HX_NMOOD] = {"EUPHORIC", "EMOTIONAL", "DREAMY", "DARK", "NOSTALGIC", "ENERGETIC",
                                             "PLAYFUL", "MELANCHOLIC"};
static const uint8_t HX_MOOD_SCALE[HX_NMOOD] = {1, 1, 1, 2, 1, 1, 1, 2};   /* the scale it sets (N_SCALE: 1 MAJ, 2 MIN) */
static const uint8_t HX_PROG[HX_NMOOD][4][2] = {   /* {root, quality} a bar */
    {{5, HQ_MAJ7}, {7, HQ_DOM7}, {4, HQ_MIN7}, {9, HQ_MIN7}},    /* EUPHORIC   IVmaj7 V7 iiim7 vim7 (the royal road) */
    {{9, HQ_MIN7}, {5, HQ_MAJ7}, {0, HQ_ADD9}, {7, HQ_SUS4}},    /* EMOTIONAL  vim7 IVmaj7 Iadd9 Vsus4 */
    {{0, HQ_MAJ9}, {4, HQ_MIN7}, {5, HQ_MAJ7}, {5, HQ_MIN7}},    /* DREAMY     Imaj9 iiim7 IVmaj7 ivm7 */
    {{0, HQ_MIN9}, {8, HQ_MAJ7}, {5, HQ_MIN7}, {7, HQ_DOM7}},    /* DARK       im9 bVImaj7 ivm7 V7 */
    {{0, HQ_MAJ7}, {9, HQ_MIN7}, {2, HQ_MIN7}, {7, HQ_DOM7}},    /* NOSTALGIC  Imaj7 vim7 iim7 V7 */
    {{9, HQ_MIN}, {5, HQ_MAJ}, {0, HQ_MAJ}, {7, HQ_MAJ}},        /* ENERGETIC  vi IV I V */
    {{0, HQ_ADD9}, {4, HQ_DOM7}, {5, HQ_MAJ}, {5, HQ_MIN}},      /* PLAYFUL    Iadd9 III7 IV iv */
    {{0, HQ_MIN}, {10, HQ_MAJ}, {8, HQ_MAJ}, {7, HQ_DOM7}},      /* MELANCHOLIC i bVII bVI V7 (the descent) */
};
static void hx_sort4(uint8_t *a, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a[j - 1] > a[j]; j--) {
            uint8_t x = a[j];
            a[j] = a[j - 1];
            a[j - 1] = x;
        }
}
/* chord c (mood m, bar b) over ROOT root (0..11): its notes into out (low to high), how many; prev / pn the chord
 * before (pn 0: none, centred) */
static uint32_t hx_voice(uint32_t m, uint32_t b, uint32_t root, const uint8_t *prev, uint32_t pn, uint8_t *out)
{
    const uint8_t *ch = HX_PROG[m % HX_NMOOD][b & 3u];
    const int8_t *iv = HQ_IV[ch[1] % HQ_N];
    uint32_t n = 0, inv, best = ~0u, i;
    int32_t oct, base = 48 + (int32_t)((root + ch[0]) % 12u);   /* the chord's root from C3 .. B3 */
    uint8_t cand[4];
    while (n < 4u && iv[n] >= 0)
        n++;
    for (inv = 0; inv < n; inv++)
        for (oct = -12; oct <= 12; oct += 12) {
            uint32_t cost = 0;
            for (i = 0; i < n; i++)               /* inversion inv: the lowest inv notes an octave up */
                cand[i] = (uint8_t)(base + oct + iv[i] + (i < inv ? 12 : 0));
            hx_sort4(cand, n);
            if (cand[0] < 48u || cand[n - 1u] > 84u)   /* (C3 .. C6) */
                continue;
            if (pn) {
                for (i = 0; i < n; i++) {
                    int32_t d = (int32_t)cand[i] - (int32_t)prev[i < pn ? i : pn - 1u];
                    cost += (uint32_t)(d < 0 ? -d : d);
                }
            } else {
                int32_t c = 0;
                for (i = 0; i < n; i++)
                    c += cand[i];
                c = c / (int32_t)n - 64;                 /* (centred near E4) */
                cost = (uint32_t)(c < 0 ? -c : c);
            }
            if (cost < best) {
                best = cost;
                for (i = 0; i < n; i++)
                    out[i] = cand[i];
            }
        }
    return n;
}
/* the root note of mood m's bar b over ROOT root, in octave o (C(o-1) = 12 o) */
static uint32_t hx_bass(uint32_t m, uint32_t b, uint32_t root, uint32_t o)
{
    return 12u * o + (root + HX_PROG[m % HX_NMOOD][b & 3u][0]) % 12u;
}
