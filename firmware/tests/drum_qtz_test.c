/* SPDX-License-Identifier: GPL-3.0-only */
/* A3.3 exact integer QTZ contract; production seq.c is compiled by hostsim.c below. */
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define PROJECT_TEST_NO_PROCESS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static int32_t reference_render(int32_t raw, uint32_t strength)
{
    uint32_t magnitude = (uint32_t)(raw < 0 ? -raw : raw);
    uint32_t numerator = magnitude * (100u - strength);
    uint32_t rounded = numerator / 100u;
    if (numerator % 100u >= 50u) rounded++;
    return raw < 0 ? -(int32_t)rounded : (int32_t)rounded;
}

int main(void)
{
    static const uint8_t strengths[] = {0, 1, 25, 50, 75, 99, 100};
    int8_t raw_before[DRUM_RAW_LANES][NSTEP];
    uint32_t raw, strength, lane, step, i;
    int32_t previous, rendered;

    assert(drum_qtz_strength_set(0));
    assert(drum_qtz_strength_get() == 0);
    for (i = 0; i < sizeof strengths / sizeof strengths[0]; ++i) {
        strength = strengths[i];
        assert(drum_qtz_strength_set((int32_t)strength));
        assert(drum_qtz_strength_get() == strength);
        for (raw = (uint32_t)(int32_t)DRUM_RAW_MIN; (int32_t)raw <= DRUM_RAW_MAX; ++raw) {
            int32_t signed_raw = (int32_t)raw;
            rendered = drum_qtz_rendered_offset(signed_raw, strength);
            assert(rendered == reference_render(signed_raw, strength));
            if (signed_raw >= -DRUM_RAW_MAX && signed_raw <= DRUM_RAW_MAX)
                assert(drum_qtz_rendered_offset(-signed_raw, strength) == -rendered);
            if (strength == 0) assert(rendered == signed_raw);
            if (strength == 100) assert(rendered == 0);
        }
    }

    /* Half cases round away from zero, with exact sign symmetry. */
    assert(drum_qtz_rendered_offset(1, 50) == 1);
    assert(drum_qtz_rendered_offset(-1, 50) == -1);
    assert(drum_qtz_rendered_offset(3, 50) == 2);
    assert(drum_qtz_rendered_offset(-3, 50) == -2);
    assert(drum_qtz_rendered_offset(5, 50) == 3);
    assert(drum_qtz_rendered_offset(-5, 50) == -3);
    assert(drum_qtz_rendered_offset(DRUM_RAW_MIN, 50) == -16);
    assert(drum_qtz_rendered_offset(DRUM_RAW_MAX, 50) == 16);

    /* The rounded magnitude cannot rise as strength increases from 0 through 100. */
    for (raw = (uint32_t)(int32_t)DRUM_RAW_MIN; (int32_t)raw <= DRUM_RAW_MAX; ++raw) {
        int32_t signed_raw = (int32_t)raw;
        previous = signed_raw < 0 ? -signed_raw : signed_raw;
        for (strength = 0; strength <= 100; ++strength) {
            rendered = drum_qtz_rendered_offset(signed_raw, strength);
            assert((rendered < 0 ? -rendered : rendered) <= previous);
            previous = rendered < 0 ? -rendered : rendered;
        }
    }

    /* Invalid setter values are rejected without changing the accepted parameter. */
    assert(drum_qtz_strength_set(37));
    assert(!drum_qtz_strength_set(-1));
    assert(drum_qtz_strength_get() == 37);
    assert(!drum_qtz_strength_set(101));
    assert(drum_qtz_strength_get() == 37);
    assert(!drum_qtz_strength_set(INT32_MAX));
    assert(drum_qtz_strength_get() == 37);
    assert(drum_qtz_rendered_offset(17, 101) == 17); /* invalid direct input safely means OFF */

    /* Strength changes are reversible views of immutable raw timing across all cells. */
    for (lane = 0; lane < DRUM_RAW_LANES; ++lane)
        for (step = 0; step < NSTEP; ++step)
            assert(drum_raw_offset_set(lane, step,
                (int32_t)((lane * 19u + step * 7u) % 64u) + DRUM_RAW_MIN));
    memcpy(raw_before, drum_raw_offset, sizeof raw_before);
    {   static const uint8_t sequence[] = {0, 100, 50, 0};
        for (i = 0; i < sizeof sequence; ++i) {
            assert(drum_qtz_strength_set(sequence[i]));
            for (lane = 0; lane < DRUM_RAW_LANES; ++lane)
                for (step = 0; step < NSTEP; ++step)
                    assert(drum_qtz_rendered_offset(drum_raw_offset_get(lane, step),
                               drum_qtz_strength_get()) == reference_render(raw_before[lane][step], sequence[i]));
            assert(!memcmp(raw_before, drum_raw_offset, sizeof raw_before));
        }
    }

    /* A single step has eight independent raw inputs but one shared QTZ strength. */
    assert(drum_qtz_strength_set(50));
    for (lane = 0; lane < DRUM_RAW_LANES; ++lane) {
        int32_t input = (int32_t)lane * 9 - 32;
        assert(drum_raw_offset_set(lane, 7, input));
        assert(drum_qtz_rendered_offset(drum_raw_offset_get(lane, 7), drum_qtz_strength_get()) ==
               reference_render(input, 50));
    }
    assert(drum_raw_offset_get(0, 7) == -32 && drum_raw_offset_get(7, 7) == 31);

    /* Defensive input bounds are explicit; stored production values remain in range. */
    assert(drum_qtz_rendered_offset(-1000, 0) == DRUM_RAW_MIN);
    assert(drum_qtz_rendered_offset(1000, 0) == DRUM_RAW_MAX);
    puts("drum QTZ integer math, symmetric rounding, monotonicity and raw invariance PASS");
    return 0;
}
