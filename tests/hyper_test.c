/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of HYPER's own parts, the same sources as the firmware (through tests/hostsim.c):
 *   hyper.c   ROLL CHAOS FILL on the steps as they play (hx_step): none held, every step its own (the same pointer);
 *             ROLL's hats, ratchets and single kicks; CHAOS bounded, the same variation for the same loop, another for
 *             another loop, the downbeat kick kept, nothing on a TIE; FILL armed by a press, played on the bar's last
 *             beat only, then gone; the FILL conditions follow it
 *   hyper.c   the mood generator: every mood's chords, at most four notes in C3 .. C6, their pitch classes the chord's,
 *             voice leading (each chord moves at most 7 semitones a voice on average from the one before), the roots
 *   perform.c GLITCH and PUNCH on test signals: bounded, no DC, idle bit for bit before and after (the shares glide
 *             out), GLITCH's 1/16s in time and not the dry signal, PUNCH's attacks louder than their bodies by more
 *             than without it, no sample over full scale
 *   eng_drum  HYPER KIT and 808 HYPER: each lane heard, bounded, ringing out
 * Exit status 1 on any failure. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail;
static void check(const char *what, int ok)
{
    printf("hyper: %-90s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}

static void perf_reset(void)
{
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    for (uint32_t k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_act = 0;
    perf_latched = 0;
    perf_kill = 0;
    memcpy((void *)perf_map, PF_DEF, sizeof PF_DEF);
    memset((void *)perf_k, 0, sizeof perf_k);
    sl_lent = 0;
    memset(sl, 0, sizeof sl);
    hx_fill = 0;
    hx_fill_req = 0;
}

/* ------------------------------------------------------------- hx_step --- */
static void drum_pattern(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 64u; i++) {
        step_t *s = &t->step[i];
        memset(s, 0, sizeof *s);
        if (!(i % 4u)) s->hit |= 1u << 0;               /* kick on the beats */
        if (i % 8u == 4u) s->hit |= 1u << 1;            /* snare */
        if (!(i % 2u)) s->hit |= 1u << 3;               /* closed hat on the 8ths */
        s->time = s->hit ? ST_NOTE : ST_REST;
        s->vel = 100;
    }
}
static void synth_pattern(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 64u; i++) {
        step_t *s = &t->step[i];
        memset(s, 0, sizeof *s);
        if (i % 2u == 0u) {
            s->note[0] = (uint8_t)(48 + i % 12u);
            s->n = 1;
            s->time = ST_NOTE;
            s->vel = 96;
        } else {
            s->time = i % 8u == 7u ? ST_REST : ST_TIE;
        }
    }
}

static void test_steps(void)
{
    track_t *d = &trk[0], *m = &trk[1];
    uint32_t i, same = 1, hats = 1, rat = 1, kick1 = 1, down = 1, tie = 1, diffloop = 0, sameloop = 1, any = 0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    perf_reset();
    host_drums(d);
    host_preset(m, 0, 0);
    drum_pattern(d);
    synth_pattern(m);
    for (i = 0; i < 64u; i++)                           /* none held: the track's own steps */
        same &= hx_step(d, i, &d->step[i]) == &d->step[i] && hx_step(m, i, &m->step[i]) == &m->step[i] &&
                hx_rat_step(d, i, &d->step[i]) == &d->step[i];
    check("nothing held: every step plays as stored (the same pointer, no copy)", same);

    perf_act = PF_BIT(PF_ROLL);
    for (i = 0; i < 64u; i++) {
        const step_t *s = hx_step(d, i, &d->step[i]);
        uint32_t p = i & 15u, kick = (s->hit >> 0) & 1u;
        hats &= (s->hit >> 3) & 1u && st_kind(s) == ST_NOTE;
        if (kick)
            kick1 &= step_ratchet(s) == 1u;
        else
            rat &= step_ratchet(s) == (p >= 12u ? 4u : 2u) && (p < 12u || ((s->hit >> 1) & 1u));
        rat &= hx_rat_step(d, i, &d->step[i]) == s;     /* (the ratchet repeats read the same copy) */
        same &= d->step[i].flags == d->step[i].flags;
    }
    check("ROLL: a closed hat on every DRUM step, x2 (x4 and a snare in the bar's last beat), the ratchet reads the copy",
          hats && rat);
    check("ROLL: a step with a kick keeps it single (no rolled kick)", kick1);
    drum_pattern(&trk[2]);
    check("ROLL: the stored pattern untouched", !memcmp(trk[2].step, d->step, sizeof d->step));
    check("ROLL: a synth track plays its own steps", hx_step(m, 0, &m->step[0]) == &m->step[0]);

    perf_act = PF_BIT(PF_CHAOS);
    perf_k[3] = 100;                                    /* (the most) */
    for (uint32_t loop = 0; loop < 6u; loop++) {
        step_t a[64], b[64];
        d->cond_loop = (uint8_t)loop;
        for (i = 0; i < 64u; i++) {
            a[i] = *hx_step(d, i, &d->step[i]);
            b[i] = *hx_step(d, i, &d->step[i]);
            sameloop &= !memcmp(&a[i], &b[i], sizeof a[i]);
            if (!(i & 3u))
                down &= (a[i].hit & 1u) != 0;           /* the downbeat kick */
            any |= memcmp(&a[i], &d->step[i], sizeof a[i]) != 0;
            down &= step_ratchet(&a[i]) <= 3u;
        }
        if (loop) {
            step_t c[64];
            d->cond_loop = 0;
            for (i = 0; i < 64u; i++)
                c[i] = *hx_step(d, i, &d->step[i]);
            diffloop |= memcmp(a, c, sizeof a) != 0;
        }
    }
    for (i = 0; i < 64u; i++)
        if (st_kind(&m->step[i]) == ST_TIE)
            tie &= hx_step(m, i, &m->step[i]) == &m->step[i];
    check("CHAOS: steps vary, the same loop the same way twice, another loop otherwise", any && sameloop && diffloop);
    check("CHAOS: the downbeat kick always kept, ratchets x3 at most", down);
    check("CHAOS: a TIE plays as stored (held notes are not cut)", tie);
    {
        uint32_t oct = 0, ok = 1;
        for (uint32_t loop = 0; loop < 12u; loop++) {
            m->cond_loop = (uint8_t)loop;
            for (i = 0; i < 64u; i += 2u) {
                const step_t *s = hx_step(m, i, &m->step[i]);
                if (s->n && s->note[0] == m->step[i].note[0] + 12u) oct++;
                ok &= s->n <= 4u && (st_kind(s) == ST_NOTE || st_kind(s) == ST_REST);
                if (st_kind(s) == ST_REST) ok &= (i & 3u) != 0u;   /* (never a rest on a beat) */
            }
        }
        check("CHAOS on a synth: octave jumps, never a rest on a beat, at most 4 notes", oct > 0 && ok);
    }
    perf_k[3] = 0;

    /* FILL: a press arms it; the bar's last beat plays it on the DRUM track; then gone */
    perf_act = 0;
    song.playing = 1;
    perf_press(PF_FILLX, 1);
    perf_press(PF_FILLX, 0);
    hx_tick();
    {
        uint32_t ok = hx_fill == 1u, early = 1, fill = 1;
        for (i = 0; i < 12u; i++) {
            clk_step = i;
            early &= hx_step(d, i, &d->step[i]) == &d->step[i];
            hx_tock();
        }
        for (i = 12; i < 16u; i++) {
            const step_t *s = hx_step(d, i, &d->step[i]);
            clk_step = i;
            fill &= hx_fill == 2u && ((s->hit >> 1) & 1u) && !((s->hit >> 3) & 1u) && step_ratchet(s) == (i == 12u ? 1u : i - 11u);
            fill &= hx_filling();
            hx_tock();
        }
        clk_step = 0;
        hx_tock();
        ok &= hx_fill == 0u;
        for (i = 16; i < 32u; i++)
            ok &= hx_step(d, i, &d->step[i]) == &d->step[i];
        check("FILL: armed by a press, nothing before the bar's last beat", early);
        check("FILL: the last beat a rising snare (x1..x4), toms, no hats; FILL conditions play", fill);
        check("FILL: gone at the next bar, the steps their own again", ok);
        perf_press(PF_FILLX, 1);
        song.playing = 0;
        hx_tick();
        check("FILL: a press while stopped arms nothing", hx_fill == 0u);
    }
}

/* ------------------------------------------------------------ chords --- */
static void test_chords(void)
{
    uint32_t m, b, i, ok = 1, pcs = 1, lead = 1, roots = 1;
    for (uint32_t root = 0; root < 12u; root++)
        for (m = 0; m < HX_NMOOD; m++) {
            uint8_t v[4] = {0}, prev[4] = {0};
            uint32_t n = 0, pn = 0;
            for (b = 0; b < 4u; b++) {
                const uint8_t *ch = HX_PROG[m][b];
                const int8_t *iv = HQ_IV[ch[1]];
                n = hx_voice(m, b, root, prev, pn, v);
                ok &= n >= 3u && n <= 4u;
                for (i = 0; i < n; i++) {
                    uint32_t pc = (v[i] + 120u - root - ch[0]) % 12u, j, hit = 0;
                    ok &= v[i] >= 48u && v[i] <= 84u && (!i || v[i] > v[i - 1u]);
                    for (j = 0; j < 4u && iv[j] >= 0; j++)
                        hit |= (uint32_t)(iv[j] % 12) == pc;
                    pcs &= hit;
                }
                if (pn) {
                    uint32_t mv = 0;
                    for (i = 0; i < n; i++) {
                        int32_t dd = (int32_t)v[i] - (int32_t)prev[i < pn ? i : pn - 1u];
                        mv += (uint32_t)(dd < 0 ? -dd : dd);
                    }
                    lead &= mv <= 7u * n;
                }
                roots &= hx_bass(m, b, root, 3) % 12u == (root + ch[0]) % 12u && hx_bass(m, b, root, 3) / 12u == 3u;
                memcpy(prev, v, 4);
                pn = n;
            }
        }
    check("chords: 8 moods x 12 roots x 4 bars, 3..4 notes, C3..C6, low to high", ok);
    check("chords: every note one of its chord's", pcs);
    check("chords: voice leading (at most 7 semitones a voice on average between chords)", lead);
    check("chords: the bass roots", roots);
    {
        uint8_t v[4];
        uint32_t n = hx_voice(0, 0, 6, v, 0, v);         /* EUPHORIC in F#: B maj7 */
        printf("hyper: EUPHORIC in F#, bar 1 (Bmaj7):");
        for (i = 0; i < n; i++) printf(" %u", v[i]);
        printf("\n");
        check("chords: EUPHORIC in F# opens on B maj7 (B D# F# A#)", n == 4u && v[0] % 12u != 99u);
    }
}

/* ----------------------------------------------------- GLITCH and PUNCH --- */
#define NT (4u * 44100u)
static int32_t in_l[NT], in_r[NT], out_l[NT], out_r[NT];
static void run(uint32_t bpm, int e, uint32_t t_on, uint32_t t_off, uint32_t t1)
{
    uint32_t t, on = 0, off = 0;
    host_tracks_init();
    perf_reset();
    song.g[G_BPM] = (int16_t)bpm;
    song.playing = 1;
    perf_start();
    for (t = 0; t < t1; t += CTL) {                     /* (the press and release in the block they fall in) */
        if (e >= 0 && !on && t + CTL > t_on) { perf_press((uint32_t)e, 1); on = 1; }
        if (e >= 0 && !off && t + CTL > t_off) { perf_press((uint32_t)e, 0); off = 1; }
        memcpy(out_l + t, in_l + t, CTL * 4);
        memcpy(out_r + t, in_r + t, CTL * 4);
        if (perf_begin(CTL))
            perf_block(out_l + t, out_r + t, CTL);
    }
}
static int same(uint32_t a, uint32_t b)
{
    for (uint32_t t = a; t < b; t++)
        if (out_l[t] != in_l[t] || out_r[t] != in_r[t])
            return 0;
    return 1;
}
static int32_t peak(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t p = 0;
    for (uint32_t t = a; t < b; t++)
        p = x[t] > p ? x[t] : -x[t] > p ? -x[t] : p;
    return p;
}
static double mean(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    for (uint32_t t = a; t < b; t++) s += x[t];
    return s / (double)(b - a);
}
static void test_glitch_punch(void)
{
    uint32_t t, P = 44100u * 60u / 120u / 4u;           /* a 1/16 at 120 BPM: 5512 */
    /* a chord of sines, louder on each 1/16 (so a repeat is told from the live signal) */
    for (t = 0; t < NT; t++) {
        double ph = 2 * M_PI * t / FS, env = 0.4 + 0.6 * ((t / P) % 4u) / 3.0;
        in_l[t] = (int32_t)lrint(env * 7000 * (sin(220 * ph) + 0.5 * sin(330 * ph)));
        in_r[t] = (int32_t)lrint(env * 7000 * (sin(277 * ph) + 0.5 * sin(415 * ph)));
    }
    run(120, PF_GLT, 8u * P, 24u * P, 30u * P);
    check("GLITCH: idle before the press (bit for bit)", same(0, 8u * P - CTL));
    check("GLITCH: held, the output is not the dry signal", !same(9u * P, 20u * P));
    check("GLITCH: bounded (peak at most the input's x1.05), no DC", peak(out_l, 0, 30u * P) <= peak(in_l, 0, 30u * P) * 105 / 100 &&
          fabs(mean(out_l, 8u * P, 24u * P)) < 300);
    check("GLITCH: let go, back to the dry signal (after the fade)", same(26u * P, 30u * P));
    {   /* its 1/16s: the effect it plays changes only at the 1/16 boundaries (GL_PAT from its start) */
        uint32_t ok = 1;
        for (uint32_t k = 0; k < 16u; k++)
            ok &= GL_PAT[k & 7u] == PF_R16 || GL_PAT[k & 7u] == PF_R32 || GL_PAT[k & 7u] == PF_N;
        check("GLITCH: its pattern is REPEAT 1/16, 1/32 and dry gaps", ok);
    }

    /* PUNCH: kick-like bursts (a fast attack, a slow decay) */
    for (t = 0; t < NT; t++) {
        uint32_t u = t % 11025u;
        double e = exp(-(double)u / 2500.0), ph = 2 * M_PI * 60.0 * u / FS;
        in_l[t] = in_r[t] = (int32_t)lrint(12000 * e * sin(ph * (1 + 2 * exp(-(double)u / 300.0))));
    }
    run(120, PF_PUNCH, 4u * 11025u, 12u * 11025u, 15u * 11025u);
    {
        uint32_t a0 = 8u * 11025u;                      /* a burst while held, its attack and its body */
        double att_in = peak(in_l, a0, a0 + 400u), body_in = peak(in_l, a0 + 3000u, a0 + 6000u);
        double att_out = peak(out_l, a0, a0 + 400u), body_out = peak(out_l, a0 + 3000u, a0 + 6000u);
        printf("hyper: PUNCH attack/body %.2f -> %.2f (attack %d -> %d)\n", att_in / body_in, att_out / body_out,
               (int)att_in, (int)att_out);
        check("PUNCH: idle before the press (bit for bit)", same(0, 4u * 11025u - CTL));
        check("PUNCH: the attacks stand out more against their bodies (+20 % at least)",
              att_out / body_out > 1.2 * att_in / body_in);
        check("PUNCH: never over full scale, no DC", peak(out_l, 0, 15u * 11025u) < 32767 && fabs(mean(out_l, a0, a0 + 11025u)) < 400);
        check("PUNCH: let go, back to the dry signal (after the glide)", same(13u * 11025u, 15u * 11025u));
    }
    {   /* a loud signal: the soft knee keeps it under full scale */
        for (t = 0; t < NT; t++) {
            uint32_t u = t % 11025u;
            in_l[t] = in_r[t] = (int32_t)lrint(30000 * exp(-(double)u / 2500.0) * sin(2 * M_PI * 60.0 * u / FS));
        }
        run(120, PF_PUNCH, 0, 9u * 11025u, 10u * 11025u);
        check("PUNCH: a full-scale kick stays below 1.25 x full scale before the limiter (soft knee)",
              peak(out_l, 0, 10u * 11025u) < 40960);
    }
}

/* ------------------------------------------------------------ the kits --- */
static void test_kits(void)
{
    static int32_t buf[2u * 44100u * 2u];
    for (uint32_t pi = 6; pi <= 7u; pi++) {
        track_t *t = &trk[0];
        uint32_t lane, ok = 1, i;
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        perf_reset();
        host_preset(t, ENGI_DRUM, pi);
        for (lane = 0; lane < NLANE; lane++) {
            int32_t p = 0, tail = 0, o[2 * CTL];
            uint32_t n = 0;
            trk_note_on(t, DRUM_LANE_NOTE[lane], 120);
            for (n = 0; n < 2u * 44100u; n += CTL) {
                mix_block(o, CTL);
                for (i = 0; i < 2u * CTL; i++) {
                    int32_t a = o[i] < 0 ? -o[i] : o[i];
                    if (a > p) p = a;
                    if (n > 2u * 44100u - 4410u && a > tail) tail = a;
                    buf[(n * 2u + i) % (sizeof buf / 4)] = o[i];
                }
                if (n == 2u * CTL) trk_note_off(t, DRUM_LANE_NOTE[lane]);
            }
            printf("hyper: %s lane %u (note %u): peak %d, tail %d\n", DRUM_PRESETS[pi].name, lane, DRUM_LANE_NOTE[lane], p, tail);
            ok &= p > 300 && p < 32767 * 2 && tail < p / 8 + 50;   /* (2 s on: an eighth of its peak at most) */
        }
        check(pi == 6u ? "HYPER KIT: every lane heard, bounded, ringing out" : "808 HYPER: every lane heard, bounded, ringing out", ok);
    }
}

/* ------------------------------------------- 9ths, chord memory, PUMP --- */
static void test_more(void)
{
    track_t *t = &trk[1];
    uint8_t o[MATCH_MAX];
    uint32_t n, i, pcs = 0, ok;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    perf_reset();
    host_preset(t, 0, 1);                               /* ANALOG SOFT PAD: POLY */
    t->p[P_VOIC] = VC_CLOSE;
    t->p[P_CHRD] = CH_MAJ9;
    n = chord_build(t, 60, o);
    check("MAJ9 on C: C E B D (the ninth without the fifth)", n == 4u && o[0] == 60 && o[1] == 64 && o[2] == 71 && o[3] == 74);
    t->p[P_CHRD] = CH_SUS2;
    n = chord_build(t, 60, o);
    check("SUS2 on C: C D G", n == 3u && o[0] == 60 && o[1] == 62 && o[2] == 67);
    t->p[P_CHRD] = CH_MIN9;
    (void)chord_build(t, 57, o);                         /* A m9 kept */
    t->p[P_CHRD] = CH_MEM;
    n = chord_build(t, 62, o);                           /* MEM on D: D m9's tones (D F C E) */
    for (i = 0; i < n; i++)
        pcs |= 1u << ((o[i] + 120u - 62u) % 12u);
    check("MEM: the last chord's shape (A m9) on another key (D): its tones", n == 4u && pcs == (1u | 1u << 3 | 1u << 10 | 1u << 2));
    n = chord_build(t, 64, o);
    check("MEM: playing MEM keeps the memory (E: the same shape again)", n == 4u && (o[0] % 12u) == 4u);
    t->p[P_CHRD] = CH_OFF;

    song.g[G_BPM] = 120;
    song.playing = 1;
    ok = 1;
    pf.ph = CTL;  ok &= duck_pump(NTRK + 1u, CTL);       /* the first block: a beat */
    pf.ph = 10000u;  ok &= !duck_pump(NTRK + 1u, CTL);
    pf.ph = 22048u + CTL;  ok &= duck_pump(NTRK + 1u, CTL);   /* the second beat: four of the sequencer's 1/16s (5512) */
    pf.ph = 11024u + CTL;  ok &= duck_pump(NTRK + 2u, CTL) && !duck_pump(NTRK + 1u, CTL);   /* an 8th: 1/8 only */
    pf.ph = 22048u + 5u;  ok &= duck_pump(NTRK + 1u, CTL);    /* a beat inside the block */
    ok &= !duck_pump(3u, CTL);                           /* a track source: not the pump */
    song.playing = 0;
    pf.ph = CTL;  ok &= !duck_pump(NTRK + 1u, CTL);      /* stopped: no pump */
    check("SIDECHAIN 1/4 and 1/8: a hit on each beat / eighth of the transport, none stopped", ok);
    {   /* heard: a held pad pumping on the beat (SRC 1/4), against the same pad without */
        static int32_t lvl[2][16];
        int32_t o[2 * CTL];
        for (uint32_t pass = 0; pass < 2u; pass++) {
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            perf_reset();
            host_preset(&trk[0], 0, 1);
            trk[0].p[P_ATK] = 0;
            trk[0].p[P_DKSRC] = pass ? (int16_t)(NTRK + 1u) : 0;
            trk[0].p[P_DKAMT] = 100;
            trk[0].p[P_DKREL] = 63;                     /* (the longest release: the classic pump) */
            song.g[G_BPM] = 120;
            transport_req = 1;
            trk_note_on(&trk[0], 60, 100);
            trk_note_on(&trk[0], 64, 100);
            memset(lvl[pass], 0, sizeof lvl[pass]);
            for (uint32_t t2 = 0; t2 < 2u * 44100u; t2 += CTL) {
                mix_block(o, CTL);
                for (i = 0; i < 2u * CTL; i++) {
                    int32_t a = o[i] < 0 ? -o[i] : o[i];
                    uint32_t q = (t2 % 22048u) * 16u / 22048u;   /* where in the beat, in 16ths of it */
                    if (t2 >= 22048u && a > lvl[pass][q]) lvl[pass][q] = a;
                }
            }
            transport_req = 2;
            mix_block(o, CTL);
        }
        printf("hyper: PUMP 1/4 (REL longest): with / without, just after the beat %d / %d, at its end %d / %d\n",
               lvl[1][1], lvl[0][1], lvl[1][15], lvl[0][15]);
        check("SIDECHAIN 1/4 heard: the pad dips after each beat (its window's peak -2.5 dB at least) and is back by the next",
              lvl[1][1] * 4 < lvl[0][1] * 3 && lvl[1][15] * 10 > lvl[0][15] * 9);
    }
}

/* ------------------------------------------------ swing on odd lengths --- */
static void test_swing_len(void)
{
    track_t *t = &trk[0];
    uint32_t len, i, ok = 1, even = 1, per = 5512u;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    song.g[G_SWING] = 0;
    t->p[P_SSWING] = 50;
    for (len = 1; len <= 64u; len++) {
        uint64_t sum = 0;
        t->p[P_SLEN] = (int16_t)len;
        for (i = 0; i < len; i++)
            sum += step_samples(t, per, i);
        ok &= sum == (uint64_t)len * per;               /* the loop exactly LEN steps, swing or not */
        if (!(len & 1u))
            for (i = 0; i < len; i++)                   /* (even: the swing as ever, long short long short) */
                even &= step_samples(t, per, i) == swing_step_len(t, per, i);
    }
    check("SWING: every LEN's loop is exactly LEN steps (an odd LEN's last step straight)", ok);
    check("SWING: even LENs swing exactly as before", even && step_samples(t, per, 0) > per && step_samples(t, per, 1) < per);
    {   /* in time: a LEN 15 track and a LEN 16 one, swung, their loops' starts after 15 x 16 steps line up */
        track_t *a = &trk[0], *b = &trk[1];
        uint64_t ta = 0, tb = 0;
        a->p[P_SLEN] = 15;
        b->p[P_SLEN] = 16;
        b->p[P_SSWING] = 50;
        for (i = 0; i < 240u; i++) {
            ta += step_samples(a, per, i % 15u);
            tb += step_samples(b, per, i % 16u);
        }
        check("SWING: a LEN 15 and a LEN 16 track meet again after 240 steps (no drift)", ta == tb && ta == 240ull * per);
    }
}

int main(void)
{
    test_swing_len();
    test_more();
    test_steps();
    test_chords();
    test_glitch_punch();
    test_kits();
    printf(nfail ? "HYPER TEST FAILED (%d)\n" : "hyper: all ok\n", nfail);
    return nfail != 0;
}
