/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Drum-only runtime adapter: eight logical lanes share six SLOOP sample voices.
 * Each lane selects one of the 55 embedded ADPCM samples; no synth drum generator
 * is present in this renderer. One-shots ignore note-off, and a closed/pedal hat
 * chokes an open hat. The existing audio ISR renders these voices through the
 * shared mixer, envelopes, declick and FX-send path. */
static void drum_mark_custom(void);
#include "drum_meters.h"
#include "drum_fx.c"
#define NDRUM 6
#define DRUM_KITS 6u
#include "drum_fx.h"
#define DRUM_DEFAULT_KIT 0u
#include "drum_presets.h"
static const char *const DRUM_KIT_NAMES[DRUM_KITS] = {"BOOM BAP", "DIRTY", "DUSTY", "TIGHT", "TRAP", "AIRY"};
static const char *const DRUM_KIT_STYLES[DRUM_KITS] = {"FACTORY", "FACTORY", "FACTORY", "FACTORY", "FACTORY", "FACTORY"};
static uint32_t drum_kit(void) { return (uint32_t)clamp(TDRUM->p[P_E0], 0, DRUM_KITS - 1u); }
static volatile uint8_t drum_apply_guard;
static volatile uint8_t drum_kit_source = DRUM_DEFAULT_KIT;
static uint8_t drum_user_valid[DRUM_USER_KITS];
static uint8_t drum_user_pads[DRUM_USER_KITS][DRUM_RAW_LANES][8];
/* USER snapshots include eight lane FX and the master, never DSP history. */
static drum_fx_config_t drum_user_fx[DRUM_USER_KITS][9];
#include "drum_user_samples.h"
static void drum_mark_custom(void) { if (!drum_apply_guard) drum_kit_source = DRUM_KIT_SOURCE_CUSTOM; }
static volatile int16_t drum_pad_edit[8][DRUM_EDIT_COUNT] = {
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0},
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0}
}; /* tune semitones, end percent, decay amount, start percent; persisted by project codec */
int16_t drum_pad_edit_get(uint32_t lane, uint32_t param)
{
    if (param >= DRUM_EDIT_COUNT)
        return 0;
    if (lane >= 8u)
        return param == DRUM_EDIT_END ? 100 : 0;  /* lanes 9..16 keep the upstream full-sample behavior */
    return drum_pad_edit[lane][param];
}
void drum_pad_edit_set(uint32_t lane, uint32_t param, int32_t value)
{
    static const int16_t lo[DRUM_EDIT_COUNT] = {-24, 1, 0, 0};
    static const int16_t hi[DRUM_EDIT_COUNT] = {24, 100, 127, 99};
    if (lane >= 8u || param >= DRUM_EDIT_COUNT)
        return;
    fm1_irq_off();
    if (param == DRUM_EDIT_START)
        value = clamp(value, 0, drum_pad_edit[lane][DRUM_EDIT_END] - 1);
    if (param == DRUM_EDIT_END)
        value = clamp(value, drum_pad_edit[lane][DRUM_EDIT_START] + 1, 100);
    drum_pad_edit[lane][param] = (int16_t)clamp(value, lo[param], hi[param]);
    fm1_irq_on();
    drum_mark_custom();
}
static volatile int16_t drum_pad_mix[8][DRUM_MIX_COUNT] = {
    {127, 0, 0}, {127, 0, 0}, {127, 0, 0}, {127, 0, 0},
    {127, 0, 0}, {127, 0, 0}, {127, 0, 0}, {127, 0, 0}
}; /* session-only: level 0..127, pan -64..63, mute 0/1 */
int16_t drum_pad_mix_get(uint32_t lane, uint32_t param)
{
    if (param >= DRUM_MIX_COUNT)
        return 0;
    if (lane >= 8u)
        return param == DRUM_MIX_LEVEL ? 127 : 0;
    return drum_pad_mix[lane][param];
}
void drum_pad_mix_set(uint32_t lane, uint32_t param, int32_t value)
{
    static const int16_t lo[DRUM_MIX_COUNT] = {0, -64, 0};
    static const int16_t hi[DRUM_MIX_COUNT] = {127, 63, 1};
    if (lane >= 8u || param >= DRUM_MIX_COUNT)
        return;
    fm1_irq_off();
    drum_pad_mix[lane][param] = (int16_t)clamp(value, lo[param], hi[param]);
    fm1_irq_on();
    drum_mark_custom();
}
int drum_kit_has_sample_edit(uint32_t kit) { return kit < DRUM_KITS; }

