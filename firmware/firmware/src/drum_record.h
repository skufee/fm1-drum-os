/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef DRUM_RECORD_H
#define DRUM_RECORD_H
#include <stddef.h>
#include <stdint.h>
#include "drum_fx.h"

#define DRUMREC_VERSION 1u
#define DRUMREC_HEADER_SIZE 16u
#define DRUMREC_STEPS 64u
#define DRUMREC_PADS 8u
#define DRUMREC_STEP_SIZE 10u /* core.h: sizeof(dstep_t), verified static assertion */
#define DRUMREC_PAD_SIZE 7u
#define DRUMREC_PROJECT_FIELDS 16u
#define DRUMREC_PAYLOAD_SIZE (8u + DRUMREC_PROJECT_FIELDS * 2u + DRUMREC_STEPS * DRUMREC_STEP_SIZE + DRUMREC_PADS * DRUMREC_PAD_SIZE)
#define DRUMREC_SIZE (DRUMREC_HEADER_SIZE + DRUMREC_PAYLOAD_SIZE)

#define DRUMREC2_VERSION 2u
#define DRUMREC2_RAW_SIZE (DRUMREC_PADS * DRUMREC_STEPS)
#define DRUMREC2_PAYLOAD_SIZE (DRUMREC_PAYLOAD_SIZE + DRUMREC2_RAW_SIZE)
#define DRUMREC2_SIZE (DRUMREC_HEADER_SIZE + DRUMREC2_PAYLOAD_SIZE)

/* DRM3 keeps the DRM2 payload byte-for-byte, then adds the live sample
 * assignments and a compact project-local user-kit mirror.  Four kits are a
 * measured fit inside the existing 3840-byte storage object; this is not a
 * new flash object or partition. */
#define DRUMREC3_VERSION 3u
#define DRUMREC3_SOURCE_CUSTOM 0xFFu
#define DRUMREC3_USER_KITS 4u
#define DRUMREC3_PAD_SIZE 8u
#define DRUMREC3_CURRENT_META_SIZE (1u + DRUMREC_PADS)
#define DRUMREC3_USER_KIT_SIZE (1u + DRUMREC_PADS * DRUMREC3_PAD_SIZE)
#define DRUMREC3_USER_KITS_SIZE (DRUMREC3_USER_KITS * DRUMREC3_USER_KIT_SIZE)
#define DRUMREC3_PAYLOAD_SIZE (DRUMREC2_PAYLOAD_SIZE + DRUMREC3_CURRENT_META_SIZE + DRUMREC3_USER_KITS_SIZE)
#define DRUMREC3_SIZE (DRUMREC_HEADER_SIZE + DRUMREC3_PAYLOAD_SIZE)

/* DRM4 uses the existing object and append-only field encoding. Map identity 1
 * distinguishes the six neutral RC0 factories from historic 16-kit indices. */
#define DRUMREC4_VERSION 4u
#define DRUMREC4_FX_COUNT 9u
#define DRUMREC4_FX_SIZE 5u
#define DRUMREC4_PAYLOAD_SIZE (DRUMREC3_PAYLOAD_SIZE + 1u + (1u + DRUMREC3_USER_KITS) * DRUMREC4_FX_COUNT * DRUMREC4_FX_SIZE)
#define DRUMREC4_SIZE (DRUMREC_HEADER_SIZE + DRUMREC4_PAYLOAD_SIZE)

/* DRM5 appends source-percent START for current pads and four USER kits.
 * DRM1..4 stay byte-identical and decode START as zero; NOR objects unchanged. */
#define DRUMREC5_VERSION 5u
#define DRUMREC5_START_SIZE ((1u + DRUMREC3_USER_KITS) * DRUMREC_PADS)
#define DRUMREC5_PAYLOAD_SIZE (DRUMREC4_PAYLOAD_SIZE + DRUMREC5_START_SIZE)
#define DRUMREC5_SIZE (DRUMREC_HEADER_SIZE + DRUMREC5_PAYLOAD_SIZE)
/* DRM6 references four one-zone CRC-validated user slots without changing NOR. */
#define DRUMREC6_VERSION 6u
#define DRUMREC6_REF_SIZE ((1u + DRUMREC3_USER_KITS) * DRUMREC_PADS * 4u)
#define DRUMREC6_PAYLOAD_SIZE (DRUMREC5_PAYLOAD_SIZE + DRUMREC6_REF_SIZE)
#define DRUMREC6_SIZE (DRUMREC_HEADER_SIZE + DRUMREC6_PAYLOAD_SIZE)

