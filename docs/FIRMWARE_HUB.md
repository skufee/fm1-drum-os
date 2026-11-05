# Firmware Hub identity: FM-1_1337

The owner confirmed the free identity `FM-1_1337` on 2026-11-05. Application,
M-UPGRADE, package markers and extended Drum CAPS all report this exact nine-byte
identity. The update loader reports `ota-FM-1_1337`. Earlier DRUM OS builds used
`FM-1_900` and the superseded `FM-1_947` candidate; neither is the current asset.
This owner confirmation is not physical update-transition acceptance.

Recovery `FM-1_000`, USB MIDI/SysEx framing, DRM6, NOR geometry, both JEDEC gates,
audio/sequencer/UI and sample IDs0-70 are unchanged by this identity fix.

## Append-only CAPS revision2 (FL protocol remains1)

CAPS request opcode1 still has no payload. Existing field offsets are preserved:

| Bytes | Meaning |
|---|---|
| 0 | FL application protocol version1, unchanged |
| 1 | CAPS layout revision2 (legacy64-byte replies had0) |
| 2-55 | Existing format/capabilities/counts/sizes/firmware fields, unchanged |
| 56-63 | First8 identity bytes, compatibility prefix only; not the full new ID |
| 64 | Full identity byte length:9 |
| 65-73 | Exact ASCII `FM-1_1337`, no terminator |

The reply is74 bytes; its normal SysEx CRC covers the entire reply. A new editor
accepts both legacy64-byte CAPS revision0 and this length-checked revision2.
Malformed size/revision/prefix/identity fails closed. Old editors that insist on
64 bytes reject this firmware; deploy the updated editor before using the new
image. No old fields were moved and no project/storage-format migration occurs.
The diagnostic and rollback tools also parse both layouts.

Archive imports remain lossless locally. Device library restore recognizes all
three identities900/947/1337 only when both the archive and connected device
firmware names identify FM1 DRUM OS, including same-ID restores. Archives claiming900/947/1337 without that firmware name are also rejected during import. Unknown-family
raw data is retained for backup, never implicitly converted or authorized.

## Reproduce the focused software checks

Use the existing external Windows JieLi 2.5.3 toolchain and pinned AC79 SDK as
described in BUILD.md. From `firmware/`:

```powershell
python tools/build.py --sdk "$env:AC79_SDK"
python tests/hub_package_test.py
gcc -O1 -w -D_POSIX_C_SOURCE=200809L -DPROJECT_TEST_NO_PROCESS=1 -DFELUCCA_DRUM_ONLY=1 -DFELUCCA_RUNTIME_TRACKS=1 -DFELUCCA_RUNTIME_DRUM_TRACK=0 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/hub_compatibility_test.c -lm -o build/hub-host.exe
./build/hub-host.exe
```

`--release` may change the output name/version label; it no longer chooses a
different product identity. The package test checks FWSC markers, CRC16 headers,
file bounds, both JLFS loader headers, every LZ4 block, the exact packaged app,
the actual RAM loader and its identity/signature. Host/CPU-emulator checks do not
establish physical USB or flashing acceptance.

The editor needs the CAPS parser and library-identity compatibility patch deployed
with this firmware. It accepts old/new identity aliases only when both
archives/devices identify themselves as FM1 DRUM OS. Other firmware-family,
CRC, dependency and occupied-slot checks remain in force. Existing Auto-fit and
UI patches are retained. Use the existing Pages workflow after owner review;
no new deployment workflow or backend is required.

## Owner hardware acceptance: all items still pending for this image

1. Before switching firmware, export the complete project and sample-library
   backups, retain raw A/B and sample-bank backups, their CRC/SHA-256, the old
   working image and the matching-revision recovery method. Keep another copy
   away from the device. Confirm the candidate FWSC SHA-256 against the supplied
   SHA256.txt. Do not erase legacy/protected data to make a test pass.
2. **V15 -> DRUM OS:** using the already verified updater for this FM1 revision,
   select the exact candidate file, complete both update stages without removing
   power, reconnect, and check normal MIDI identity `FM-1_1337`, editor CAPS and
   JEDEC. Confirm boot, pad playback, ordinary SAVED and an owner-performed cold
   restart. Record exact source image/version, candidate hash and result.
3. **Another custom firmware -> DRUM OS:** repeat with an identified known-good
   custom source firmware and the same candidate bytes. Confirm normal identity,
   preserved backed-up data and a successful boot. Unknown legacy records may
   remain protected: this patch performs no automatic migration or retirement.
4. **DRUM OS -> another firmware:** from a normal running DRUM OS boot, use the
   verified updater and a known-good destination package with an update loader
   supporting the physical chip. Check that DRUM OS starts that package's loader,
   that the destination boots and reports its own identity. Requiring a forced
   ROM-rescue boot to leave DRUM OS is a failed normal-transition test. Do not
   assume the other firmware supports UC25HQ80 merely because DRUM OS does.
5. Repeat the applicable acceptance on both flash variants when hardware is
   available. Software predicate checks alone are not UC25HQ80 hardware proof.
   An interrupted-update recovery test, also requested by the Hub, needs an
   owner-prepared backup/rescue plan before intentionally interrupting a write;
   it has not been performed here.

Requirements and live metadata rules: <https://thefirmwarehub.com/submit/> and
<https://thefirmwarehub.com/kb/>. The root `fmhub.json` has no browser-emulator
claim. Its existing `docs/card.png` is a validated 1024 x 1024
PNG (277091 bytes), square and below the 300 KB limit.

Do not overwrite the existing GitHub Release/tag `v1.0.0`. Review and commit the
source changes yourself, choose a new release/tag for the tested candidate and
attach its separate FWSC. Do not submit as Firmware Hub compatibility VERIFIED
until real-device transitions above pass.