/* Stable A3.4c sample IDs are generated from the natural-sorted filenames.
 * Presets assign these embedded samples to the eight product lanes; the sample
 * library's other IDs remain addressable through drum_custom_sample_id_set(). */
volatile uint8_t drum_custom_sample_ids[DRUM_RAW_LANES] = {
    SMP_CUSTOM_ID_KICK1, SMP_CUSTOM_ID_KICK2, SMP_CUSTOM_ID_SNARE1, SMP_CUSTOM_ID_PERC1,
    SMP_CUSTOM_ID_HAT1, SMP_CUSTOM_ID_OH1, SMP_CUSTOM_ID_HAT2, SMP_CUSTOM_ID_SNARE2
};

uint32_t drum_custom_sample_count(void) { return SMP_CUSTOM_SAMPLE_COUNT; }
uint32_t drum_custom_sample_id_get(uint32_t lane)
{
    return lane < DRUM_RAW_LANES ? drum_custom_sample_ids[lane] : UINT32_MAX;
}
int drum_custom_sample_id_set(uint32_t lane, uint32_t sample_id)
{
    if (lane >= DRUM_RAW_LANES || sample_id >= DRUM_SAMPLE_COUNT || (sample_id>=55u && !drum_sample_fingerprint(sample_id)))
        return 0;
    fm1_irq_off();
    drum_custom_sample_ids[lane] = (uint8_t)sample_id;
    drum_sample_refs[lane] = drum_sample_fingerprint(sample_id);
    fm1_irq_on();
    drum_mark_custom();
    return 1;
}
const char *drum_custom_sample_name(uint32_t sample_id)
{
    return sample_id < SMP_CUSTOM_SAMPLE_COUNT ? SMP_CUSTOM_SAMPLE_NAMES[sample_id] : drum_user_sample_name(sample_id);
}
/* Persistence may retain missing/stale references; playback then stays silent. */
int drum_sample_reference_set(uint32_t lane,uint32_t id,uint32_t fingerprint)
{
    if(lane>=8u||id>=DRUM_SAMPLE_COUNT||(id<55u?fingerprint!=0u:fingerprint==0u))return 0;
    fm1_irq_off();drum_custom_sample_ids[lane]=(uint8_t)id;drum_sample_refs[lane]=fingerprint;fm1_irq_on();return 1;
}
static int drum_sample_slot_referenced(uint32_t slot)
{
    uint32_t k,lane;if(slot>=DRUM_USER_SAMPLE_COUNT)return 1;
    for(lane=0;lane<8;lane++)if(drum_custom_sample_ids[lane]==55u+slot)return 1;
    for(k=0;k<4;k++)if(drum_user_valid[k])for(lane=0;lane<8;lane++)if(drum_user_pads[k][lane][0]==55u+slot)return 1;
    return 0;
}
int drum_preset_select(uint32_t preset)
{
    uint32_t lane;
    const uint8_t *p;
    if (preset >= DRUM_PRESET_COUNT)
        return 0;
    p = DRUM_PRESETS[preset];
    /* One short, bounded update prevents the audio ISR from observing a half-applied kit. */
    fm1_irq_off();
    TDRUM->p[P_E0] = (int16_t)preset;
    for (lane = 0; lane < 8u; lane++) {
        drum_custom_sample_ids[lane] = p[lane];
        drum_sample_refs[lane] = 0;
        drum_pad_mix[lane][DRUM_MIX_LEVEL] = 127;
        drum_pad_mix[lane][DRUM_MIX_PAN] = 0;
        drum_pad_mix[lane][DRUM_MIX_MUTE] = 0;
        drum_pad_edit[lane][DRUM_EDIT_TUNE] = 0;
        drum_pad_edit[lane][DRUM_EDIT_END] = 100;
        drum_pad_edit[lane][DRUM_EDIT_DECAY] = 0;
        drum_pad_edit[lane][DRUM_EDIT_START] = 0;
    }
    drum_kit_source = (uint8_t)preset;
    /* Legacy processing must not colour a neutral factory load. */
    TDRUM->p[P_DIST]=TDRUM->p[P_CHOR]=TDRUM->p[P_DLY]=TDRUM->p[P_REV]=0;
    TDRUM->p[P_ITYPE]=TDRUM->p[P_IMIX]=TDRUM->p[P_TFLT]=0;
    song.g[G_DUST]=song.g[G_DUCK]=song.g[G_FILT]=0;
    song.g[G_DRREV]=song.g[G_DRDLY]=song.g[G_TXLVL]=0;
    fm1_irq_on();
    drum_fx_reset();
    return 1;
}
uint32_t drum_preset_count(void) { return DRUM_PRESET_COUNT; }
const char *drum_preset_name(uint32_t preset) { return preset < DRUM_PRESET_COUNT ? DRUM_KIT_NAMES[preset] : 0; }
uint32_t drum_preset_compat_id(int32_t stored_id)
{
    /* Runtime selector only. Legacy LOAD uses drum_legacy_preset_apply(), never this fallback. */
    return stored_id >= 0 && stored_id < (int32_t)DRUM_PRESET_COUNT ? (uint32_t)stored_id : DRUM_DEFAULT_KIT;
}

