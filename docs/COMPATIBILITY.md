# Compatibility and known limits

- Embedded IDs 0–54 retain their exact category/name/number identity: HAT1–14, KICK1–12, OH1–3, PERC1–10, SNARE1–16. Six approved factory kits/48 assignments are unchanged.
- DRM6 remains 1,959 bytes, preserving DRM1–5 reads. User IDs55–58 keep their original primary locations; additional IDs59–70 use new logical slices. Older firmware may reject projects containing those new IDs. Back up before rollback; an old 80 KiB uploader is not safe for banks containing new children.
- Six voices serve eight lanes. Eight lane FX and six master FX plus OFF are retained. Session-only record/quantization preferences do not become durable merely because the editor stores them locally.
- Sixteen logical 20 KiB slots share the same four 80 KiB user regions. New compressed capacity is19,968 bytes each; old primary sources up to81,408 bytes are retained on read. Overlapping children are locked. An unknown/corrupt/multizone primary masks its children conservatively; raw backups remain possible. No automatic data conversion, erasure or NOR repartition is performed.
- Samples have no atomic second onboard copy. Occupied replacement/deletion requires an external backup and no references in live/USER/project rollback states. Interrupted unidentifiable records remain protected. A/B project durability still requires a verified commit.
- Source full-scale PCM values already exist in the final bank (about2.12% total; KICK12 about16.11%). No source gain changes were applied. IMA is lossy and bounded; physical listening/critical-clipping acceptance is pending.
- Static `.data+.bss` headroom is limited (about4.4 KiB); actual stack/timing, NOR power-loss behavior, USB/MIDI and final hardware acceptance remain pending. `1.0.0-rc` is not RELEASE VERIFIED.
