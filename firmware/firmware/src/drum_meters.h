/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only audio statistics. UI envelope/peak hold never touches audio samples.
 * Lane 0..7: post lane FX/gain/mute, before master. 8/9: final DAC L/R.
 * Uses existing drums lane_buf; no additional audio/capture buffer. */
#ifndef FM1_DRUM_METERS_H
#define FM1_DRUM_METERS_H
#include <stdint.h>
#ifndef DRUM_METERS_ENABLE
#define DRUM_METERS_ENABLE 1
#endif
#define DRUM_METER_COUNT 10u
#define DRUM_METER_LEFT 8u
#define DRUM_METER_RIGHT 9u

static uint16_t dm_accum[DRUM_METER_COUNT], dm_previous[DRUM_METER_COUNT];
static volatile uint16_t dm_published[DRUM_METER_COUNT];
static volatile uint32_t dm_sequence;
static uint32_t dm_samples;
static uint16_t dm_target[DRUM_METER_COUNT], dm_envelope[DRUM_METER_COUNT], dm_hold[DRUM_METER_COUNT];
static uint32_t dm_hold_until[DRUM_METER_COUNT], dm_ui_time, dm_ui_sequence, dm_audio_time;
static uint8_t dm_ui_ready;

static uint16_t drum_meter_abs(int32_t x)
{
    uint32_t a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    return (uint16_t)(a > 32767u ? 32767u : a);
}

static void drum_meter_lane(uint32_t lane, const int32_t *samples, uint32_t n)
{
#if DRUM_METERS_ENABLE
    uint32_t i; uint16_t peak = 0;
    if (lane >= 8u) return;
    for (i=0;i<n;i++) {
        uint16_t a = drum_meter_abs(samples[i]);
        if (a > peak) peak = a;
    }
    if (peak > dm_accum[lane]) dm_accum[lane] = peak;
#else
    (void)lane; (void)samples; (void)n;
#endif
}

static void drum_meter_output(const int32_t *stereo, uint32_t n)
{
#if DRUM_METERS_ENABLE
    uint32_t i, k;
    for(i=0;i<n;i++) for(k=0;k<2;k++) {
        uint16_t a = drum_meter_abs(stereo[2u*i+k]);
        if(a > dm_accum[8u+k]) dm_accum[8u+k] = a;
    }
    dm_samples += n;
    /* At 44100 Hz: 735 samples = 60 Hz. Publish adjacent-window maxima:
     * short transients remain observable by the existing ~30 Hz LCD cadence. */
    if(dm_samples >= 735u) {
        dm_sequence++; /* odd: writer active; reader retries without masking IRQ */
        for(k=0;k<DRUM_METER_COUNT;k++) {
            dm_published[k] = dm_accum[k] > dm_previous[k] ? dm_accum[k] : dm_previous[k];
            dm_previous[k] = dm_accum[k]; dm_accum[k] = 0;
        }
        dm_samples -= 735u;
        dm_sequence++; /* even: atomically readable snapshot */
    }
#else
    (void)stereo; (void)n;
#endif
}

/* Main-loop only. No blocking, allocation, float or audio-buffer access. */
static void drum_meter_ui_tick(uint32_t now_ms)
{
    uint32_t k, attempt, elapsed, steps, sequence=0;
    uint16_t snapshot[DRUM_METER_COUNT];
    for(attempt=0;attempt<2u;attempt++) {
        sequence=dm_sequence;
        if(sequence & 1u) continue;
        for(k=0;k<DRUM_METER_COUNT;k++) snapshot[k]=dm_published[k];
        if(sequence==dm_sequence) {
            if(sequence != dm_ui_sequence) {
                for(k=0;k<DRUM_METER_COUNT;k++) dm_target[k]=snapshot[k];
                dm_ui_sequence=sequence; dm_audio_time=now_ms;
            }
            break;
        }
    }
    if(!dm_ui_ready) { dm_ui_time=now_ms; dm_ui_ready=1; }
    elapsed=now_ms-dm_ui_time;
    steps=elapsed/16u;
    if(steps>8u) steps=8u; /* bounded after a stalled UI; no catch-up storm */
    if(elapsed>128u) dm_ui_time=now_ms-128u;
    while(steps--) {
        dm_ui_time+=16u;
        for(k=0;k<DRUM_METER_COUNT;k++) {
            uint16_t target=now_ms-dm_audio_time>100u ? 0u : dm_target[k];
            uint16_t e=dm_envelope[k], peak=dm_hold[k];
            if(target>=e) e=target; /* <=16ms attack */
            else {
                uint16_t drop=(uint16_t)((e+15u)>>4); /* ~248ms release */
                e=e>drop ? (uint16_t)(e-drop) : 0;
                if(e<target) e=target;
                if(e<16u) e=0;
            }
            dm_envelope[k]=e;
            if(e>=peak && e) { peak=e; dm_hold_until[k]=dm_ui_time+180u; }
            else if((int32_t)(dm_ui_time-dm_hold_until[k])>=0) {
                uint16_t drop=(uint16_t)((peak+31u)>>5); /* ~500ms fall */
                peak=peak>drop ? (uint16_t)(peak-drop) : 0;
                if(peak<e) peak=e;
                if(peak<16u) peak=0;
            }
            dm_hold[k]=peak;
        }
    }
}

/* Integer quasi-log scale, ~6dB per octave. Silence/under -66dB = zero.
 * The display API is 0..1000; no log/sqrt work occurs in the audio callback. */
static uint16_t drum_meter_scale(uint16_t amplitude)
{
    uint32_t base=16u, octave=0u, value;
    if(amplitude<16u) return 0;
    while(base<16384u && amplitude>=base*2u) { base*=2u; octave++; }
    value=1u+(octave*100u)+((uint32_t)(amplitude-base)*100u/base);
    return (uint16_t)(value>1000u?1000u:value);
}
static uint16_t drum_meter_level(uint32_t index)
{ return index<DRUM_METER_COUNT ? drum_meter_scale(dm_envelope[index]) : 0; }
static uint16_t drum_meter_peak(uint32_t index)
{ return index<DRUM_METER_COUNT ? drum_meter_scale(dm_hold[index]) : 0; }
#endif
