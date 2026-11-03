/* SPDX-License-Identifier: GPL-3.0-only */
/* Integrated application MIDI test: existing UART byte parser -> queue ->
 * production sequencer/sample voices. No hardware register access. */
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define PROJECT_TEST_NO_PROCESS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void realtime(uint32_t status)
{
    um_byte(status);
    events_block(CTL); /* dispatch message */
    events_block(CTL); /* apply queued transport */
}

int main(void)
{
    static const uint8_t notes[8] = {36,35,38,39,42,46,44,37};
    uint32_t lane, age, held, before, p;
    host_tracks_init();
    song.sel = TRK_DRUM;
    song.g[G_DRCH] = 10;
    song.g[G_ROUTE] = 0;
    song.g[G_CHOTH] = 1; /* only configured channel */
    song.g[G_SYNC] = 2;  /* existing TRS source */
    song.playing = song.rec = 0;
    assert(drum_preset_select(0));
    events_block(CTL);
    for (lane = 0; lane < 8; lane++) {
        age = drums.age;
        um_byte(0x99); um_byte(notes[lane]); um_byte(100);
        events_block(CTL);
        assert(drums.age == age + 1);
        assert(lane_of_note(notes[lane]) == lane);
        age = drums.age;
        /* Running-status velocity-zero note-on is a note-off, no extra hit. */
        um_byte(notes[lane]); um_byte(0);
        events_block(CTL);
        assert(drums.age == age);
    }
    age = drums.age;
    um_byte(0x90); um_byte(36); um_byte(100);
    events_block(CTL);
    assert(drums.age == age); /* stale synth channel cannot hit drum lane */
    /* Source selection: USB realtime must not start TRS-synced transport. */
    mclk_event(0xFA, 1);
    assert(!transport_req && !song.playing);
    fm1_ms = 100;
    realtime(0xFA);
    assert(song.playing && clk_beat == 0);
    for (p = 0; p <= 24; p++) {
        fm1_ms = 100 + p * 20;
        realtime(0xF8);
    }
    assert(mclk_on());
    assert(mclk.n24 == 0 && song.g[G_BPM] == 125);
    held = clk_beat * BEAT_U + clk_pos;
    realtime(0xFC);
    assert(!song.playing && !song.rec && !rec_wait);
    before = clk_beat * BEAT_U + clk_pos;
    events_block(CTL);
    assert(clk_beat * BEAT_U + clk_pos == before);
    fm1_ms += 20;
    realtime(0xFB);
    assert(song.playing && clk_beat * BEAT_U + clk_pos >= held); /* resume, no restart */
    fm1_ms = mclk.last_ms + 499;
    assert(mclk_on());
    fm1_ms++;
    assert(!mclk_on());
    before = clk_pos;
    events_block(CTL);
    assert(clk_pos == (before + CTL * (uint32_t)song.g[G_BPM]) % BEAT_U);
    realtime(0xFA);
    assert(song.playing && TDRUM->seq_idx == 0 && clk_beat == 0);
    realtime(0xFC);
    assert(!song.playing);
    printf("RC0 MIDI PASS: 8 mapped lanes; running-status/velocity-zero; channel/source refusal; 24PPQN tempo; START/STOP/CONTINUE; 500ms internal-clock fallback\n");
    return 0;
}
