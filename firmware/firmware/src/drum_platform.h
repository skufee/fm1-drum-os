/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_DRUM_PLATFORM_H
#define FM1_DRUM_PLATFORM_H
#include <stdint.h>
/* Owner-confirmed free Hub identity, 2026-11-05. */
#define FM1_DRUM_ID "FM-1_1337"
_Static_assert(sizeof(FM1_DRUM_ID) >= 9u && sizeof(FM1_DRUM_ID) <= 21u,
               "Identity must fit the FWSC marker and CAPS revision 2 extension");
static inline int fm1_flash_known_id(uint32_t id)
{
    return id == 0x856014u || id == 0xB36014u;
}
#endif
