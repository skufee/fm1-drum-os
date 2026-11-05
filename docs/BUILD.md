# Build and tests

Use Python 3.13, Pillow 12.2.0, the Windows-native JieLi 2.5.3 toolchain and the pinned AC79NN SDK V1.2.1 (2023-12-13). Obtain SDK/toolchain separately under their original terms; neither is bundled. Set `JIELI_TOOLCHAIN` and `AC79_SDK` to your own local directories. No WSL is required. The SDK payload checksums in the build tools must match the validated revision; do not bypass mismatches.

From the repository root:

```powershell
python -m pip install -r firmware/requirements.txt
cd firmware
python tools/build.py --sdk "$env:AC79_SDK"
python tests/run_host.py
cd ../drum-os-editor
npm ci
npm run typecheck
npm test
npm run build
npm run test:browser
```

Node 24.6+ is required. Host C tests additionally require `gcc` on PATH; they use explicit peripheral/project-service doubles and do not establish physical USB/NOR acceptance. Browser tests use installed Chrome on Windows; on Linux first run `npx playwright install --with-deps chromium`.

The firmware build outputs `firmware/build/felucca.{bin,elf,fwsc}`. Internal output names preserve the upstream tooling; the application reports version `1.0.0-rc.2` and keeps hardware package identity `FM-1_1337`. Do not use the upstream `--release` option to change that identity.

Normal public builds require no WAVs: `assets/embedded/felucca_samples.h` is the exact checksummed generated bank. `tools/sync_embedded.py --editor ../drum-os-editor` verifies firmware/browser bytes and LCD waveform metadata after a build. If deliberately regenerating an authorized bank, set `FM1_SAMPLE_SOURCE` to the external 55-WAV directory, run `python tools/gen_samples.py build/gen/felucca_samples.h`, then `tools/sync_embedded.py --update --distributable --editor ../drum-os-editor`. Do not commit WAVs or gain-normalize/change IDs casually.

The owner confirmed identity FM-1_1337. Physical update-transition acceptance remains pending; CAPS revision2 requires the updated editor. See [the candidate checklist](FIRMWARE_HUB.md).