/* Restore historical mappings while preserving the serialized pad parameters.
 * CUSTOM is deliberate: old factory indices do not identify RC0 factories. */
static void drum_legacy_preset_apply(int32_t id)
{
    uint32_t lane;
    if (id >= 0 && id < 16) {
        fm1_irq_off();
        for (lane = 0; lane < 8u; lane++) {drum_custom_sample_ids[lane] = DRUM_LEGACY_PRESETS[id][lane];drum_sample_refs[lane]=0;}
        fm1_irq_on();
    } else {
        drum_preset_select(DRUM_DEFAULT_KIT);
    }
    drum_kit_source = DRUM_KIT_SOURCE_CUSTOM;
    drum_fx_reset();
}
static void drum_user_fx_export(drum_fx_config_t out[DRUM_USER_KITS][9]) { memcpy(out, drum_user_fx, sizeof drum_user_fx); }
static void drum_user_fx_import(const drum_fx_config_t in[DRUM_USER_KITS][9]) { memcpy(drum_user_fx, in, sizeof drum_user_fx); }

uint32_t drum_kit_source_get(void) { return drum_kit_source; }
void drum_kit_source_set(uint32_t source)
{
    drum_kit_source = (uint8_t)(source < DRUM_KITS ? source : DRUM_KIT_SOURCE_CUSTOM);
}

void drum_user_kit_export(uint8_t valid[DRUM_USER_KITS], uint8_t pads[DRUM_USER_KITS][DRUM_RAW_LANES][8])
{
    if (!valid || !pads) return;
    fm1_irq_off();
    memcpy(valid, drum_user_valid, sizeof drum_user_valid);
    memcpy(pads, drum_user_pads, sizeof drum_user_pads);
    fm1_irq_on();
}

void drum_user_kit_import(const uint8_t valid[DRUM_USER_KITS], const uint8_t pads[DRUM_USER_KITS][DRUM_RAW_LANES][8])
{
    uint32_t k, lane;
    if (!valid || !pads) return;
    fm1_irq_off();
    memset(drum_user_valid, 0, sizeof drum_user_valid);
    memset(drum_user_pads, 0, sizeof drum_user_pads);
    memset(drum_user_fx, 0, sizeof drum_user_fx);
    memset(drum_user_sample_refs,0,sizeof drum_user_sample_refs);
    for (k = 0; k < DRUM_USER_KITS; k++) {
        int good = valid[k] <= 1u;
        for (lane = 0; lane < DRUM_RAW_LANES && good; lane++) {
            const uint8_t *p = pads[k][lane];
            good = !valid[k] || (p[0] < DRUM_SAMPLE_COUNT && p[1] <= 127u && (int8_t)p[2] >= -64 &&
                   (int8_t)p[2] <= 63 && p[3] <= 1u && (int8_t)p[4] >= -24 && (int8_t)p[4] <= 24 &&
                   p[5] >= 1u && p[5] <= 100u && p[6] <= 127u && p[7] < p[5]);
        }
        if (good && valid[k]) {
            drum_user_valid[k] = 1;
            memcpy(drum_user_pads[k], pads[k], sizeof drum_user_pads[k]);
        }
    }
    fm1_irq_on();
}

