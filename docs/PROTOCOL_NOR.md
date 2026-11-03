# Read-only NOR diagnosis — Drum protocol 1 extension

Firmware 1.0.0-rc.1 adds capability `CAPS[14] & 1`. Existing FL command80 framing, application version1, 7-bit packing, request/reply CRC32, transaction retry and 256-byte chunks remain unchanged. No new write command is added. Older firmware lacks the bit and must not receive these subcommands automatically.

Opcode27 retains its old explicitly requested backup/recovery preparation subcommands0–2. New subcommands3–5 are independent read-only operations: no recovery token, backup lock, erase/program, runtime update or sample rescan/publication. They require STOP, no REC/ARM, quiet audio and no upload/restore. NACK1=bounds/malformed,3=busy,6=read/flash error. They preserve the last SAVE diagnostic. Reads may fail if bytes change during an export; clients compare reports before/after and verify raw CRCs.

## Journal report

Request `[3,object]`: object1–4=projects,7=AUTOSAVE; all other objects rejected. Response152 bytes:

| Offset | Field |
|---:|---|
| 0..3 | schema1,kind0,object,copy count2 |
| 4,8,12,16 | LE32 total8192,physical A offset,physical B offset,CRC32(A4096 followed by B4096) |
| 20..23 | selected **outer journal** copy0/1/255,readOK,existing DRM repairable flag,reserved |
| 24,88 | Copy A and B rows,64 bytes each |

Each copy row: state@0 (0erased/1outer-valid/2corrupt/3read-unknown), format@1,ownedDRM-prefix@2,reason@3; LE32 offset@4,length@8,sequence@12,declared payloadCRC@16,raw sectorCRC@20; raw32-byte header@24 and raw8-byte payload prefix@56. Outer-valid does not imply the application can decode that payload or restore it on cold boot.

Formats:0 unknown;1–6 exact valid DRM1–6;0x11–0x16 exact valid historical FUN1–6;254 erased. FUN identity requires the existing source-defined length, numeric magic and FNV checksum. FUN6 numeric magic0x46554E36 is raw little-endian `36 4e 55 46`. FUNB numeric magic0x46554E42 is **unknown schema**, never accepted as FUN6 or granted ownership for repair.

Reasons:0 knownDRM,1erased,2read failure,3invalid outer FELU header,4payload CRC failure,5outer-valid unknown payload,6legacyFUN protected,7DRM signature but complete validation fails. Known legacy identity only labels the report; it does not authorize conversion.

## Full journal raw read

Request `[4,object,offsetLE16,countLE16]`: count1–256; offset0–8191; cannot cross a4096-byte sector boundary. Response: offsetLE16@0,countLE16@2,totalLE32=8192@4,raw bytes@8. Offsets0–4095 refer to A,4096–8191 to B. AUTOSAVE physical sectors are nonadjacent: A0x9F000/B0xFE000. Verify pair and both individual sector CRCs against the report. A complete pair is8KiB total, not8KiB per copy.

## Logical USER/physical bank report

Request `[5,slot]`, zero-based0–15. Response160 bytes:

| Offset | Field |
|---:|---|
| 0..3 | schema1,kind1,slot,physical bank0–3 |
| 4,8,12 | LE32 logical offset,logical length20480,raw backup length81920(primary slots0–3) or20480(child) |
| 16,20,24,28 | LE32 bank offset,bank length81920,logical CRC32,full bank CRC32 |
| 32,36,40,44 | LE32 FSMP declared length,declared CRC,computed payload CRC,verified parent's declared extent |
| 48..51 | cached class0empty/1valid/2intent/3protected;format;reason;flags |
| 52..55 | live lane-reference mask;USER kit-reference mask;persisted dependencies signed8 (-1indeterminate/0none/1referenced);reserved |
| 56..119 | raw64-byte logical header |
| 120..151 | raw32-byte physical parent header |
| 152,156 | LE32 sample fingerprint,first nonFF byte within logical slice (0xFFFFFFFF erased) |

Flags: bit0access,bit1cachedready,bit2content protected,bit3payload CRC computed. These are **application policy**, not NOR BP registers. Formats:0unknown,1erased,2FSMP one-zone,3FSMP multizone unsupported,4FSMP unsupported/invalid,5SLG2 signature only (schema unknown),6authenticated FUP1 interrupted upload. Reasons:0ready,1erased,2unknown/unsupported parent,3parent overlap,4unknown header,5FSMP version,6multizone,7zone/bounds,8CRC,9child tag,10interrupted intent,11read error,12unsupported loop.

Persisted-reference uncertainty is separate from content protection. Any non-erased project/AUTOSAVE copy outside recognized valid DRM prevents proof that a sample has no dependencies, even if its LIST flags are zero. Report source limitations rather than calling it an unreferenced free slot.

Physical banks:0xA0000,0xB4000,0xC8000,0xE7000, each0x14000 bytes. Logical offset = bank[slot%4]+floor(slot/4)*0x5000. Banks remain disjoint from projects0x97000–0x9EFFF,AUTOSAVE0x9F000/0xFE000,presets0xDC000–0xDFFFF,OTA0xE0000–0xE4FFF,FM60xE5000–0xE6FFF and settings0xFC000/0xFD000. Full raw banks use existing opcode19 kind2 targets0–3. Before/after reports and per-bank/per-slice CRCs must match the downloaded bytes.

Unknown or both-corrupt records remain blocked. Neither diagnostics nor downloading backups authorizes recovery. Existing opcode28 requires a complete checked backup and separate explicit owner confirmation, and still only permits the already supported one-valid-DRM/one-owned-corrupt-DRM repair. It cannot clear unknown FUNB/SLG2 or convert legacy FUN automatically.
