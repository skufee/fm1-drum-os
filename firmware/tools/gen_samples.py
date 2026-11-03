#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the production embedded custom drum sample library into a C header.

The production payload is exactly the 55 PCM16 mono 22050-Hz WAV files in the
workspace sibling directory converted_samples/. Stable sample IDs follow
natural-sorted relative filenames. The input samples are encoded directly with
the existing sampleio.ima_encode implementation; no normalization, trimming,
resampling, or looping is applied. The former factory sample generation helpers
remain below as upstream/reference code, but do not feed this production build.

With FELUCCA_SLICE=1 the SLICE engine's built-in BREAK (eng_slice.c) is rendered here
too: one bar of 16ths arranged from Felucca's own generated drums, stored after every set
and not one of the SAMPLE sets. Its slice table (decoder states on a 128-point grid, the hits as AUTO
slices) is written with it: SLC_BREAK_INIT.

The header is cached in build/gen_samples.cache under a hash of every input.
"""
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENDIR = SRC / "build" / "genwav"
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
CUSTOM55 = Path(os.environ.get("FM1_SAMPLE_SOURCE", SRC.parent / "converted_samples"))
CUSTOM55_ARTIFACTS = Path(os.environ.get("FM1_SAMPLE_REPORT_DIR", SRC / "build"))
PREBUILT55 = SRC / "assets" / "embedded" / "felucca_samples.h"
TR = 22050                                   # stored sample rate

# set -> kind ("oneshot" decaying, "sus" looped sustain, "piano" its attack then a steady loop that the
# envelope fades, "kit" one sample per key).
# The other slots are user sets loaded from the web editor.
CC0_SETS = [("PIANO", "piano"), ("BASS", "oneshot"), ("VIBES", "oneshot"), ("HORNS", "oneshot"),
            ("STRGS", "oneshot"), ("FLUTE", "sus"), ("SCRCH", "oneshot"), ("KIT", "kit")]
MEASURED_TUNING = ()                         # sets recorded off A440 (measured near the named note)

# the preset of each set: name, {SET (filled in), TUNE, BITS, LOOP (filled in), CUT, -, DRV, -}, env, mono,
# sends (DIST, CHORUS, DELAY, REVERB), more (track parameter, value) pairs (preset_t.x)
SET_PRESETS = {
    "PIANO": ("GRAND PNO", [0, 0, 0, 0, 127, 0, 0, 0], (0, 110, 0, 58), 0, (0, 6, 8, 24), ()),
    "BASS": ("UP BASS", [0, 0, 0, 0, 118, 0, 12, 0], (0, 127, 127, 26), 1, (0, 0, 0, 4), ()),
    "VIBES": ("VIBES", [0, 0, 0, 0, 127, 0, 0, 0], (0, 127, 127, 72), 0, (0, 24, 22, 30), ("P_LD_AMP", 18, "P_LRATE", 86)),
    "HORNS": ("HORN STAB", [0, 0, 0, 0, 120, 0, 20, 0], (0, 127, 127, 22), 0, (8, 0, 14, 18), ()),
    "STRGS": ("STRING STB", [0, 0, 0, 0, 122, 0, 10, 0], (0, 127, 127, 26), 0, (0, 10, 12, 24), ()),
    "FLUTE": ("LOFI FLUTE", [0, 0, 12, 0, 100, 0, 0, 0], (12, 80, 120, 60), 1, (0, 16, 26, 30), ("P_LD_PIT", 1, "P_LRATE", 89, "P_LFADE", 40)),
    "SCRCH": ("SCRATCH", [0, 0, 0, 0, 127, 0, 8, 0], (0, 127, 127, 18), 0, (0, 0, 0, 0), ()),
    "PERC": ("GM KIT", [0, 0, 0, 0, 127, 0, 0, 0], (0, 127, 127, 60), 0, (0, 0, 0, 6), ()),
}
# more presets on a set: (set, name, E[], env, mono, sends, more)
EXTRA_PRESETS = [
    ("PIANO", "DUSTY PNO", [0, 0, 34, 0, 96, 0, 24, 0], (0, 98, 0, 52), 0, (0, 8, 10, 22), ()),
    ("PIANO", "LOFI KEYS", [0, 0, 45, 0, 78, 0, 30, 0], (0, 108, 0, 60), 0, (0, 30, 18, 34), ("P_LD_PIT", 1, "P_LRATE", 38)),
    ("BASS", "DEEP BASS", [0, -12, 0, 0, 90, 0, 40, 0], (0, 127, 127, 30), 1, (0, 0, 0, 0), ()),
]

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# GM drum map (General MIDI percussion, channel 10): role -> [(lo, hi, root)].
# One sample can serve several GM notes; a range is pitched around its root.
GM_KIT = {
    "kick": [(35, 36, 36)], "rim": [(37, 37, 37)], "snare": [(38, 38, 38), (40, 40, 40)],
    "clap": [(39, 39, 39)], "chh": [(42, 42, 42), (44, 44, 44)], "ohh": [(46, 46, 46)],
    # (the toms skip the hi-hats' 42, 44, 46: a zone over them would win, the hats would play a tom)
    "tomlo": [(41, 41, 43), (43, 43, 43), (45, 45, 43)], "tomhi": [(47, 48, 48), (50, 50, 48)],
    "tom": [(41, 41, 47), (43, 43, 47), (45, 45, 47), (47, 48, 47), (50, 50, 47)],
    "crash": [(49, 49, 49), (52, 52, 52), (55, 55, 55), (57, 57, 57)],
    "ride": [(51, 51, 51), (53, 53, 53), (59, 59, 59)],
    "tamb": [(54, 54, 54)], "cowbell": [(56, 56, 56)], "conga": [(62, 64, 63)],
    "shaker": [(69, 70, 70), (82, 82, 82)], "claves": [(75, 75, 75)], "wood": [(76, 77, 76)],
}
GM_ROLE_WORDS = [("bassdrum", "kick"), ("kick", "kick"), ("snare", "snare"), ("hihat", "chh"), ("chat", "chh"),
                 ("ohat", "ohh"), ("clap", "clap"), ("tom lo", "tomlo"), ("tom hi", "tomhi"), ("tomlo", "tomlo"),
                 ("tomhi", "tomhi"), ("tom", "tom"),
                 ("rim", "rim"), ("cowbell", "cowbell"), ("tamb", "tamb"), ("shaker", "shaker"),
                 ("conga", "conga"), ("claves", "claves"), ("wood", "wood"), ("crash", "crash"), ("ride", "ride")]
GM_KEEP = {"crash": 0.6, "ride": 0.6, "ohh": 0.6}   # (SLOOP 2.5: the cymbals 1.0 -> 0.6 s, 12.8 KB of flash back;
                                                      # the first 0.44 s as recorded, then a fade: GM_FADE)
GM_RATE = {"chh": 32000, "ohh": 32000, "crash": 32000, "ride": 32000}   # the metal keeps its top (else TR)
# an acoustic kit from the CC0 recordings (assets/samples-cc0/KIT: VCSL): the concert bass drum and the toms
# get a shorter body (a kit's tuned-up, damped heads): their level falls as exp(-t / tau) after a hold
GM_SHAPE = {"kick": (0.03, 0.13), "tomlo": (0.04, 0.22), "tomhi": (0.04, 0.2)}
GM_FADE = {"crash": 0.16, "ride": 0.16}               # the cymbals fade out over their last 0.16 s (else 30 ms)
# the balance of the kit (dB under full scale): the kick and the snare up front, the metal and the hand
# percussion further back, as the synthesised kits (tools/level_drumkits.py); the click's wood block stays
GM_GAIN = {"chh": -9, "ohh": -9, "crash": -8, "ride": -9, "shaker": -8, "tamb": -6, "conga": -4, "cowbell": -4,
           "claves": -4}

# SLICE's BREAK: (step, GM role, gain) on a bar of 16ths; the open hat is choked by the next hat
BREAK_BPM, BREAK_STEPS = 120, 16
BREAK_HITS = [(0, "kick", 1.0), (0, "chh", 0.55), (2, "kick", 0.7), (2, "chh", 0.4), (4, "snare", 1.0),
              (4, "chh", 0.45), (6, "chh", 0.4), (7, "kick", 0.8), (8, "chh", 0.55), (9, "snare", 0.3),
              (10, "kick", 0.9), (10, "ohh", 0.4), (12, "snare", 1.0), (12, "chh", 0.45), (14, "chh", 0.4),
              (14, "snare", 0.35)]
SLC_GRID, SLC_AUTO = 128, 32                 # eng_slice.c slc_src_t

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60)}

_wavs = {}


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out, conv = [], 1
    if kind != "kit" and not all(re.search(r"_m\d+\.wav$", p.name) for p in files):
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        given = re.search(r"_m(\d+)\.wav$", p.name)
        if given and kind != "kit":
            root = int(given.group(1))                  # (the file says it)
        elif kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = {"oneshot": 1.0, "sus": 0.95, "kit": 0.6, "piano": 1.0}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "piano":                          # the attack, then a steady tone the envelope (DEC) fades:
            ls, le, xf, a0 = int(0.55 * TR), n - 1, int(0.08 * TR), int(0.30 * TR)   # long notes in 1 s
            w = int(0.02 * TR)
            env = [max(1e-6, (sum(v * v for v in x[max(0, i - w):i + w]) / (2 * w)) ** 0.5) for i in range(0, n, w)]
            ref = env[a0 // w]
            x = [v * (ref / env[min(len(env) - 1, i // w)] if i >= a0 else 1.0) for i, v in enumerate(x)]
            ls = best_loop_start(x, le, xf, int(0.45 * TR), int(0.65 * TR))   # (the tone in phase: no dip)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        elif kind == "sus":                          # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            fade = int(0.08 * TR)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def best_loop_start(x, le, xf, lo, hi):
    """the loop start in [lo, hi) whose last xf samples look most like the loop end's (correlation, on
    every 4th sample, every 2nd start: plain Python, no numpy): the crossfade then joins two copies of the
    same wave, not two phases that cancel"""
    tail = x[le - xf:le:4]
    tn = sum(v * v for v in tail) ** 0.5 + 1e-9
    best, bl = -2.0, (lo + hi) // 2
    for ls in range(lo, hi, 2):
        seg = x[ls - xf:ls:4]
        c = sum(a * b for a, b in zip(seg, tail)) / ((sum(v * v for v in seg) ** 0.5 + 1e-9) * tn)
        if c > best:
            best, bl = c, ls
    return bl


def gm_role(name):
    n = name.lower()
    return next((r for w, r in GM_ROLE_WORDS if w in n), None)


def gm_kit_sources(have_cc0):
    """role -> wav path: the CC0 acoustic kit (assets/samples-cc0/KIT); without it (and for SLICE's BREAK)
    Felucca's own synthesized drums (gen_waves.py) for the roles it lacks"""
    src = {}
    if have_cc0 and (CC0 / "KIT").exists():
        for p in sorted((CC0 / "KIT").glob("*.wav")):
            r = gm_role(p.name)
            if r and r not in src:
                src[r] = p
    for p in sorted(GENDIR.glob("D *.wav")):
        r = gm_role(p.stem)
        if r and r not in src:
            src[r] = p
    if "tomlo" in src or "tomhi" in src:
        src.pop("tom", None)
    return src


def gm_kit_entry(role, path, rate=TR):
    sr, x = wav(path)
    x = resample(x[max(0, onset(x) - 16):], sr, rate)
    x = x[:int(GM_KEEP.get(role, 0.45) * rate)]
    if role in GM_SHAPE:
        hold, tau = GM_SHAPE[role]
        h = int(hold * rate)
        x = [v * (1.0 if i < h else math.exp(-(i - h) / (tau * rate))) for i, v in enumerate(x)]
    pk = peak(x)
    end = len(x)                                    # drop the silent tail
    while end > 64 and abs(x[end - 1]) < 0.004 * pk:
        end -= 1
    x = x[:end]
    fade = min(len(x) // 4, int(GM_FADE.get(role, 0.03) * rate))
    for i in range(fade):
        x[len(x) - fade + i] *= 1 - i / fade
    g = 30000 / pk * 10 ** (GM_GAIN.get(role, 0) / 20) if path.parent.name == "KIT" else 30000 / pk
    return [int(v * g) for v in x]


def ima_states(data, positions):
    """IMA ADPCM decoder state before sample p for each p (ascending), packed as eng_slice.c reads it:
    (predictor & 0xFFFF) | index << 16 (segment 0)"""
    pred, idx, out, want = 0, 0, [], list(positions)
    for n in range(max(want) + 1 if want else 0):
        while want and want[0] == n:
            out.append((pred & 0xFFFF) | idx << 16)
            want.pop(0)
        code = (data[n >> 1] >> (4 * (n & 1))) & 15
        step = sio.IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
    return out


def break_loop():
    """SLICE's BREAK: BREAK_HITS from Felucca's generated drums at TR -> (int16 samples, hit positions)"""
    src = gm_kit_sources(False)                     # generated sounds only: the same on every build
    n = int(round(TR * 60 / BREAK_BPM * 4))
    x = [0.0] * n
    pos = [s * n // BREAK_STEPS for s in range(BREAK_STEPS + 1)]
    for step, role, gain in BREAK_HITS:
        smp = gm_kit_entry(role, src[role])
        if role == "ohh":                           # choked by the next hat
            nxt = min([s for s, r, _ in BREAK_HITS if r in ("chh", "ohh") and s > step] or [BREAK_STEPS])
            keep, fade = pos[nxt] - pos[step], int(0.004 * TR)
            smp = [v * min(1.0, (keep - i) / fade) for i, v in enumerate(smp[:keep])]
        for i, v in enumerate(smp):                 # the tails wrap round: a seamless loop
            x[(pos[step] + i) % n] += v * gain
    pk = peak(x)
    return [int(v * 30000 / pk) for v in x], sorted({pos[s] for s, _, _ in BREAK_HITS})


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}
        self.brk = None
        self.custom = []

    def slice_break(self):
        """SLICE's BREAK, after every set; its slice table: decoder states at k * len / SLC_GRID, the hits"""
        x, hits = break_loop()
        off, _ = self.add(x, 0)
        n = len(x)
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        hits = hits[:SLC_AUTO]
        self.brk = dict(off=off, n=n, grid=grid, apos=hits, ast=ima_states(data, hits))

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def gm_kit(self, have_cc0):
        """one GM-mapped kit: the CC0 acoustic kit"""
        z0 = len(self.zones)
        for role, path in gm_kit_sources(have_cc0).items():
            rate = GM_RATE.get(role, TR) if path.parent.name == "KIT" else TR
            smp = gm_kit_entry(role, path, rate)
            off, st = self.add(smp, len(smp))
            for lo, hi, root in GM_KIT[role]:
                self.zones.append(dict(off=off, n=len(smp), ls=len(smp), le=len(smp), looped=False, sr=rate,
                                       root16=root * 16, pred=st[0], idx=st[1], lo=lo, hi=hi))
        self.sets.append(("PERC", z0, len(self.zones) - z0))
        self.kinds["PERC"] = "kit"

    def cc0_set(self, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        named = sets or [("NONE", 0, 0)]

        def preset(i, k, pname, e, env, mono, fx, more):
            e = list(e)
            e[0], e[3] = i, 0 if k == "kit" else 1
            x = ", ".join(f"{more[j]} + 1, {more[j + 1]}" for j in range(0, len(more), 2))
            return (f'    {{"{pname}", {{{", ".join(map(str, e))}}}, {{{", ".join(map(str, env))}}}, 0, {mono}, '
                    f'FX({", ".join(map(str, fx))}){", .x = {" + x + "}" if x else ""}}},')
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            if name in SET_PRESETS:
                L.append(preset(i, k, *SET_PRESETS[name]))
            else:
                a, d, s_, r = ENV[k]
                L.append(f'    {{"{name}", {{{i}, 0, 0, {0 if k == "kit" else 1}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0}},')
        for setname, *rest in EXTRA_PRESETS:
            i = next((j for j, (n, _, _) in enumerate(named) if n == setname), None)
            if i is not None:
                L.append(preset(i, self.kinds.get(setname, "wave"), *rest))
        L.append("};")
        names = ", ".join(f'"{n}"' for n, _, _ in named)
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        if self.custom:
            L.append("")
            L.append(f"#define SMP_CUSTOM_SAMPLE_COUNT {len(self.custom)}u")
            L.append("static const char *const SMP_CUSTOM_SAMPLE_NAMES[SMP_CUSTOM_SAMPLE_COUNT] = {")
            L.extend(f'    "{e["name"]}",' for e in self.custom)
            L.append("};")
            for e in self.custom:
                L.append(f'#define SMP_CUSTOM_ID_{e["macro"]} {e["sample_id"]}u')
            # PAD START has integer-percent resolution. Exact decoder states at
            # each selectable point remove ALL seek work from note-on/audio.
            # Separate arrays avoid C structure padding: 55 * 100 * 3 = 16500 B.
            starts = adpcm_percent_states(blob, zones[:len(self.custom)])
            L.append("#define SMP_START_CHECKPOINT_BYTES (SMP_CUSTOM_SAMPLE_COUNT * 100u * 3u)")
            for name, ctype, part in (("PRED", "int16_t", 0), ("IDX", "uint8_t", 1)):
                L.append(f"static const {ctype} SMP_START_{name}[SMP_CUSTOM_SAMPLE_COUNT][100] = {{")
                for row in starts:
                    L.append("    {" + ",".join(str(state[part]) for state in row) + "},")
                L.append("};")
        L += self.break_header()
        return "\n".join(L) + "\n"

    def break_header(self):
        """SLC_BREAK_INIT: an eng_slice.c slc_src_t (len, rate, nseg, nauto, seg[], grid[], apos[], ast[])"""
        b = self.brk
        if not b:
            return ["#define SLC_BREAK_INIT {0}"]
        rate = int(round(TR / 44100 * 65536))

        def lst(v, k):
            v = list(v) + [0] * (k - len(v))
            return ", \\\n    ".join(", ".join(map(str, v[i:i + 12])) for i in range(0, len(v), 12))
        return ["", f"/* SLICE's BREAK: one bar of {BREAK_STEPS} steps at {BREAK_BPM} BPM, {len(b['apos'])} hits */",
                f"#define SLC_BREAK_BPM {BREAK_BPM}", f"#define SLC_BREAK_STEPS {BREAK_STEPS}",
                f"#define SLC_BREAK_INIT {{{b['n']}, {rate}, 1, {len(b['apos'])}, {{{{{b['off']}, 0, {b['n']}}}}}, {{ \\",
                "    " + lst(b["grid"], SLC_GRID) + "}, { \\", "    " + lst(b["apos"], SLC_AUTO) + "}, { \\",
                "    " + lst(b["ast"], SLC_AUTO) + "}}"]

    def summary(self):
        brk = f", SLICE BREAK {self.brk['n']} samples" if self.brk else ""
        custom = f", {len(self.custom)} user WAVs" if self.custom else ""
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM{custom}{brk}"


def adpcm_percent_states(blob, zones):
    """State BEFORE decoding floor(n*p/100), same low-nibble-first IMA as C.

    No approximation to PCM input: states follow the exact encoded SMP_DATA.
    Offsets remain sample indices; an odd nibble is never treated as a byte.
    """
    rows = []
    for z in zones:
        pred, idx, pos, row = 0, 0, 0, []
        for percent in range(100):
            target = z["n"] * percent // 100
            while pos < target:
                b = blob[z["off"] + (pos >> 1)]
                code = (b >> 4) if pos & 1 else b & 15
                step = sio.IMA_STEP[idx]
                delta = (step >> 3) + (step if code & 4 else 0) + (step >> 1 if code & 2 else 0) + (step >> 2 if code & 1 else 0)
                pred = max(-32768, min(32767, pred + (-delta if code & 8 else delta)))
                idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
                pos += 1
            row.append((pred, idx))
        rows.append(row)
    return rows


def natural_key(value):
    return tuple(int(part) if part.isdigit() else part.casefold()
                 for part in re.split(r"(\d+)", value))


def custom55_paths(root=CUSTOM55):
    root = Path(root)
    if not root.is_dir():
        raise SystemExit(f"custom sample source directory is missing: {root}")
    paths = sorted((p for p in root.rglob("*") if p.is_file() and p.suffix.casefold() == ".wav"),
                   key=lambda p: natural_key(p.relative_to(root).as_posix()))
    if len(paths) != 55:
        raise SystemExit(f"A3.4c requires exactly 55 WAV files; found {len(paths)} under converted_samples")
    expected = [f"{category}{i}" for category, count in
                (("HAT",14),("KICK",12),("OH",3),("PERC",10),("SNARE",16)) for i in range(1,count+1)]
    if [p.stem.upper() for p in paths] != expected:
        raise SystemExit("55 WAV names/order must be HAT1..14,KICK1..12,OH1..3,PERC1..10,SNARE1..16")
    return paths


def add_custom55(builder, paths, custom_root=CUSTOM55):
    """Replace the factory embedded sample payload with the exact PCM16 source set."""
    manifest = []
    macros = set()
    for sample_id, path in enumerate(paths):
        raw = path.read_bytes()
        chunks = sio.wav_chunks(raw)
        tag, channels, rate, bits = sio.wav_format(raw, chunks)
        if (tag, channels, rate, bits) != (1, 1, TR, 16):
            raise SystemExit(f"{path.name}: expected mono PCM16 at {TR} Hz, got tag={tag} ch={channels} rate={rate} bits={bits}")
        sr, samples, _loop = sio.read_wav(path)
        if sr != TR or not samples:
            raise SystemExit(f"{path.name}: empty or inconsistent WAV data")
        adpcm, _state = ima_encode(samples, len(samples))
        off = len(builder.blob)
        builder.blob.extend(adpcm)
        pad = len(builder.blob) & 1
        if pad:
            builder.blob.append(0)       # the following zone starts on an even ADPCM sample boundary
        stem = path.stem
        category_match = re.match(r"^([A-Za-z]+)", stem)
        index_match = re.search(r"(\d+)$", stem)
        category = category_match.group(1).upper() if category_match else "UNCATEGORIZED"
        macro = re.sub(r"[^A-Za-z0-9_]", "_", stem.upper())
        if macro in macros:
            raise SystemExit(f"sample ID macro collision after normalization: {stem}")
        macros.add(macro)
        # All custom sounds are one-shots. The drum path applies source-rate playback and tune
        # relative to the triggering lane, so no category or GM note is baked into an ID.
        zone = dict(off=off, n=len(samples), ls=len(samples), le=len(samples), looped=False,
                    sr=TR, root16=0, pred=0, idx=0, lo=0, hi=127)
        builder.zones.append(zone)
        builder.custom.append({"sample_id": sample_id, "name": stem, "macro": macro})
        manifest.append({
            "original_filename": path.name,
            "relative_path": path.relative_to(custom_root).as_posix(),
            "source_sha256": hashlib.sha256(raw).hexdigest(),
            "category": category,
            "category_index": int(index_match.group(1)) if index_match else None,
            "sample_id": sample_id,
            "conversion_parameters": {
                "input": "mono signed PCM16 little-endian, 22050 Hz",
                "codec": "SLOOP sampleio.ima_encode / internal IMA ADPCM 4-bit",
                "initial_predictor": 0,
                "initial_step_index": 0,
                "nibble_order": "low nibble first",
                "resampling": "none",
                "normalization_or_trimming": "none",
                "looping": False,
            },
            "generated_symbol": f"SMP_DATA + {off}",
            "adpcm_payload_bytes": len(adpcm),
            "alignment_padding_bytes": pad,
            "adpcm_sha256": hashlib.sha256(adpcm).hexdigest(),
            "sample_frames": len(samples),
            "sample_rate": TR,
            "sample_offset_bytes": off,
        })
    # Keep the SLOOP set name expected by its generic metadata readers. Drum playback indexes
    # all 55 zones through the explicit sample-ID API rather than pretending they form a GM kit.
    builder.sets.append(("PERC", 0, 1))
    builder.kinds["PERC"] = "kit"
    return manifest


def write_custom_manifest(manifest):
    CUSTOM55_ARTIFACTS.mkdir(parents=True, exist_ok=True)
    result = {
        "library": "FM1 DRUM OS production 55 samples, embedded in application XIP",
        "sample_id_order": "natural-sorted relative path, case-insensitive extension",
        "codec": "SLOOP internal IMA ADPCM 4-bit",
        "sample_count": len(manifest),
        "adpcm_payload_bytes": sum(e["adpcm_payload_bytes"] for e in manifest),
        "alignment_padding_bytes": sum(e["alignment_padding_bytes"] for e in manifest),
        "zone_metadata_bytes": len(manifest) * 28,
        "samples": manifest,
    }
    (CUSTOM55_ARTIFACTS / "source-to-adpcm-map.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def input_key(paths):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], os.environ.get("FELUCCA_SLICE") == "1")).encode())   # sum() differs across versions
    files = (sorted(GENDIR.glob("*.wav")) if os.environ.get("FELUCCA_SLICE") == "1" else []) + paths
    for p in files:
        try:
            rel = p.relative_to(SRC).as_posix()
        except ValueError:
            rel = "converted_samples/" + p.relative_to(CUSTOM55).as_posix()
        h.update(rel.encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    # Public builds use the exact licensed generated bank, without WAV sources.
    # An explicit missing source must fail instead of silently selecting a bank.
    if not CUSTOM55.is_dir() and not os.environ.get("FM1_SAMPLE_SOURCE") and PREBUILT55.is_file():
        meta = json.loads(PREBUILT55.with_name("library.json").read_text(encoding="utf-8"))
        raw = PREBUILT55.read_bytes()
        if meta.get("sample_count") != 55 or hashlib.sha256(raw).hexdigest() != meta.get("header_sha256"):
            raise SystemExit("embedded sample bank checksum/count mismatch")
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_bytes(raw)
        print("samples: verified distributed 55-zone ADPCM bank (no source WAV required)")
        return
    if os.environ.get("FELUCCA_SLICE") == "1":
        subprocess.run([sys.executable, str(Path(__file__).with_name("gen_waves.py")), str(GENDIR)], check=True)
    paths = custom55_paths()
    key = input_key(paths)
    try:
        ck, summary, text = CACHE.read_text(encoding="utf-8").split("\n", 2)
        if ck == key:
            Path(out).write_text(text, encoding="utf-8")
            manifest = add_custom55(Builder(), paths)
            write_custom_manifest(manifest)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    manifest = add_custom55(b, paths)
    if os.environ.get("FELUCCA_SLICE") == "1":       # SLICE's BREAK: only when that engine is built
        b.slice_break()                             # last: the sets' offsets stay as they were
    text = b.header()
    Path(out).write_text(text, encoding="utf-8")
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}", encoding="utf-8")
    write_custom_manifest(manifest)
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