static void drum_user_kit_capture_slot(uint32_t slot)
{
    uint32_t lane;
    if (slot >= DRUM_USER_KITS) return;
    drum_user_valid[slot] = 1;
    for (lane = 0; lane < 9u; lane++) drum_user_fx[slot][lane] = drum_fx_get(lane);
    for (lane = 0; lane < DRUM_RAW_LANES; lane++) {
        drum_user_pads[slot][lane][0] = drum_custom_sample_ids[lane];
        drum_user_sample_refs[slot][lane] = drum_sample_refs[lane];
        drum_user_pads[slot][lane][1] = (uint8_t)drum_pad_mix_get(lane, DRUM_MIX_LEVEL);
        drum_user_pads[slot][lane][2] = (uint8_t)drum_pad_mix_get(lane, DRUM_MIX_PAN);
        drum_user_pads[slot][lane][3] = (uint8_t)drum_pad_mix_get(lane, DRUM_MIX_MUTE);
        drum_user_pads[slot][lane][4] = (uint8_t)drum_pad_edit_get(lane, DRUM_EDIT_TUNE);
        drum_user_pads[slot][lane][5] = (uint8_t)drum_pad_edit_get(lane, DRUM_EDIT_END);
        drum_user_pads[slot][lane][6] = (uint8_t)drum_pad_edit_get(lane, DRUM_EDIT_DECAY);
        drum_user_pads[slot][lane][7] = (uint8_t)drum_pad_edit_get(lane, DRUM_EDIT_START);
    }
}

int drum_user_kit_save(uint32_t slot)
{
    if (slot >= DRUM_USER_KITS) return 0;
    fm1_irq_off();
    drum_user_kit_capture_slot(slot);
    drum_kit_source = DRUM_KIT_SOURCE_CUSTOM;
    fm1_irq_on();
    return 1;
}

int drum_user_kit_load(uint32_t slot)
{
    uint32_t lane;
    if (slot >= DRUM_USER_KITS || !drum_user_valid[slot]) return 0;
    drum_apply_guard = 1;
    fm1_irq_off();
    for (lane = 0; lane < DRUM_RAW_LANES; lane++) {
        drum_custom_sample_ids[lane] = drum_user_pads[slot][lane][0] < DRUM_SAMPLE_COUNT ? drum_user_pads[slot][lane][0] : SMP_CUSTOM_ID_KICK1;
        drum_sample_refs[lane] = drum_custom_sample_ids[lane]>=55u ? drum_user_sample_refs[slot][lane] : 0;
        drum_pad_mix[lane][DRUM_MIX_LEVEL] = drum_user_pads[slot][lane][1];
        drum_pad_mix[lane][DRUM_MIX_PAN] = (int8_t)drum_user_pads[slot][lane][2];
        drum_pad_mix[lane][DRUM_MIX_MUTE] = drum_user_pads[slot][lane][3] ? 1 : 0;
        drum_pad_edit[lane][DRUM_EDIT_TUNE] = (int8_t)drum_user_pads[slot][lane][4];
        drum_pad_edit[lane][DRUM_EDIT_END] = drum_user_pads[slot][lane][5];
        drum_pad_edit[lane][DRUM_EDIT_DECAY] = drum_user_pads[slot][lane][6];
        drum_pad_edit[lane][DRUM_EDIT_START] = drum_user_pads[slot][lane][7];
    }
    drum_kit_source = DRUM_KIT_SOURCE_CUSTOM;
    fm1_irq_on();
    drum_apply_guard = 0;
    for (lane = 0; lane < 9u; lane++) drum_fx_restore(lane, &drum_user_fx[slot][lane]);
    return 1;
}

