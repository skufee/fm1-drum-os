# FM1 DRUM OS

**A drum-focused sampler and sequencer for the M-VAVE FM-1.**
<img width="1448" height="1086" alt="Студийный синтезатор M‑VAVE FM‑1 с интерфейсом драм-машины" src="https://github.com/user-attachments/assets/df87ebe0-6ccd-4caf-89cc-1c98ceb015eb" />
FM1 DRUM OS turns the FM-1 into a compact, hands-on drum machine built around sampling, unquantized playing, expressive sequencing and sound shaping. Make a beat on the hardware, adjust individual drum lanes, or use the browser editor to manage kits, patterns and your own samples.

**[Web Editor](https://skufee.github.io/fm1-drum-os/)** · **[Download firmware](https://github.com/skufee/fm1-drum-os/releases)** · **[Firmware update guide](docs/FIRMWARE_UPDATE.md)** · **[Report an issue](https://github.com/skufee/fm1-drum-os/issues)**

> **Current release:** v1.0.0 — Initial Release.
>
> Project saving, custom sample uploads and playback have been
> tested on physical M-VAVE FM-1 hardware.
> Back up your projects and user samples before updating.

## Features

- **8 drum lanes** with independent sample assignments, level, pan and mute.
- **6 simultaneous sample voices** using the embedded sample engine.
- **55 built-in drum samples** across kicks, snares, hats, open hats and percussion.
- **16 user sample slots** for custom sounds, with upload and backup through the Web Editor.
- **Up to 64 sequencer steps** with swing, microtiming, adjustable quantization strength and ratchets.
- **Live recording** with overdub/replace, optional count-in and metronome.
- **Per-pad sound editing** including tune, start, end and decay.
- **8 lane effects and 6 master effects** for shaping individual hits and the drum bus.
- **6 factory kits**, 4 user kits and 4 project slots.
- **Autosave and project recovery** so you can pick up where you left off.
- **USB MIDI / SysEx** integration with a browser-based editor.

The emphasis is on *playing* rather than programming: record loosely, tighten the groove only as much as you want, and make the drums your own.

## Web Editor

**[Open FM1 DRUM OS Web Editor](https://skufee.github.io/fm1-drum-os/)**

The editor runs in a compatible desktop browser and lets you:

- Edit kits, pads, patterns, effects and projects.
- Import WAV files, convert them to mono IMA ADPCM and fit them into a user sample slot.
- Preview sounds before sending them to the FM-1.
- Upload custom samples to the 16 user slots and assign them to pads.
- Export and import sample-library backups.
- Work offline, then connect to the device over USB MIDI when ready.

Use **Chrome or Edge** with a **USB data cable**. Web MIDI with SysEx permission is required for device communication. The first connection requires browser permission; later sessions can reconnect automatically if permission is retained.

**Sample limit:** each new user-sample upload supports up to **19,968 bytes of IMA ADPCM** (roughly **1.81 seconds at 22.05 kHz**). The editor can trim or resample longer WAV files to fit. Older large-format samples and protected data may have additional compatibility restrictions.

## Getting started

1. **Back up your data.** Export projects and the user sample library with the Web Editor before changing firmware.
2. **Get the firmware.** Download the `.fwsc` file for your device from [Releases](https://github.com/skufee/fm1-drum-os/releases).
3. **Update the FM-1.** Follow the [firmware update guide](docs/FIRMWARE_UPDATE.md) and use the updater appropriate for your hardware. Do not assume an update is reversible without a matching backup.
4. **Make a beat.** Pick a kit, play the pads and record a pattern. Use swing and quantization strength to shape the timing.
5. **Add your own sounds.** Open the [Web Editor](https://skufee.github.io/fm1-drum-os/), import a WAV, choose Auto-fit, preview it and upload it to a free USER slot.

## Sequencing and sound

FM1 DRUM OS is designed for drum programming and live performance rather than general-purpose synthesis. Each of the eight lanes can use any available sample, including the same source on multiple pads. The sequencer supports both intentionally loose timing and grid-based patterns, with per-step adjustments and effects to add movement and character.

The included factory kits provide starting points, not fixed instruments: change sample assignments or create your own kits in the editor.

## Compatibility and important notes

- **Hardware:** M-VAVE FM-1, compatible with firmware package identity `FM-1_900`.
- **Editor:** a desktop browser with Web MIDI SysEx support; HTTPS is provided by GitHub Pages.
- **Storage:** 16 logical user-sample slots share four physical flash banks. Existing legacy samples can occupy multiple logical slots; protected or unknown data is not overwritten automatically.
- **Projects:** newer projects that reference USER sample IDs 59–70 may not work correctly on older firmware. Always make backups before downgrading.
- **Saving:** a runtime acknowledgement is not the same as a completed flash save. Confirm the device reports **SAVED** before switching it off.
- **Audio preview:** browser playback previews the sample data, not the complete hardware effects/output path.

See [Compatibility & known limits](docs/COMPATIBILITY.md) for details.

## Documentation

- [Firmware update](docs/FIRMWARE_UPDATE.md)
- [Web Editor](docs/EDITOR.md)
- [Compatibility and known limits](docs/COMPATIBILITY.md)
- [Build from source](docs/BUILD.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Build from source

The firmware source and the Web Editor live in the same repository. Firmware builds require the separately obtained **JieLi AC79 toolchain and matching SDK**; they are not included here. The Web Editor is a TypeScript/Vite project.

```bash
cd drum-os-editor
npm ci
npm run build
```

For native firmware build requirements and tests, see [BUILD.md](docs/BUILD.md).

## Credits and license

FM1 DRUM OS is an independent, drum-focused adaptation of **[SLOOP](https://github.com/isod89/sloop-fm1)** and the **Felucca** firmware work by Leo Kuroshita / Hügelton Instruments. Thanks to the upstream developers and the wider FM-1 community.

Firmware/editor source is distributed under **[GPL-3.0-only](LICENSE)**. Third-party components, fonts and embedded audio have their own notices and permissions; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

*This is an independent community firmware project and is not an official M-VAVE product.*
