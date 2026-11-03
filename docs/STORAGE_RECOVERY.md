# Storage recovery — 1.0.0-rc.2

This candidate adds an explicit recovery operation, not a decoder for unknown FUNB/SLG2 data. No operation runs on connect or boot. Hardware acceptance is pending. Preserve the previous firmware and its matching-revision recovery method.

## Before any write

Stop/disarm, finish voices, and download full AUTOSAVE A/B (8192 bytes), four project pairs, all four complete sample banks (81920 bytes each), current-runtime DRM6 and diagnostic reports. Keep two verified private copies with SHA-256/CRC32. Check that runtime snapshot contents are the state you want to retain; a firmware update may boot a different state. Browser local edits are not the device seed. Restoring an archived runtime can trigger ordinary autosave and also needs approval.

New capabilities byte14 bit1 advertises controlled retirement; bit2 advertises bounded raw rollback. Old firmware cannot execute these commands and must be updated manually using the owner's already verified procedure. Firmware update itself requires separate permission. Do not use an unverified boot-button sequence.

## AUTOSAVE: two separately confirmed steps

In Dashboard choose AUTOSAVE, then **Backup + prepare ONE journal copy**. Save the downloaded archive; inspect its full original A/B, CRCs, current-runtime DRM6 seed and target. The archive is not permission to write. Check the acknowledgment and type the exact displayed phrase only after approving this particular range. **Confirm ONE retirement step** affects one 4096-byte sector; the valid other copy remains byte-identical. There is no automatic second operation or write retry.

For the observed historical pair the first target is A, 0x9F000–0x9FFFF. Original FUN6 B at 0xFE000–0xFEFFF survives. A becomes checked current-runtime DRM6. The unknown FUNB ceases to be active but remains externally backed up. Do not describe this as faithful FUNB migration.

After verification, download a NEW full A/B backup and separately prepare/review/confirm B at 0xFE000–0xFEFFF. A survives this step. Only after both modern records are verified does the normal strict A/B save guard accept ordinary autosave. Never erase both together. Never authorize a target that differs from the reviewed plan.

Failure, timeout, disconnect or uncertain ACK: do not retry the write automatically. Read recovery status and take a fresh raw backup. An interrupted target can be partially erased/programmed; the survivor remains available. Recovery preparation expires after 120 seconds of inactivity without writing.

## USER banks

All live pads, four USER kits and both copies of all four projects/AUTOSAVE must have proven absent references to the bank's four logical slots. Unknown references remain a blocker. A project load alone does not remove references from its rollback copy. Resolve dependencies explicitly; do not clear them to force an upload.

The physically empty banks1/2 need no erasure. After AUTOSAVE is known, their eight logical slots become usable through existing guards.

For archived unknown bank0 and bank3, separately use **Backup + prepare ONE sample bank**, save/review its full archive and type its exact phrase. Targets:

| Physical bank (zero-based) | Range | Size | Logical USER slots |
|---|---|---|---|
| 0 | 0xA0000–0xB3FFF | 81920 | 1,5,9,13 |
| 3 | 0xE7000–0xFAFFF | 81920 | 4,8,12,16 |

One confirmation clears one bank only, using its existing twenty 4KiB sectors, with readback and rescan. The former SLG2/unknown contents are retired, not converted. Banks1/2, projects, settings, bootloader and layout are not changed. A failed bank erase can leave that bank partial; there is no atomic spare bank. Keep complete backups and stop on errors.

## Exact reverse recovery

The application exposes fixed-object/bank raw rollback, not arbitrary-address writes. `firmware/tools/storage_rollback.py --prepare ...` downloads the current complete target backup and writes a local review plan. It stages RAM proof only. `--execute` requires a separate exact RESTORE phrase including address, length and archived source SHA-256. No write is retried. Runtime state is not restored by raw rollback.

The optional Python tool requires `mido` and `python-rtmidi`; install those separately or supply your existing dependency directory with `--deps`. The browser retirement workflow does not require Python.

The tool streams sequential 256-byte pages with CRC/readback; the first page and commit header are programmed last. Each journal step requires a valid other-copy survivor. AUTOSAVE raw rollback holds all automatic saves until a real cold boot or separately confirmed successful retirement; CANCEL/timeout do not lift that hold. The old unknown-format NO SAVE can return after rollback by design. Bank rollback requires no persisted/live references, including rollback copies. Restore banks before reintroducing an unknown AUTOSAVE; perform each target as a separately approved operation. An interrupted bank rollback is not atomic; complete originals remain external.

If a hardware test assigned a new USER sample, first obtain separate approval to remove its references from live pads and USER kits and persist that reference-free state. Both AUTOSAVE rollback copies must become reference-free: after the first verified save, make one explicit harmless persisted change (for example selected pad) and wait for a second verified save, respecting the unchanged20-second interval. Identical snapshots do not force an extra NOR write. Download/decode both copies and verify references before any bank restore. Restore the desired reference-free seed afterward if needed. This is never automated by the rollback tool and also requires approval for those AUTOSAVE writes.

Example syntax (owner only; do not run --execute without explicit approval):

```powershell
python firmware/tools/storage_rollback.py --prepare --kind journal --target 7 --copy 0 --source ORIGINAL-A.bin --out rollback-review-A
# Review rollback-review-A/rollback-plan.json and required_confirmation, then separately authorize.
python firmware/tools/storage_rollback.py --execute rollback-review-A/rollback-plan.json --out rollback-result-A --confirm "EXACT REVIEWED RESTORE PHRASE"
```

## Hardware acceptance after approval

Verify version/capabilities and all original hashes again. Restore only the explicitly reviewed modern runtime seed if needed. Execute and verify one journal step at a time, then each bank individually. Change a pattern/pad/FX, STOP/disarm, wait for quiet idle and a fresh durable SAVED. The normal 20-second write interval remains. Have the owner fully power the unit off/on, then compare the restored wire state. Upload a short ADPCM test to an unreferenced slot, verify header/data CRC/readback, preview/START/END and pad playback, then another real cold boot. A SysEx ACK, emulator restart or USB reconnect is not physical power-loss acceptance.

## Updating the existing Pages repository

Review the updated `firmware/`, `drum-os-editor/`, docs and workflow in your existing repository; preserve owner changes and Auto-fit history. Run `npm ci`, `npm run typecheck`, `npm test`, `npm run build`, `npm run test:browser` in `drum-os-editor/`. The manual workflow uses that directory's lockfile and `dist`, and the relative Vite base supports repository subpaths.

```powershell
git status --short
git diff --stat
# Stage only reviewed source/documentation; never add raw backups or private diagnostics.
git commit -m "Add explicit backup-bound storage retirement and rollback"
git push
gh workflow run editor-pages.yml -f publish=true
```

These commands are instructions for the owner; preparation does not push, deploy or flash. Update the release download separately with the reviewed FWSC and SHA-256. Do not call the candidate RELEASE VERIFIED before actual SAVE/cold boot and USER upload pass on the device.