int drum_user_kit_reset_current(void)
{
    return drum_preset_select(drum_preset_compat_id(TDRUM->p[P_E0]));
}

static struct {
    voice_t v[NDRUM];
    uint32_t age;
    int16_t set;                 /* legacy SLOOP sample-set lookup for non-drum builds */
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    int32_t filter[NDRUM], env[NDRUM];
    uint32_t end[NDRUM];
    uint8_t decay_shift[NDRUM];    /* 0 = no added decay envelope, else 8..24 */
    uint8_t attack[NDRUM];         /* 32-output-sample fade only for a nonzero sample START */
    uint8_t lane_level[NDRUM], lane_mute[NDRUM];
    uint8_t preview[NDRUM];       /* candidate audition bypasses lane mute/FX, still one of six voices */
    int8_t lane_pan[NDRUM];
    volatile uint16_t hits;      /* bit per lane hit since the UI last looked (pads, key LEDs) */
    volatile uint8_t kick;       /* a kick was hit (fx.c DUCK) */
    int32_t a0, a1;              /* the drum track's mute / solo attenuation over this block (fx.c), Q15, 0 = heard */
} drums = {.set = -2};

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* ------------------------------------------------------------ lanes --- */
/* Historical note/key translation helpers retained for MIDI and upstream UI
 * compatibility. Drum pattern ownership remains the eight logical product lanes. */
static const uint8_t LANE_NOTE[DRUM_LANES] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static const char *const LANE_NAME[DRUM_LANES] = {
    "KICK", "KICK 2", "SNARE", "CLAP", "HAT", "OPEN HAT", "PEDAL", "RIM",
    "SNARE 2", "LOW TOM", "HI TOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"};
static const char *const LANE_SHORT[DRUM_LANES] = {           /* 5 characters: tiles, dials */
    "kick", "kick2", "snare", "clap", "hat", "open", "pedal", "rim",
    "snr 2", "tom l", "tom h", "crash", "ride", "shake", "conga", "bell"};
/* Legacy MIDI note -> historical lane mapping; unsupported lanes are rejected by drum_on(). */
static const uint8_t LANE_OF_GM[81 - 35 + 1] = {
    /* 35 */ 1, 0, 7, 2, 3, 8, 9, 4, 9, 6,
    /* 45 */ 9, 5, 10, 10, 11, 10, 12, 11, 12, 13,
    /* 55 */ 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
    /* 65 */ 14, 14, 15, 15, 13, 13, 15, 15, 13, 13,
    /* 75 */ 7, 7, 7, 14, 14, 15, 15};
static uint32_t lane_of_note(uint32_t note)
{
    return note < 35u ? 0u : note > 81u ? 13u : LANE_OF_GM[note - 35u];
}
/* the lane of key k (0 = F3 .. 26 = G5): white keys in order, a black key the white key left of it */
static uint32_t lane_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) & 15u;
}
/* the white key of lane l (key index), for the key LEDs */
static uint32_t key_of_lane(uint32_t l)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    return (l / 7u) * 12u + K[l % 7u];
}

