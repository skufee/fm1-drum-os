#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused FWSC/loader identity and integrity check; no hardware access."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import fm1pkg_make as pkg
import lz4blk


def main():
    raw = (ROOT / 'build/felucca.fwsc').read_bytes()
    identity = ''.join(chr(raw[i * 48 + 47] - i - 1)
                       for i in range(20) if raw[i * 48 + 47] != 0x7D)
    assert identity == pkg.PRODUCT
    logical = b''.join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:]
    hdr = bytearray(logical[:64]); pkg.enc(hdr, 0, 64)
    u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
    u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
    assert u16(hdr, 0) == pkg.crc16(hdr[2:])
    assert u32(hdr, 4) == len(logical) and u16(hdr, 8) == 2
    table = logical[64:224]
    assert u16(hdr, 2) == pkg.crc16(table)
    files = {}
    ranges = []
    for i in range(2):
        e = bytearray(table[i * 80:(i + 1) * 80]); pkg.enc(e, 0, 80)
        off, size = u32(e, 8), u32(e, 12)
        assert off >= 1024 and off % 256 == 0 and size and off + size <= len(logical)
        ranges.append((off, off + size))
        name = bytes(e[64:]).split(b'\0')[0].decode('ascii')
        data = logical[off:off + size]
        assert u16(e, 4) == pkg.crc16(data) and name not in files
        files[name] = data
    assert ranges[0][1] <= ranges[1][0]
    assert set(files) == {'flash.bin', 'ota.bin'}
    flash, ota = files['flash.bin'], files['ota.bin']
    assert len(flash) == pkg.FLASH_SIZE == 0x93000
    assert ota == (ROOT / 'build/loader/ota.bin').read_bytes()
    for off in (0, 32):
        assert u16(ota, off) == pkg.crc16(ota[off + 2:off + 32])
        assert ota[off + 16:off + 31] == b'usb_hid_ota.bin'
    assert u16(ota, 2) == pkg.crc16(ota[32:]) and u32(ota, 8) == len(ota) - 32
    assert u32(ota, 40) == 0x01C0A800
    image = bytearray(); off = 64
    while off < len(ota):
        clen, dlen = struct.unpack_from('<II', ota, off); off += 8
        assert 0 < clen <= 4096 and 0 < dlen <= 4096 and off + clen <= len(ota)
        block = lz4blk.decompress(ota[off:off + clen]); assert len(block) == dlen
        image += block; off += clen
    assert off == len(ota) and len(image) == u32(ota, 36)
    assert u16(ota, 34) == pkg.crc16(image)
    assert bytes(image) == (ROOT / 'build/loader/loader.bin').read_bytes()
    assert b'FELUCCA-LOADER-1' in image[:64]
    assert ('ota-' + identity).encode() + b'\0' in image
    app = (ROOT / 'build/felucca.bin').read_bytes()
    area = bytearray(flash[0x4000:]); pkg.sfc(area, 0, len(area), 0, pkg.KEY)
    assert area[0x120:0x120 + len(app)] == app and identity.encode() + b'\0' in app
    assert b'FM-1_900\0' not in app and b'ota-FM-1_900\0' not in image
    assert b'FM-1_947\0' not in app and b'ota-FM-1_947\0' not in image
    print(f'HUB PACKAGE PASS: {identity}; CRC16 headers/files/JLFS/LZ4; exact app and RAM loader {len(image)}B')


if __name__ == '__main__':
    main()
