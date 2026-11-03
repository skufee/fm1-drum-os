/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DRUM_FX_H
#define DRUM_FX_H
#include <stdint.h>
typedef struct { uint8_t type, p[4]; } drum_fx_config_t;
#define DRUM_FX_MASTER 8u
#define DRUM_FX_LANE_TYPES 8u
#define DRUM_FX_MASTER_TYPES 6u
drum_fx_config_t drum_fx_get(uint32_t lane);
void drum_fx_set(uint32_t lane, uint32_t type);
void drum_fx_param_set(uint32_t lane, uint32_t parameter, int32_t value);
void drum_fx_restore(uint32_t lane, const drum_fx_config_t *config);
void drum_fx_reset(void);
const char *drum_fx_name(uint32_t lane, uint32_t type);
const char *drum_fx_param_name(uint32_t lane, uint32_t type, uint32_t parameter);
void drum_fx_param_text(uint32_t lane, uint32_t type, uint32_t parameter, uint32_t value, char *out, unsigned capacity);
void drum_fx_lane_process(uint32_t lane, int32_t *samples, uint32_t n, uint32_t bpm);
void drum_fx_master_process(int32_t *l, int32_t *r, uint32_t n, uint32_t bpm);
int drum_fx_lanes_active(void);
void drum_preview(uint32_t sample_id, uint32_t level);
#endif
