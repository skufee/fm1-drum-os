# FM1 DRUM OS

**1.0.0-rc.2 — hardware acceptance pending.** A SLOOP/Felucca-derived drum sampler for the M-Vave FM1: 8 pads, 6 sample voices, 55 embedded sources, 16 bounded user sample slots, a 64-step sequencer, lane/master effects, and one integrated Web MIDI editor.

This repository contains the firmware and editor together. It contains no original WAV library, SDK/compiler binaries, device dumps or prebuilt firmware. Required embedded ADPCM data is included with the owner's confirmed redistribution permission. Source code is GPL-3.0-only; preserve the [third-party notices](THIRD_PARTY_NOTICES.md).

- `firmware/`: active drum-only firmware sources, build tools, licensed generated sample bank and focused host tests. The inner `firmware/` directory retains the upstream source layout.
- `drum-os-editor/`: TypeScript/Vite editor, offline sample preparation/library backup and Web MIDI SysEx.
- `docs/`: [explicit storage recovery](docs/STORAGE_RECOVERY.md), [build](docs/BUILD.md), [device update](docs/FIRMWARE_UPDATE.md), [editor](docs/EDITOR.md), [compatibility](docs/COMPATIBILITY.md) and [publication](docs/PUBLISHING.md).
- `.github/workflows/editor-pages.yml`: manual build/test/deploy workflow; publication defaults to disabled.

No hardware acceptance or RELEASE VERIFIED claim is made. Read the limits before writing projects/samples or updating firmware. Factory sample IDs and approved kit mappings remain stable.
