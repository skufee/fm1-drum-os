# Firmware update and acceptance

This candidate is `1.0.0-rc`, package identity `FM-1_900`. Use only the update method and recovery procedure already verified for your exact FM1 hardware revision. No universal boot-button sequence or recovery method is asserted here; obtain the matching manufacturer's/upstream updater separately. This repository does not automatically flash a device.

Before an update, save projects, export the complete user-sample library and retain available raw A/B backups, the previously working firmware, the new FWSC SHA-256 and a matching-revision rollback/recovery path. The source repository intentionally excludes release binaries; use the separately supplied matching release bundle. Do not repartition NOR or erase unknown data to make an update/save appear successful.

After an owner-controlled update, verify firmware version, identity and diagnostics in the editor. Test edit → STOP/disarm → quiet idle → fresh **SAVED** → real power cycle. First changed autosave requires at least 2.5 seconds of input idle and no active voices/transport; subsequent attempts retain a 20-second minimum interval. WAIT or a runtime SysEx ACK is not durable-save confirmation. NO SAVE exposes an error/stage/raw result/JEDEC; preserve that information and raw backups before recovery.

Unknown, corrupt/unidentified and protected legacy data remains fail-closed. One-use A/B repair requires the complete verified backup, explicit confirmation and one valid survivor; never erase both copies. Real flash latency, power interruption, USB/MIDI, sample listening, CPU/stack headroom and long-run operation require separate hardware acceptance before 1.0.