/* drum steps: a lane's bit, level, ratchet */
static int dstep_has(const dstep_t *s, uint32_t l) { return (s->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }
static uint32_t dstep_lvl(const dstep_t *s, uint32_t l) { return (s->lvl[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_rat(const dstep_t *s, uint32_t l) { return (s->rat[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_mask(const dstep_t *s) { return (uint32_t)s->on[0] | (uint32_t)s->on[1] << 8; }
static void dstep_set(dstep_t *s, uint32_t l, uint32_t lvl, uint32_t rat)   /* lane on, with its level and ratchet */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] |= (uint8_t)(1u << (l & 7u));
    s->lvl[b] = (uint8_t)((s->lvl[b] & ~(3u << sh)) | (lvl & 3u) << sh);
    s->rat[b] = (uint8_t)((s->rat[b] & ~(3u << sh)) | (rat & 3u) << sh);
}
static void dstep_clr(dstep_t *s, uint32_t l)                                /* lane off */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] &= (uint8_t)~(1u << (l & 7u));
    s->lvl[b] &= (uint8_t)~(3u << sh);
    s->rat[b] &= (uint8_t)~(3u << sh);
}

static const smp_zone_t *drum_zone_of(const voice_t *v)
{
    uint32_t id=(uint32_t)v->s[4];
    return id>=55u&&id<DRUM_SAMPLE_COUNT?drum_user_zone(id-55u):&SMP_ZONES[id<55u?id:0u];
}

/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}

/* The metronome (GLO > GLOBAL > CLICK, and the REC count-in), 2.4.1: a voice of its own, mixed after the drum
 * track (fx.c mix_block), so it is heard with every sample preset, whatever
 * the drum track's mute, a solo, its filter or slicer. A short sine tick: 1568 Hz on a bar's first beat, 1047 Hz
 * on the others, about 12 ms to fade by 1/e; its level follows GLO > DRUMS > LVL (100: as loud as the old
 * wood block). It costs a few hundred cycles a block while it sounds, nothing after */
static struct { uint32_t ph, inc; int32_t env, gain; } click;   /* env: Q30, 0 = silent */
static uint32_t click_n;                         /* clicks so far, and the last one's accent (the host tests count them) */
static uint8_t click_acc;
static void click_on(int accent)
{
    click.ph = 0;
    click.inc = accent ? (uint32_t)(((uint64_t)1568u << 32) / FS) : (uint32_t)(((uint64_t)1047u << 32) / FS);
    click.gain = accent ? 20000 : 12000;
    click.env = 1 << 30;
    click_acc = (uint8_t)(accent != 0);
    click_n++;
}
static void click_render(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    int32_t g;
    if (!click.env)
        return;
    g = (int32_t)((int64_t)click.gain * song.g[G_DRLVL] / 100);
    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)sine_i(click.ph) * (click.env >> 15) >> 15) * g >> 15);
        ml[i] += v;
        mr[i] += v;
        click.ph += click.inc;
        click.env -= click.env >> 9;                 /* about 12 ms to 1/e */
    }
    if (click.env < (1 << 18))
        click.env = 0;                                /* (-72 dB: done, about 100 ms) */
}

/* Exact percent checkpoint: generated from the very same encoded SMP_DATA.
 * Restore the state BEFORE its nibble, then use the unchanged IMA decoder.
 * O(1), one ordinary prime decode, no realtime scan and no RAM seek index. */
static uint32_t drum_start_prime(const smp_zone_t *z, uint32_t zid, voice_t *v, uint32_t percent)
{
    uint32_t pos = (z->n * percent) / 100u;
    v->ph[0] = pos;
    v->s[0] = zid<55u?SMP_START_PRED[zid][percent]:drum_usr_pred[zid-55u][percent];
    v->s[1] = zid<55u?SMP_START_IDX[zid][percent]:drum_usr_idx[zid-55u][percent];
    v->s[3] = sample_next(z, v, 0);
    return pos;
}

