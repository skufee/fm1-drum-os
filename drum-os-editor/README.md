# FM1 DRUM OS Web Editor

A static, offline-first drum editor for the8-pad/6-voice FM1 DRUM OS firmware. It extends the existing FL USB MIDI SysEx transport; it does not flash firmware, replace USB drivers or run a server backend.

## Run locally

Use Node24.6 or later on Windows (no WSL required):

```powershell
npm ci
npm run typecheck
npm test
npm run build
npm run dev
```

Open the displayed localhost URL. Production assets are in dist/. Serve them over HTTPS or localhost for Web MIDI SysEx. Opening index.html directly with file:// is not a supported MIDI/module setup. Offline means operation without FM1; initial loading of the static app/assets is still required. The app does not install a service worker or claim cached-network-offline installation.

## Workflow

1. Create a kit offline, choose one of the six exact read-only factory templates, edit eight pads, current/fourUSER kit snapshots, lane/master FX and all64 steps/raw offsets. Export a checked .fm1.json editor file; localStorage is convenience storage, not the only backup.
2. Samples includes real dry previews for all55 embedded ADPCM sources. Import a WAV, trim, choose a sample rate, convert to mono and encode IMA locally. The corrective firmware exposes16 logical one-zone slots inside the same four existing80KiB regions. New uploads use20KiB slices with19,968 ADPCM bytes each (up to1.81s at22.05kHz). EmbeddedIDs0–54 and olduserIDs55–58 remain stable; newIDs59–70 address the extra slices. Existing large primary sources up to81,408bytes remain readable/backed up and can protect overlapping extra slots; no automatic conversion/erase occurs. The editor also connects to the original4-slot RC3 firmware and obeys its advertised count/capacity. OfflineJSON can retain oldlargeprimary sources; they cannot be uploaded above the connected slot limit. Browser source audition is dry; firmware effects are not simulated.
3. Use Connect USB MIDI once to request SysEx permission and connect a unique FM1 port pair. If several matching ports exist, select the pair explicitly. On a later visit, a Permissions API query may reconnect a unique FM1 pair or a previously verified port pair only when SysEx permission is already granted; prompt/unknown/denied states never trigger a background permission request. A valid capability handshake precedes device writes. The editor reads device runtime; if local work is dirty it is retained until explicit Pull or Sync. Physical device changes are detected by revision polling; dirty local work is not silently discarded.
4. Sync runtime requires STOP/disarmed state. Runtime ACK is distinct from persistent commit. WAIT means pending safe idle/write interval; SAVED comes from a fresh durable device-status read. SAVE FAILED keeps local edits and the exact error reason visible.
5. If CAPS is readable but runtime STATE fails, the connection remains diagnostics-only. Save diagnostics/JEDEC and raw backup access remain available; device mutations are blocked until a complete successful Pull.
6. Projects exposes four independent slots. Manual SAVE/LOAD, exact binary DRM backup and checked DRM1–DRM6 restore are distinct from editor JSON. Existing objects are backed up before confirmed overwrite. Unknown records are never interpreted as empty. Sample replacement/deletion requires full raw backup, confirmation and firmware dependency checks; live kits/USER kits/both rollback project copies count.
7. Recovery first downloads the complete8KiB A/B pair with CRC proof. Explicit confirmation can authorize one strictly classified repair of a known damaged inactive copy. Authorization is not NOR success: request save and poll the resulting status. Unknown/both-corrupt records remain blocked. Consult the project's HARDWARE_SAVE_DIAGNOSTICS.md for device procedures.

BPM bounds follow the device and existing DRM formats:40–240. START must remain below END. Raw timing is -32..31 in1/64-step units. Session-only QTZ/record preferences retain the firmware persistence contract.

## Tests

```powershell
npm test
npm run build
npm run test:browser
```

Unit tests cover canonical7-bit framing/header+payload CRC, correlation/timeout/retry/NACK/disconnect, exact factory maps, JSON CRC/bounds,55 real source decodes, IMA golden vectors from firmware tools, raw schemas, interrupted transfers and proof-gated recovery. Browser tests use a real headless browser for offline editing/import/export/audio preparation, all eight pages and tablet layout, unsupported/denied MIDI, a deterministic mock MIDI peer, ACK-versus-SAVED/error/disconnect and diagnostics-only access.

