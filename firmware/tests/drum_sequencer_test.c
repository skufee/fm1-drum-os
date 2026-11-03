/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum-only clock/playback regression for the one-track production runtime. */
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
    static const uint8_t note[8] = {36,35,38,39,42,46,44,37};
    uint32_t hits[8] = {0}, i, b, wrap_seen = 0, max_step = 0, last_step = 0;
    uint32_t prev_age, later;
    int32_t out[CTL * 2];

    host_tracks_init();
    song.sel = TRK_DRUM;
    song.playing = 0;
    transport_req = 0;
    song.g[G_BPM] = 120;
    seq_start();
    song.rec = 1u << TRK_DRUM;
    i = rec_target(TDRUM, &later) % NSTEP;
    dstep_set(&TDRUM->dstep[i], 7, LV_NORM, 0);
    drum_input(0, LV_NORM, 0, 1);
    drum_input(1, LV_HARD, 0, 1);
    assert(dstep_has(&TDRUM->dstep[i], 7));
    assert(dstep_has(&TDRUM->dstep[i], 0));
    assert(dstep_has(&TDRUM->dstep[i], 1));
    assert(dstep_lvl(&TDRUM->dstep[i], 1) == LV_HARD);
    song.rec = 0;
    seq_stop();
    steps_clear(TDRUM);

    TDRUM->p[P_SLEN] = 64;
    TDRUM->p[P_SDIV] = 2; /* sixteenth notes */
    for (i = 0; i < 64; i += 8)
        dstep_set(&TDRUM->dstep[i], (i / 8) & 7u, LV_NORM, 0);
    assert(NTRK == 1 && TDRUM == &trk[0]);
    assert(NDRUM == 6);

    seq_start();
    assert(song.playing);
    prev_age = drums.age;
    /* Run long enough to traverse all 64 sequencer steps and wrap. */
    for (b = 0; b < 40u * FS / CTL; b++) {
        memset(out, 0, sizeof out);
        mix_block(out, CTL);
        if (TDRUM->seq_idx > max_step) max_step = TDRUM->seq_idx;
        if (max_step >= 60u && TDRUM->seq_idx < last_step)
            wrap_seen = 1;
        last_step = TDRUM->seq_idx;
        if (drums.age != prev_age) {
            uint32_t v;
            for (v = 0; v < NDRUM; v++) {
                uint32_t k;
                if (drums.v[v].age <= prev_age)
                    continue;
                for (k = 0; k < 8; k++)
                    if (drums.v[v].note == note[k]) hits[k]++;
            }
            prev_age = drums.age;
        }
    }
    for (i = 0; i < 8; i++) assert(hits[i] >= 1u); /* each logical lane is audible at least once */
    assert(wrap_seen);
    seq_stop();
    assert(!song.playing);

    printf("drum sequencer: record/overdub, 8 lanes, 64 steps, two wraps, START/STOP PASS\n");
    return 0;
}
