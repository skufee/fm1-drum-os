# Web Editor

Run `npm ci` and `npm run dev` inside `drum-os-editor/`, or use the prepared GitHub Pages workflow. HTTPS or localhost and a browser supporting Web MIDI SysEx are required. A previously granted permission can reconnect automatically; the first connection asks for SysEx permission through a user action. Ambiguous ports remain manually selectable. Browser security/permission checks are not bypassed.

Create kits/patterns and prepare WAVs offline. The distributed embedded previews decode the exact firmware ADPCM, dry; they do not simulate firmware FX or limiter. Local dirty state, runtime ACK, pending persistent commit and fresh SAVED are separate. Pull/Sync is explicit when local work is dirty. A failed runtime read can retain a diagnostics-only connection.

Six factory templates are read-only maps. Four USER kits and four independent project slots remain separate from the sixteen user **sample** slots. Empty sample slots need no backup; occupied replacement requires a complete backup, confirmation, CRC and firmware dependency checks. Live/USER kits and both project rollback copies can prevent deletion/replacement. Never disable these guards for convenience.

Library export/import preserves the checked archive and raw backups. Restoring a library to hardware uses validated existing upload commands, not unrestricted raw NOR writes; protected/unknown/dependent or oversized legacy records may remain blocked and are reported. Keep an external backup before modifying occupied slots.

Current user sample uploads fit 19,968 ADPCM bytes per logical slot (39,936 PCM frames, approximately 1.81 seconds at 22.05 kHz). Legacy large primary samples remain readable, but overlap-lock additional slots until explicitly backed up/replaced. See [compatibility](COMPATIBILITY.md).

If pending local source B differs from device snapshot A for one sample ID, library restore preserves B locally with unset references. Upload B explicitly before Sync; A's fingerprint is never bound to B. Library restore is not an atomic16-slot transaction.
