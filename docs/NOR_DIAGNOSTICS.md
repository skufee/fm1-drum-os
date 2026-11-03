# Safe NOR diagnostics and update

1.0.0-rc.1 is a diagnostic compatibility candidate. It does not silently clear protected data or claim hardware release acceptance. Auto-fit and16 USER slots remain available.

## Read without changing the device

Connect the editor normally. Connecting does not run recovery or new NOR probes. On capable firmware, Dashboard offers **Read AUTOSAVE A/B classification**, **Download AUTOSAVE raw + report**, and **Download sample banks + report**. The first complete archive includes the8192 raw A/B bytes as base64 plus report/CRCs. The second includes all four full80KiB banks and all16 logical-slot reports. Separate buttons then download each raw `.bin`, without relying on browser multiple-download permission. Samples also offers **Read selected slot protection**. Keep these backups privately; do not commit dumps or project data to GitHub.

On older firmware the new probes are disabled. The existing separately clicked A/B backup prepares only a RAM lock; no repair occurs without the separate authorization/confirmation action. Downloading a backup is not permission to erase. Runtime ACK/WAIT is not SAVED. Browser permission requirements remain intact.

The reports distinguish valid outer journal CRC from known application payload, unknown signatures from recognized FSMP/FUN, empty headers from nonempty sector bodies, physical parent overlap and indeterminate persisted references. Flags4 do not prove hardware write-protect bits.

## Proven compatibility case

An examined FM1 had AUTOSAVE A at0x9F000: valid FELU header/payloadCRC,seq3,len3640,numeric magic0x46554E42 (FUNB signature, schema unknown). B at0xFE000: valid FUN6,seq2,len3816. Highest-sequence outer A is selected, but DRM/FUN1–6 decoders cannot import FUNB; the save guard rejects implicit overwrite. This is a data-format compatibility blocker, not proof of flash erase/program failure. New diagnostics expose the distinction; they do not mislabel FUNB as a supported format.

USER1/5/9/13 share bank0xA0000 with an SLG2 primary signature (schema unknown). USER4/8/12/16 share bank0xE7000 with unknown nonempty contents; the primary's header isFF but other bytes in its first20KiB are not. Unknown parents protect their children, including individually erased children. Unknown AUTOSAVE also prevents proving absence of sample references elsewhere. Neither pattern permits automatic clearing.

## Recovery decision after backup

1. Keep two verified copies of the raw A/B pair, four complete sample banks and project backups, their SHA/CRC manifest, diagnostics, and previous matching firmware/recovery path. A library JSON alone does not contain AUTOSAVE.
2. Obtain the original writer/specification for unknown FUNB/SLG2 before proposing semantic migration. Valid CRC alone does not define the payload. Known FUN6 can be identified but is not automatically converted or erased by this candidate.
3. If a format cannot be identified, decide explicitly whether its backed-up data may be retired. That needs **separate operation-specific owner consent**, exact target ranges/hash checks and a reviewed recovery implementation. This candidate provides no unknown-data clear command. Do not format all NOR or bypass protection flags.
4. The existing confirmed DRM repair remains limited to one valid known DRM survivor and one positively owned CRC-damaged DRM copy. Both-corrupt/unknown/legacy records fail closed. Preserve the valid survivor and rollback data.
5. After any separately approved future migration/clear, test edit→STOP/disarm→quiet idle→fresh SAVED→real power cycle, exact pad/pattern/raw/FX/USER restoration, then upload a backed-up test sample and verify CRC/START/END/voices. Do not infer durable save from a SysEx ACK.

## Update firmware / Pages

Firmware updates require owner action with the already validated matching-revision procedure and backups. No flash command is run by these tools. After updating to1.0.0-rc.1, confirm CAPS version and diagnostics bit before new probes. Physical read-only checks on1.0.0-rc do not establish acceptance of the new firmware.

To update an **existing** source repository/site, first review `git diff`, preserve owner changes and the Auto-fit commit, then commit/push the reviewed source changes yourself. Run the existing manual `editor-pages.yml` workflow only when deployment is authorized. It builds from `drum-os-editor/package-lock.json` and publishes `drum-os-editor/dist`; `base: './'` supports repository subpaths. Example owner commands:

```powershell
git status --short
git diff --stat
# Review/stage the intended source and docs, then:
git commit -m "Add read-only physical NOR diagnostics"
git push
gh workflow run editor-pages.yml -f publish=true
```

Do not reinitialize an existing repository or publish raw backups. No push/deployment or physical migration is performed by this preparation pass. See [NOR protocol](PROTOCOL_NOR.md) and the existing [build guide](BUILD.md).
