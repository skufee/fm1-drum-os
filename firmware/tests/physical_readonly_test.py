#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pure USB-script guard/packing checks. Never imports MIDI or opens a device."""
from pathlib import Path
import importlib.util
spec=importlib.util.spec_from_file_location('readonly_tool',Path(__file__).resolve().parents[1]/'tools/physical_nor_readonly.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for n in range(300):
    raw=bytes((i*17+n)&255 for i in range(n))
    packed=m.pack7(raw);assert all(v<128 for v in packed) and m.unpack7(packed)==raw
for op in [4,6,8,10,12,13,14,15,16,20,21,22,23,24,25,26,28,29]:
    for p in [b'',b'\0',b'\2',b'\0\7',bytes([0x5a])*9]:assert not m.allowed(op,p),(op,p)
assert all(m.allowed(op,b'') for op in [1,2,18])
assert m.allowed(27,b'\0\7') and m.allowed(27,b'\2') and m.allowed(27,b'\1\0\0\0\1')
assert not m.allowed(27,b'\0\0') and not m.allowed(27,b'\3\7') and not m.allowed(27,b'\4\7\0\0\0\1')
assert m.allowed(19,b'\2\3\0\0\0\0\0\1') and not m.allowed(19,b'\2\4\0\0\0\0\0\1')
print('PASS readonly USB tool:300 pack7 vectors; no SAVE/STOP/SET/upload/restore/authorize or unsupported new command can be sent')
