/* SPDX-License-Identifier: GPL-3.0-only */
/* A3.2a minimal production-path timing regression, using the real seq.c host harness. */
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define PROJECT_TEST_NO_PROCESS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

int main(void)
{
    uint32_t lane, step, block, prev_age, first0 = SEQ_NONE, first1 = SEQ_NONE;
    uint32_t count0 = 0, count1 = 0;
    uint32_t adv;
    int32_t out[CTL * 2];
    for (lane = 0; lane < DRUM_RAW_LANES; ++lane)
        for (step = 0; step < NSTEP; ++step)
            assert(drum_raw_offset_get(lane, step) == 0);
    assert(drum_raw_offset_set(0, 0, -32));
    assert(drum_raw_offset_set(1, 0, 31));
    assert(drum_raw_offset_set(7, 63, -1));
    assert(drum_raw_offset_set(2, 0, 1));
    assert(drum_raw_offset_get(0, 0) == -32);
    assert(drum_raw_offset_get(1, 0) == 31);
    assert(drum_raw_offset_get(7, 63) == -1);
    assert(drum_raw_offset_get(2, 0) == 1);
    assert(drum_raw_offset_get(3, 0) == 0); /* offsets are independent per lane */
    assert(drum_raw_offset_get(0, 63) == 0);
    assert(!drum_raw_offset_set(8, 0, 1));
    assert(!drum_raw_offset_set(0, 64, 1));
    assert(drum_raw_offset_set(0, 0, 0));
    assert(drum_raw_offset_get(0, 0) == 0);
    assert(drum_raw_offset_any());
    drum_raw_offset_clear();
    assert(!drum_raw_offset_any());

    /* Recording stores the latency-corrected input residual; one unit is one 1/64 step. */
    host_tracks_init();
    song.g[G_BPM] = 120; song.g[G_SWING] = 0;
    TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
    seq_start(); song.rec = 1u << TRK_DRUM;
    clk_beat = 0;
    clk_pos = BEAT_U / 4u + REC_LAT * 120u + (BEAT_U / 4u) * 4u / 64u;
    drum_input(2, LV_NORM, 0, 1);
    assert(dstep_has(&TDRUM->dstep[1], 2));
    assert(drum_raw_offset_get(2, 1) == 4);
    clk_pos = BEAT_U / 2u + REC_LAT * 120u - (BEAT_U / 4u) * 4u / 64u;
    drum_input(3, LV_NORM, 0, 1);
    assert(dstep_has(&TDRUM->dstep[2], 3));
    assert(drum_raw_offset_get(3, 2) == -4);
    clk_pos = 3u * (BEAT_U / 4u) + (BEAT_U / 8u) + REC_LAT * 120u + (BEAT_U / 4u) / 8u;
    drum_input(4, LV_NORM, 0, 1); /* nearest step is the next one; overdub must replace its offset */
    assert(dstep_has(&TDRUM->dstep[4], 4));
    assert(drum_raw_offset_get(4, 4) == -24);
    drum_raw_offset_set(2, 1, 31);
    clk_pos = BEAT_U / 4u + REC_LAT * 120u + (BEAT_U / 4u) * 2u / 64u;
    drum_input(2, LV_HARD, 0, 1);
    assert(drum_raw_offset_get(2, 1) == 2 && dstep_lvl(&TDRUM->dstep[1], 2) == LV_HARD);
    clk_beat = 15u;
    clk_pos = 3u * (BEAT_U / 4u) + REC_LAT * 120u + (BEAT_U / 4u) * 3u / 64u;
    drum_input(5, LV_NORM, 0, 1);
    assert(dstep_has(&TDRUM->dstep[63], 5) && drum_raw_offset_get(5, 63) == 3);
    drum_raw_offset_clear();
    song.rec = 0; seq_stop();

    /* REPLACE is confined to the selected lane and touched cell; undo restores its old raw timing. */
    host_tracks_init(); song.g[G_BPM] = 120; song.playing = 1; song.rec = 1u << TRK_DRUM;
    TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64; drum_pad_sel = 2; rec_mode_set(REC_REPLACE);
    undo.valid = 0;
    dstep_set(&TDRUM->dstep[0], 0, LV_HARD, 0);
    dstep_set(&TDRUM->dstep[0], 1, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[0], 2, LV_SOFT, 0);
    drum_raw_offset_set(0, 0, 10); drum_raw_offset_set(1, 0, -9); drum_raw_offset_set(2, 0, 7);
    clk_beat = 0; clk_pos = REC_LAT * 120u;
    drum_input(0, LV_NORM, 0, 1);                 /* live monitor, but not the selected replacement lane */
    drum_input(2, LV_HARD, 1, 1);                  /* selected lane: update its one occupied cell */
    assert(dstep_has(&TDRUM->dstep[0], 0) && drum_raw_offset_get(0, 0) == 10);
    assert(dstep_has(&TDRUM->dstep[0], 1) && drum_raw_offset_get(1, 0) == -9);
    assert(dstep_has(&TDRUM->dstep[0], 2) && drum_raw_offset_get(2, 0) == 0 &&
           dstep_lvl(&TDRUM->dstep[0], 2) == LV_HARD && dstep_rat(&TDRUM->dstep[0], 2) == 1);
    assert(undo.valid && dstep_lvl((const dstep_t *)&undo.st[0], 2) == LV_SOFT && undo.raw[2][0] == 7);
    rec_mode_set(REC_OVERDUB); song.rec = 0; song.playing = 0;

    /* A live monitored recording must not be fired again by the scheduled pass. */
    host_tracks_init(); song.g[G_BPM] = 120; song.g[G_SWING] = 0;
    TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
    seq_start(); song.rec = 1u << TRK_DRUM;
    clk_beat = 0;
    clk_pos = BEAT_U / 4u + BEAT_U / 8u + REC_LAT * 120u + (BEAT_U / 4u) / 8u;
    drum_input(0, LV_NORM, 0, 1);
    assert(drum_raw_offset_get(0, 2) == -24 && dstep_has(&TDRUM->dstep[2], 0));
    prev_age = drums.age;
    for (block = 0; block < 4u; ++block) {
        memset(out, 0, sizeof out);
        mix_block(out, CTL);
    }
    assert(drums.age == prev_age); /* live trigger only; scheduler consumed the matching lane event */
    song.rec = 0; seq_stop();

    /* Real seq.c playback: the two lanes share a step but must start at distinct clock times.
     * Output is block-quantized, so expected timing error is bounded by 2*CTL*BPM units. */
    host_tracks_init();
    song.g[G_BPM] = 120; song.g[G_SWING] = 0;
    TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
    dstep_set(&TDRUM->dstep[2], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[2], 1, LV_NORM, 0);
    drum_raw_offset_set(0, 2, -16);
    drum_raw_offset_set(1, 2, 16);
    seq_start();
    prev_age = drums.age;
    adv = CTL * (uint32_t)song.g[G_BPM];
    for (block = 0; block < 520u; ++block) {
        uint32_t v;
        memset(out, 0, sizeof out);
        mix_block(out, CTL);
        for (v = 0; v < NDRUM; ++v) {
            uint32_t a = drums.v[v].age;
            if (a <= prev_age) continue;
            if (drums.v[v].note == 36u && first0 == SEQ_NONE) first0 = block;
            if (drums.v[v].note == 35u && first1 == SEQ_NONE) first1 = block;
            if (drums.v[v].note == 36u) count0++;
            if (drums.v[v].note == 35u) count1++;
            if (a > prev_age) prev_age = a;
        }
    }
    assert(first0 != SEQ_NONE && first1 != SEQ_NONE && first1 > first0);
    printf("timing blocks: lane0=%u lane1=%u block_units=%u target_units=%u\n",
           first0, first1, adv, 32u * (BEAT_U / 4u) / 64u);
    assert((first1 - first0) * adv >= 32u * (BEAT_U / 4u) / 64u - adv);
    assert((first1 - first0) * adv <= 32u * (BEAT_U / 4u) / 64u + adv);
    assert(count0 == 1u && count1 == 1u); /* no duplicate at offset/step boundary */
    seq_stop();

    /* At QTZ 50%, opposite raw offsets shrink independently while remaining untouched. */
    {
        uint32_t f0 = SEQ_NONE, f1 = SEQ_NONE, seen;
        uint32_t age = drums.age;
        seen = age;
        host_tracks_init();
        song.g[G_BPM] = 120; song.g[G_SWING] = 0; song.playing = 1;
        TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
        dstep_set(&TDRUM->dstep[2], 0, LV_NORM, 0);
        dstep_set(&TDRUM->dstep[2], 1, LV_NORM, 0);
        assert(drum_raw_offset_set(0, 2, -16) && drum_raw_offset_set(1, 2, 16));
        assert(drum_qtz_strength_set(50));
        seq_start();
        for (block = 0; block < 520u; ++block) {
            uint32_t v;
            memset(out, 0, sizeof out);
            mix_block(out, CTL);
            for (v = 0; v < NDRUM; ++v) {
                uint32_t a = drums.v[v].age;
                if (a <= seen) continue;
                if (drums.v[v].note == 36u && f0 == SEQ_NONE) f0 = block;
                if (drums.v[v].note == 35u && f1 == SEQ_NONE) f1 = block;
                seen = a;
            }
        }
        adv = CTL * (uint32_t)song.g[G_BPM];
        assert(f0 != SEQ_NONE && f1 != SEQ_NONE && f1 > f0);
        assert((f1 - f0) * adv >= 16u * (BEAT_U / 4u) / 64u - adv);
        assert((f1 - f0) * adv <= 16u * (BEAT_U / 4u) / 64u + adv);
        assert(drum_raw_offset_get(0, 2) == -16 && drum_raw_offset_get(1, 2) == 16);
        assert(drum_qtz_strength_get() == 50 && drums.age == age + 2u);
        seq_stop();
    }

    /* A strength change after the early event fires must keep fired accounting intact. */
    {
        uint32_t before, blocks = 0;
        host_tracks_init();
        song.g[G_BPM] = 120; song.g[G_SWING] = 0; song.playing = 1;
        TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
        dstep_set(&TDRUM->dstep[2], 0, LV_NORM, 0);
        assert(drum_raw_offset_set(0, 2, -16));
        assert(drum_qtz_strength_set(0));
        seq_start();
        before = drums.age;
        while (drums.age == before && blocks++ < 520u) {
            memset(out, 0, sizeof out);
            mix_block(out, CTL);
        }
        assert(drums.age == before + 1u);
        assert(drum_raw_fired[0][2u % DRUM_GRID_RING] == 2u);
        assert(drum_qtz_strength_set(100));
        song.g[G_BPM] = 90; /* tempo changes never rewrite raw timing or refire the event */
        for (block = 0; block < 300u; ++block) {
            memset(out, 0, sizeof out);
            mix_block(out, CTL);
        }
        assert(drums.age == before + 1u); /* no repeat after moving the due point to the grid */
        assert(drum_raw_offset_get(0, 2) == -16);
        seq_stop();
    }
    /* Ratchets remain anchored to the actual quantized main hit and fire once per subdivision. */
    {
        uint32_t before, age_before;
        host_tracks_init();
        song.g[G_BPM] = 120; song.g[G_SWING] = 0; song.playing = 1;
        TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 64;
        dstep_set(&TDRUM->dstep[2], 0, LV_NORM, 1); /* primary + one ratchet */
        assert(drum_raw_offset_set(0, 2, -16) && drum_qtz_strength_set(50));
        seq_start(); before = drums.age;
        for (block = 0; block < 520u; ++block) {
            memset(out, 0, sizeof out);
            mix_block(out, CTL);
        }
        age_before = drums.age;
        assert(age_before == before + 2u);
        assert(drum_raw_offset_get(0, 2) == -16);
        seq_stop();
    }
    /* A3.4b musical grids are distinct, preserve raw cells and use one swung-grid correction. */
    {
        uint32_t u = BEAT_U / 4u;
        int32_t off;
        host_tracks_init();
        song.g[G_BPM] = 120; song.g[G_SWING] = 0; TDRUM->p[P_SDIV] = 2;
        assert(drum_qtz_strength_set(50));
        rec_grid_set(REC_GRID_OFF);
        off = drum_grid_due_offset(2, u, 2, 16, 50);
        assert(off == u / 8);                    /* OFF retains the A3.3 owning-step QTZ */
        assert(drum_qtz_strength_set(100));
        rec_grid_set(REC_GRID_4);
        assert(drum_grid_due_offset(2, u, 2, 0, 100) == -(int32_t)(2u * u));
        assert(drum_grid_due_offset(2, u, 2, 0, 50) == -(int32_t)u);
        rec_grid_set(REC_GRID_8);
        assert(drum_grid_due_offset(1, u, 2, 0, 100) == -(int32_t)u);
        rec_grid_set(REC_GRID_16);
        assert(drum_grid_due_offset(2, u, 2, 16, 100) == 0); /* exact A3.3 target */
        /* A quarter-note owner is not itself a sixteenth-note grid. At raw +20,
         * 1/16 QTZ=100 must reach the 1/16 point at +1/4 beat, not preserve +5/16. */
        assert(drum_grid_due_offset(2, BEAT_U, 0, 20, 100) == (int32_t)(BEAT_U / 4u));
        assert(drum_grid_due_offset(2, BEAT_U, 0, 20, 50) == (int32_t)(BEAT_U * 9u / 32u));
        rec_grid_set(REC_GRID_32);
        assert(drum_grid_due_offset(2, u, 2, 20, 100) == (int32_t)(u / 2u));
        assert(drum_raw_offset_set(0, 2, 20) && drum_raw_offset_get(0, 2) == 20);
        song.g[G_SWING] = 50; rec_grid_set(REC_GRID_8);
        assert(drum_grid_due_offset(1, u, 2, 0, 100) == -(int32_t)drum_grid_len(0, 2));
        assert(drum_raw_offset_get(0, 2) == 20);  /* grid/strength changes never rewrite capture */
        /* Exhaust every drum grid against all nine supported step divisions, all 64 positions,
         * and the full raw range. Intermediate strengths must be exact integer interpolation
         * between the immutable raw due time and the selected grid target. */
        {
            static const uint8_t strengths[] = {1, 25, 50, 75, 99};
            uint32_t g, d, a;
            int32_t r;
            for (g = REC_GRID_4; g <= REC_GRID_32; ++g) {
                rec_grid_set(g);
                for (d = 0; d < NDIV_STEP; ++d) {
                    uint32_t du = div_units(d);
                    for (a = 0; a < 64; ++a) for (r = DRUM_RAW_MIN; r <= DRUM_RAW_MAX; ++r) {
                        int32_t raw_due = drum_grid_due_offset(a, du, d, r, 0);
                        int32_t grid_due = drum_grid_due_offset(a, du, d, r, 100);
                        uint32_t q;
                        for (q = 0; q < sizeof strengths; ++q) {
                            int32_t delta = grid_due - raw_due;
                            int32_t pull = delta >= 0 ? (delta * strengths[q] + 50) / 100
                                                       : -((-delta * strengths[q] + 50) / 100);
                            int32_t actual = drum_grid_due_offset(a, du, d, r, strengths[q]);
                            /* 1/16 retains A3.3's integer raw-unit rounding; other grids use
                             * integer transport clocks. Both stay within one raw clock unit. */
                            int32_t err = actual - (raw_due + pull);
                            if (err < 0) err = -err;
                            assert((uint32_t)err <= du / 64u + 2u);
                        }
                    }
                }
            }
        }
        rec_grid_set(REC_GRID_OFF); song.g[G_SWING] = 0;
    }
    puts("drum raw timing storage and range PASS");
    return 0;
}