static void drum_on(uint32_t note, uint32_t vel)
{
    voice_t *v = &drums.v[0];
    uint32_t i, lane = lane_of_note(note);
    int32_t zid = -1, pad_tune = 0;
    if (note != 76u && note != 77u)                 /* the pads and key LEDs (not the click's wood block) */
        drums.hits |= (uint16_t)(1u << lane_of_note(note));
    if (note == 35u || note == 36u)
        drums.kick = 1;                             /* (DUCK) */
    if (note == 76u || note == 77u || lane >= DRUM_RAW_LANES)
        return;
    zid = (int32_t)drum_custom_sample_ids[lane];
    if (zid < 0 || (uint32_t)zid >= DRUM_SAMPLE_COUNT || !drum_sample_available((uint32_t)zid,drum_sample_refs[lane]))
        return;
    if ((uint32_t)zid < 14u)                       /* assigned closed hat, stable HAT IDs 0..13 */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].s[4] >= 26 && drums.v[i].s[4] <= 28) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];          /* fade what it was playing, not a step */
            }
    for (i = 0; i < NDRUM; i++) {                   /* free voice, else the oldest */
        if (!drums.v[i].active) {
            v = &drums.v[i];
            break;
        }
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    if (v->active)
        drums.tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    v->s[4] = zid;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    {
        const smp_zone_t *z = drum_zone_of(v);
        pad_tune = drum_pad_edit_get(lane, DRUM_EDIT_TUNE);
        int32_t end = drum_pad_edit_get(lane, DRUM_EDIT_END);
        uint32_t start = (uint32_t)drum_pad_edit_get(lane, DRUM_EDIT_START);
        uint32_t vi = (uint32_t)(v - drums.v);
        uint32_t pos = drum_start_prime(z, (uint32_t)zid, v, start);
        drums.attack[vi] = start ? 32u : 0u;
        v->s[5] = (int32_t)z->rate;
        drums.end[vi] = (z->n * (uint32_t)end) / 100u;
        if (drums.end[vi] <= pos) drums.end[vi] = pos + 1u;
        {
            uint32_t decay = (uint32_t)drum_pad_edit_get(lane, DRUM_EDIT_DECAY);
            drums.decay_shift[vi] = decay ? (uint8_t)(8u + decay * 6u / 127u) : 0u;
        }
    }
    {
        uint32_t vi = (uint32_t)(v - drums.v);
        drums.lane_level[vi] = (uint8_t)drum_pad_mix_get(lane, DRUM_MIX_LEVEL);
        drums.lane_pan[vi] = (int8_t)drum_pad_mix_get(lane, DRUM_MIX_PAN);
        drums.lane_mute[vi] = (uint8_t)drum_pad_mix_get(lane, DRUM_MIX_MUTE);
        drums.preview[vi] = 0;
        drums.filter[vi] = 0;
        drums.env[vi] = 32767;
    }
    if (pad_tune)
        v->s[5] = (int32_t)(((int64_t)v->s[5] * pow2_q16(pad_tune * 16)) >> 16);
}

/* Application-only candidate audition. Temporarily use the selected lane under the same
 * bounded update lock; drum_on captures its sample/gain into a normal voice.
 * The live assignments/parameters/source and sequencer are never changed. */
void drum_preview(uint32_t sample_id, uint32_t level)
{
    uint32_t lane=drum_pad_sel<8u?drum_pad_sel:0u;
    uint8_t old_id; uint32_t old_ref; int16_t old_mix[DRUM_MIX_COUNT],old_edit[DRUM_EDIT_COUNT];
    if(sample_id>=DRUM_SAMPLE_COUNT || (sample_id>=55u && !drum_sample_fingerprint(sample_id)) || song.rec) return;
    fm1_irq_off(); old_id=drum_custom_sample_ids[lane]; old_ref=drum_sample_refs[lane];
    memcpy(old_mix,(const void *)drum_pad_mix[lane],sizeof old_mix);
    memcpy(old_edit,(const void *)drum_pad_edit[lane],sizeof old_edit);
    drum_custom_sample_ids[lane]=(uint8_t)sample_id;drum_sample_refs[lane]=drum_sample_fingerprint(sample_id);
    drum_pad_mix[lane][0]=(int16_t)clamp((int32_t)level,0,127);
    drum_pad_mix[lane][1]=drum_pad_mix[lane][2]=0;
    drum_pad_edit[lane][0]=0; drum_pad_edit[lane][1]=100; drum_pad_edit[lane][2]=0;
    drum_pad_edit[lane][DRUM_EDIT_START]=0;
    drum_on(LANE_NOTE[lane],100);
    { uint32_t vi; for(vi=0;vi<NDRUM;vi++)
        if(drums.v[vi].active && drums.v[vi].age==drums.age) drums.preview[vi]=1;
    }
    drum_custom_sample_ids[lane]=old_id;drum_sample_refs[lane]=old_ref;
    memcpy((void *)drum_pad_mix[lane],old_mix,sizeof old_mix);
    memcpy((void *)drum_pad_edit[lane],old_edit,sizeof old_edit);
    fm1_irq_on();
}

/* adds the drums into the dry mix and the reverb and delay sends; mono != 0: into mono instead, before
 * the pan and the sends (the SLICER, slicer.c slicer_drums, does those after it) */
