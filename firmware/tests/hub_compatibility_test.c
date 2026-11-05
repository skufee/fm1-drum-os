/* SPDX-License-Identifier: GPL-3.0-only */
/* One focused FL80 host smoke; no physical USB device or NOR is accessed. */
#define PROTOCOL_TEST_ENTRY unused_protocol_matrix
#include "release_protocol_test.c"

int main(void)
{
    uint8_t pad[13];
    uint32_t i;
    const uint32_t ids[] = {0x856014u, 0xB36014u, 0u, 0xFFFFFFu, 0x856015u, 0xB36015u};
    memset(nor, 255, sizeof nor);
    host_tracks_init(); drum_preset_select(0); quiet();
    assert(call(1, 0, 0) == 0 && reply_n == 65u + sizeof(FM1_DRUM_ID) - 1u);
    assert(reply[1] == 2 && reply[64] == sizeof(FM1_DRUM_ID) - 1u);
    assert(!memcmp(reply + 56, FM1_DRUM_ID, 8));
    assert(!memcmp(reply + 65, FM1_DRUM_ID, sizeof(FM1_DRUM_ID) - 1u));
    assert(reply[8] == 55 && reply[9] == 16 && dw_u16(reply + 18) == 1959);
    for (i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        assert(!!fm1_flash_known_id(ids[i]) == (i < 2));
        save_flash_jedec = ids[i];
        assert(call(18, 0, 0) == 0 && reply_n == 20 && dw_u32(reply + 16) == ids[i]);
    }
    assert(call(3, (uint8_t[]){0}, 1) == 0 && reply_n == sizeof pad);
    memcpy(pad, reply, sizeof pad);
    assert(call(4, pad, sizeof pad) == 0 && !memcmp(reply, pad, sizeof pad));
    pad[1] = 71;
    assert(call(4, pad, sizeof pad) == 1);
    build(next_id++ & 127u, 1, 0, 0); req[5] ^= 1;
    ed_drum_handle(req, req_n); assert(receive() == 5);
    assert(!erases && !programs);
    puts("HUB HOST PASS: identity/CAPS, both JEDEC + unknown rejection, FL80 CRC/bounds, zero NOR writes");
    return 0;
}
