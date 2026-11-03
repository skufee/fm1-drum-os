/* SPDX-License-Identifier: GPL-3.0-only */
/* RC0 map identity 1; source of truth FACTORY_KITS.json. Entries contain IDs only. */
#define DRUM_PRESET_COUNT 6u
#define DRUM_FACTORY_MAP_VERSION 1u
static const uint8_t DRUM_PRESETS[DRUM_PRESET_COUNT][8] = {
    {19,0,26,39,30,29,33,45}, /* BOOM BAP */
    {24,1,26,43,30,29,33,52}, /* DIRTY */
    {22,8,28,40,37,31,35,50}, /* DUSTY */
    {25,3,28,54,31,37,34,47}, /* TIGHT */
    {14,4,27,51,32,38,34,41}, /* TRAP */
    {17,3,28,44,31,32,37,45}, /* AIRY */
};
/* Historical A3.5/A4 map identity 0: read-only DRM1/2 and FUN conversion. */
static const uint8_t DRUM_LEGACY_PRESETS[16][8] = {
    {14,15,39,41,0,26,6,29},
    {18,21,43,44,2,27,8,31},
    {16,20,40,42,1,28,9,30},
    {14,17,39,45,4,26,10,33},
    {15,19,41,46,3,27,7,34},
    {14,16,40,39,5,26,11,29},
    {22,24,52,50,12,28,13,38},
    {14,15,47,48,0,26,6,35},
    {23,25,53,54,1,27,9,37},
    {17,18,39,42,2,26,8,32},
    {14,20,49,51,4,28,29,36},
    {16,21,43,46,7,27,10,33},
    {24,25,52,54,13,28,38,34},
    {19,23,48,53,3,27,12,37},
    {20,22,45,50,5,26,11,36},
    {21,24,47,52,8,28,35,38},
};
