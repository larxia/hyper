/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPER's power-on song (main.c felucca_demo, DAH_DEMO_SONG 2) through the browser emulator's build (web/emu/felucca_web.c,
 * the firmware's sources, the device's clock driven by the audio):
 *   it boots into the song, PLAY plays its SONG from the first section, every one of the 8 tracks sounds in it
 *   (each voice count seen), the output is never silent for long, never clips (the limiter's ceiling), has no DC;
 *   the audio CPU estimate (the emulator's model of the device: an estimate, not a measurement) and the voices shed;
 *   the FX tools held over it (GLITCH ROLL CHAOS FILL PUNCH) keep it bounded.
 * Writes OUT/hyper_song.wav (the first 40 s, the drop's FX at 24..34 s) and OUT/screen_*.ppm (HOME, MIXER while playing).
 *   hyper_song_test OUTDIR */
#include "../web/emu/felucca_web.c"

static int nfail;
static void check(const char *what, int ok)
{
    printf("hyper song: %-82s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void screen(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/screen_%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t v = web_fb[i];
        v = (uint16_t)(v >> 8 | v << 8);                /* (stored high byte first) */
        fputc(((v >> 11) & 31) * 255 / 31, f);
        fputc(((v >> 5) & 63) * 255 / 63, f);
        fputc((v & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void wput(FILE *f, float x)
{
    int32_t v = (int32_t)lrintf(x * 32767.0f);
    int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    fwrite(&s, 2, 1, f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/hyper_song";
    char path[512];
    FILE *w;
    uint32_t k, i, secs = 40, frames = secs * FS, silent_run = 0, silent_max = 0, sheds0;
    uint32_t cpu_max = 0, playing = 0, ovr0;
    float peak = 0;
    double dc = 0;
    web_nor_erase();
    web_master(1023);                                   /* (MASTER all the way up: the limiter's ceiling is full scale) */
    web_boot();
    check("boots into HYPER's song: 150 BPM, 8 tracks, a 16-section SONG", song.g[G_BPM] == 150 && song.ntrk == 8u &&
          chain_config.count == 16u);
    check("track 1 HYPER KIT, track 8 808 HYPER", trk[0].preset == 6 && trk[7].preset == 7);
    rng_state = 0x1234567u;
    check("PLAY: the SONG from its first section", chain_prepare() == 0);
    snprintf(path, sizeof path, "%s/hyper_song.wav", dir);
    w = fopen(path, "wb");
    if (w) {                                            /* (44-byte header, 16-bit stereo) */
        uint32_t d = frames * 4u, hdr[11] = {0x46464952u, 36u + d, 0x45564157u, 0x20746d66u, 16u, 0x00020001u, FS, FS * 4u,
                                             0x00100004u, 0x61746164u, d};
        fwrite(hdr, 4, 11, w);
    }
    sheds0 = shed_count;
    ovr0 = web_overruns;
    (void)web_cpu_peak_q8();
    for (k = 0; k < frames; k += 128u) {
        uint32_t t = k / FS;
#define AT(sec) (k <= (sec) * FS && k + 128u > (sec) * FS)   /* (the block the second falls in) */
        if (AT(24u)) { perf_press(PF_GLT, 1); }
        if (AT(26u)) { perf_press(PF_GLT, 0); perf_press(PF_ROLL, 1); }
        if (AT(28u)) { perf_press(PF_ROLL, 0); perf_press(PF_CHAOS, 1); perf_press(PF_PUNCH, 1); }
        if (AT(31u)) { perf_press(PF_CHAOS, 0); perf_press(PF_FILLX, 1); perf_press(PF_FILLX, 0); }
        if (AT(34u)) perf_press(PF_PUNCH, 0);
        web_render(128);
        for (i = 0; i < 128u; i++) {
            float a = fabsf(out_l[i]) > fabsf(out_r[i]) ? fabsf(out_l[i]) : fabsf(out_r[i]);
            if (a > peak) peak = a;
            dc += out_l[i] + out_r[i];
            if (w) { wput(w, out_l[i]); wput(w, out_r[i]); }
            if (t >= 4u) {                              /* (after the intro's first bars) */
                if (a < 0.001f) silent_run++;
                else { if (silent_run > silent_max) silent_max = silent_run; silent_run = 0; }
            }
        }
        playing += song.playing;
        {
            uint32_t q = web_cpu_peak_q8();
            if (q > cpu_max) cpu_max = q;
        }
        if (AT(20u)) { ui.force = 1; for (i = 0; i < 20u; i++) web_render(128); screen(dir, "home"); }
    }
    if (w) fclose(w);
    printf("hyper song: %u s, peak %.3f, DC %.5f, longest silence after 4 s %.2f s, audio CPU peak ~%u %% (an estimate), "
           "voices shed %u, late blocks %u\n", secs, peak, dc / (2.0 * frames), silent_max / (double)FS,
           cpu_max * 100u / 256u, shed_count - sheds0, web_overruns - ovr0);
    check("it plays the whole time (the SONG running)", playing >= frames / 128u * 95u / 100u);
    check("never over full scale, at least as loud as FM Stud1o's demo through the same path (peak > 0.3)", peak <= 1.0f && peak > 0.3f);
    check("no DC", fabs(dc / (2.0 * frames)) < 0.01);
    check("never silent for more than 1 s once it has started", silent_max < FS);
    printf("hyper song: WAV %s\n", path);
    {   /* the SEQ + OCT+ tools on the real firmware: beats on a DRUM track, chords on a synth; the track's label */
        uint32_t b, hits = 0, ok = 1, notes;
        transport_req = 2;
        web_render(128);
        for (b = 0; b < HB_N; b++) {
            hx_beat_write(&trk[0]);
            hits = 0;
            for (i = 0; i < NSTEP; i++)
                hits += trk[0].step[i].hit != 0;
            ok &= hits >= 16u && (trk[0].step[0].hit & 1u) && trk[0].p[P_SLEN] == 64;
            printf("hyper song: beat %-10s %2u steps\n", HB_NAME[b], hits);
        }
        check("SEQ + OCT+ on DRUMS: six factory beats, each 4 bars with the kick on 1", ok);
        hx_chords_write(&trk[3]);
        notes = trk[3].step[0].n + trk[3].step[16].n + trk[3].step[32].n + trk[3].step[48].n;
        check("SEQ + OCT+ on CHORDS: a mood's 4 chords written (held)", notes >= 12u && st_kind(&trk[3].step[1]) == ST_TIE);
        track_select(1);
        printf("hyper song: track 2 picked: '%s'\n", ui.msg);
        check("a track picked says its role and sound (T2 BASS: SUB BASS)", !strcmp(ui.msg, "T2 BASS: SUB BASS"));
        track_select(0);
        check("track 1: T1 DRUMS: HYPER KIT", !strcmp(ui.msg, "T1 DRUMS: HYPER KIT"));
    }
    return nfail != 0;
}