static inline void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, int32_t *mono, uint32_t n)
{
    static int32_t lane_buf[8][CTL];
    uint32_t k, i;
    int lane_fx=drum_fx_lanes_active() && n<=CTL;
    int32_t lvl = song.g[G_DRLVL] * 200, send = send_thr(song.g[G_DRREV] * 258, thr_rev),
            dsend = send_thr(song.g[G_DRDLY] * 258, thr_dly), pk = drums.peak;   /* (2.6: the THROWs) */
    int32_t pan = trk[TRK_DRUM].p[P_PAN];
    if(lane_fx || (DRUM_METERS_ENABLE && n<=CTL)) memset(lane_buf,0,sizeof lane_buf);
    for (i = 0; i < n && drums.tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += drums.tail;
        } else {
            ml[i] += drums.tail;
            mr[i] += drums.tail;
        }
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &drums.v[k];
        const smp_zone_t *z;
        int32_t vl, vr;
        z = drum_zone_of(v);
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5];   /* Q16 source samples per output (drum_on) */
        int32_t g;
        if (!v->active)
            continue;
        {
            int32_t vp = clamp(pan + drums.lane_pan[k], -64, 63);
            vl = 4096 - (vp > 0 ? vp * 64 : 0);
            vr = 4096 + (vp < 0 ? vp * 64 : 0);
        }
        g = drums.lane_mute[k] ? 0 : mulq15(mulq15(lvl, v->vel * 258), (int32_t)drums.lane_level[k] * 258);
        g += g * 3 >> 2;                           /* existing drum output trim */
        for (i = 0; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= (drums.end[k] ? drums.end[k] : z->n)) {
                    v->active = 0;
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
            s = mulq15(s, mulq15(g, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
            if (drums.decay_shift[k]) {
                s = mulq15(s, drums.env[k]);
                drums.env[k] -= drums.env[k] >> drums.decay_shift[k];
                if (drums.env[k] < 8) v->active = 0;
            }
            if(drums.attack[k]) {
                s=mulq15(s,(int32_t)(32u-drums.attack[k])*1024);
                drums.attack[k]--;
            }
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if(lane_fx && !drums.preview[k] && drum_fx_config[lane_of_note(v->note)].type) {
                uint32_t lane=lane_of_note(v->note);
                if(lane<8u) lane_buf[lane][i]+=s;
                continue;
            }
            /* Meter bypass lanes too, summing overlapping voices before peak
             * detection. Preview is output-only, not a lane assignment. */
            if(DRUM_METERS_ENABLE && n<=CTL && !drums.preview[k]) {
                uint32_t lane=lane_of_note(v->note);
                if(lane<8u) lane_buf[lane][i]+=s;
            }
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * vl) >> 12;
            mr[i] += (s * vr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        v->ph[1] = frac;
    }
    if(lane_fx) for(k=0;k<8;k++) {
        if(!drum_fx_config[k].type) continue;
        int32_t vp=clamp(pan+drum_pad_mix_get(k,DRUM_MIX_PAN),-64,63);
        int32_t vl=4096-(vp>0?vp*64:0),vr=4096+(vp<0?vp*64:0);
        drum_fx_lane_process(k,lane_buf[k],n,(uint32_t)song.g[G_BPM]);
        for(i=0;i<n;i++) {
            int32_t s=lane_buf[k][i];
            if(drum_pad_mix_get(k,DRUM_MIX_MUTE)) s=0;
            if(DRUM_METERS_ENABLE) lane_buf[k][i]=s;
            if(mono) { mono[i]+=s; continue; }
            ml[i]+=(s*vl)>>12; mr[i]+=(s*vr)>>12;
            if(send) rev[i]+=mulq15(s,send);
            if(dsend) dly[i]+=mulq15(s,dsend);
        }
    }
    if(DRUM_METERS_ENABLE && n<=CTL)
        for(k=0;k<8u;k++) drum_meter_lane(k,lane_buf[k],n);
    drums.peak = pk;
}
static void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, uint32_t n) { drums_mix(ml, mr, rev, dly, 0, n); }
static void drums_render_mono(int32_t *mono, uint32_t n) { drums_mix(0, 0, 0, 0, mono, n); }