Windows browser tests use installed Chrome; Linux CI uses Playwright Chromium (`npx playwright install --with-deps chromium`). Browser tests serve the built app under /fm1-editor/ to check the relative GitHub Pages base path. Results/screenshots go to ../../artifacts/release-1.0-prep/editor/. Mock MIDI PASS is not real USB, NOR or physical FM1 acceptance.

## GitHub Pages preparation

The repository workflow `.github/workflows/drum-editor-pages.yml` is manual `workflow_dispatch` only. Its publish input defaults to false. No push-triggered publication, credentials or deployment was created here.

For an owner-authorized deployment: enable GitHub Pages with GitHub Actions as source; choose the intended repository/branch; run the workflow with publish enabled. The artifact is drum-web/dist and becomes that repository's Pages site. If retaining an existing Pages site, choose the deployment destination/repository before publishing. Vite base is `./`, so the same build supports https://OWNER.github.io/REPO/ or a static subfolder. The checked package-lock.json pins dependencies.

Workflow wiring follows the official [GitHub Pages custom workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) and [setup-node documentation](https://github.com/actions/setup-node). The workflow itself has not been executed against GitHub during local validation.

## Provenance

See public/PROVENANCE.md, embedded-audio.json and licenses/. Sample bytes derive from owner-provided project material; no blanket CC0 claim or proprietary Roland/Akai DSP/asset copying is made. Font assets carry their OFL notices. No SDK/compiler binaries are included in the web build.

## Focused corrective checks

`npm run typecheck`, `npm test`, `npm run build`, then `npx playwright test --config playwright.corrective.config.ts`. The focused browser scenario checks offlineUSER16, CAPS16/list71, highest-ID selection and overlap protection with a deterministic MIDI peer. It does not repeat the full A9/browser matrix and does not establish physicalUSB orNOR acceptance. Evidence is written to artifacts/corrective-pass/editor/ at the workspace root; the prior RC3 reports/evidence remain intact. Four project slots and four USER kits are unchanged.

## 1.0.0-rc connection and sample-library workflow

Empty sample slots upload directly after a fresh source-list read; they do not require a backup. An occupied source is downloaded in full before an explicit replacement confirmation. The pending local WAV/ADPCM is retained while its previous device source is backed up. Firmware CRC, capacity, overlap, dependency and durable-save checks remain authoritative.

Samples offers **Export library**, **Import library** and **Restore imported sources**. The checked `.fm1lib.json` file stores local ADPCM work and, when connected and stopped/disarmed, a complete raw snapshot of every user sample slot: four primary80KiB banks and additional20KiB children. Metadata/fingerprints, individual raw CRCs, whole-file CRC and duplicated-bank consistency are checked. Embedded55 sources are immutable firmware assets and are not replaced by this archive. Export does not write to FM1.

Import preserves the entire raw snapshot in memory and the exported file; validated sources can be edited locally. Keep the archive file: ordinary editor project JSON/localStorage stores local source work, not the complete raw-bank snapshot. Restore sends supported validated one-zone sources through the existing uploader. Matching fingerprints are skipped; empty slots need no backup; each occupied target gets a full downloaded backup and explicit confirmation. Guard/dependency failures remain visible per slot in a downloaded result report. A whole-library restore is a sequence of guarded source operations, not an atomic transaction or a raw NOR restore.

Unknown, corrupt, multizone, unsupported-rate and oversized legacy records remain lossless raw backup data; they are not silently converted, truncated, erased or restored through an invented raw-flash command. New uploaded header fingerprints can require a separate explicit reference/project synchronization, and firmware may refuse replacement while any live/USER/persisted rollback copy still depends on the target. Physical USB/NOR acceptance remains separate from mock tests.

Focused release-editor checks: `npm run typecheck`, `npm test`, `npm run build`, `npx playwright test --config playwright.release.config.ts`. This configuration writes current evidence outside source under artifacts/release-1.0-prep/editor. Release workflow/deployment files are prepared in the separate public export, not published by these commands.

If a library contains pending local audio B and a different device snapshot A for the same ID, restoring A preserves B locally with unset project references. Upload B explicitly before Sync; the editor never binds A's fingerprint to B. Raw/unsupported records stay in the archive and are not written through an unchecked NOR restore.

Read-only NOR diagnosis (1.0.0-rc.1): explicit full AUTOSAVE8KiB and four-bank raw archives, classification/header/CRC/reference reports. Unknown FUNB/SLG2 stays protected; backup never grants migration permission. See ../docs/physical-nor/NOR_DIAGNOSTICS.md. Owner Auto-fit modes and16USERslots are preserved.