/* Application-neutral, fixed-width wire model. project_fields are, in order,
 * drum P_LEVEL/P_PAN/P_MUTE/P_SDIV/P_SSWING/P_SGATE/P_DIST/P_CHOR/P_DLY/P_REV/P_E0,
 * then G_CLOCK/G_TUNE/G_DRLVL/G_DRREV/G_DRDLY. */
typedef struct {
    uint16_t bpm, swing;
    uint8_t selected_pad, pattern_length, qtz_grid, qtz_strength; /* zero = unsupported/off in A3.1 */
    int16_t project_fields[DRUMREC_PROJECT_FIELDS];
    uint8_t steps[DRUMREC_STEPS][DRUMREC_STEP_SIZE];
    struct { uint8_t level; int8_t pan; uint8_t mute; int8_t tune; uint8_t end, decay; } pads[DRUMREC_PADS];
} drumrec_t;

/* In-memory model only; the DRM2 wire format is encoded field by field. */
typedef struct {
    drumrec_t base;
    int8_t raw_offsets[DRUMREC_PADS][DRUMREC_STEPS];
} drumrec2_t;

typedef struct {
    uint8_t sample_id;
    uint8_t level;
    int8_t pan;
    uint8_t mute;
    int8_t tune;
    uint8_t end, decay;
    uint8_t reserved;
} drumrec3_pad_t;

typedef struct {
    drumrec2_t base;
    uint8_t source_id;                         /* DRM3: historic 0..15; DRM4/map1: 0..5; FF custom */
    uint8_t sample_ids[DRUMREC_PADS];          /* live assignments */
    uint8_t user_valid[DRUMREC3_USER_KITS];
    drumrec3_pad_t user_kits[DRUMREC3_USER_KITS][DRUMREC_PADS];
    /* In-memory extension only: DRM3 decoder leaves these zero; DRM3 wire unchanged. */
    uint8_t map_version;
    drum_fx_config_t fx[DRUMREC4_FX_COUNT];
    drum_fx_config_t user_fx[DRUMREC3_USER_KITS][DRUMREC4_FX_COUNT];
    uint8_t starts[DRUMREC_PADS];
    uint8_t user_starts[DRUMREC3_USER_KITS][DRUMREC_PADS];
    uint32_t sample_refs[DRUMREC_PADS];
    uint32_t user_sample_refs[DRUMREC3_USER_KITS][DRUMREC_PADS];
} drumrec3_t;

/* Returns DRUMREC_SIZE on success, 0 on invalid input/capacity. Decode returns 1
 * only for exact-size, supported-version, CRC-valid, in-range records. */
size_t drumrec_encode(void *dst, size_t capacity, const drumrec_t *record);
int drumrec_decode(drumrec_t *record, const void *src, size_t length);

/* DRM2 v2 adds exactly 512 lane-major signed raw timing bytes to the DRM1
 * payload. It has its own magic/version and never falls back to DRM1 decode. */
size_t drumrec2_encode(void *dst, size_t capacity, const drumrec2_t *record);
int drumrec2_decode(drumrec2_t *record, const void *src, size_t length);

size_t drumrec3_encode(void *dst, size_t capacity, const drumrec3_t *record);
int drumrec3_decode(drumrec3_t *record, const void *src, size_t length);

size_t drumrec4_encode(void *dst, size_t capacity, const drumrec3_t *record);
int drumrec4_decode(drumrec3_t *record, const void *src, size_t length);

size_t drumrec5_encode(void *dst, size_t capacity, const drumrec3_t *record);
int drumrec5_decode(drumrec3_t *record, const void *src, size_t length);

size_t drumrec6_encode(void *dst, size_t capacity, const drumrec3_t *record);
int drumrec6_decode(drumrec3_t *record, const void *src, size_t length);

#endif
