# Application recovery protocol (FL80 v1 extension)

Version 1.0.0-rc.2 uses the existing USB MIDI transport, framing, 7-bit packing, CRC32, ACK/NACK and chunk limit. No HAL/layout change. GET_CAPABILITIES byte14 bit0: read-only NOR diagnostics; bit1: controlled retirement; bit2: bounded raw rollback. Opcode30 is recognized only after capability handshake; older firmware rejects it. Payload integers are little-endian. Explicit operation marker is0x5A. Reads remain idempotent; destructive commands have no automatic client retries. A transport ACK is not an ordinary durable SAVE acknowledgment.

## Opcode30 subcommands

| Subcommand | Request following sub byte | Result / action |
|---|---|---|
|0 PREPARE_JOURNAL|object8 (1–4 or7)|48B plan; requires complete fresh8192B opcode27 backup and valid other-copy survivor. Captures current runtime as1959B DRM6, not historical semantic migration. Healthy modern pairs refuse unnecessary retirement.|
|1 READ_SEED|offset16,count16≤256|offset16,total16,CRC32,data. Complete sequential download required; immediate preceding duplicate accepted.|
|2 COMMIT_JOURNAL|token32,pairCRC32,seedCRC32,targetCopy8,0x5A|20B verified result. One exact sector; precondition seed still equals current RAM and original pair/survivor unchanged. Other copy preserved.|
|3 PREPARE_BANK|bank8 (0–3)|48B plan; requires full81920B opcode19 kind2 primary-bank backup, unchanged CRC and no live/USER/persisted/rollback references. Empty banks refuse unnecessary retirement.|
|4 COMMIT_BANK|token32,bankCRC32,bank8,0x5A|20B result after twenty bounded4KiB erases, complete FF readback and rescan. One bank only.|
|5 STATUS|none|32B status, read only.|
|6 CANCEL|none|Cancel RAM proof only. No erasure, no reversal of completed/partial operation, no release of raw-autosave hold.|
|7 PREPARE_RAW|kind8(0journal/1bank),objectOrBank8,copy8(0/1journal,255bank),sourceCRC32,first256RawBytes|48B plan kind2/3. Same complete current-backup proof, journal valid survivor, bank dependency guards; source journal header must validate. Stage firstpage in existing scratch RAM only.|
|8 START_RAW|token32,currentSnapshotCRC32,sourceCRC32,0x5A|Erase only approved fixed target. AUTOSAVE invalidates durable cache and sets persistent-in-session autosave hold BEFORE erase.|
|9 RAW_PAGE|token32,offset32,256bytes|nextOffset32 after program/readback. Sequential offsets≥256. Immediate duplicate is checked read-only, not reprogrammed.|
|10 FINISH_RAW|token32,sourceCRC32,0x5A|20B result after full rolling/readback CRC, firstpage/header programmed last, fullraw CRC and survivor verification. No semantic runtime apply.|

## Plan48

schema8@0=1; kind8@1(0journal,1bank,2rawjournal,3rawbank); object/bank8@2; copy8@3. base32@4,length32@8,currentSnapshotCRC32@12,token32@16,seed/sourceCRC32@20,seedLength16@24(journal1959;bank/raw0),survivorCopy8@26,survivorFormat8@27,sequence32@28,survivorRawCRC32@32,targetRawCRC32@36,flags32@40. Known DRM formats1–6; known FUN1–6 represented17–22; unknown0. Flag1 indicates known DRM survivor; flag2 historical/unknown survivor. Raw rollback source validation is not a format migration claim.

## Result20 and status32

Result: schema1@0,kind@1,object/bank@2,copy@3,verifiedCommit@4,normalPairReady@5. Journal result: sequence32@8,seedCRC32@12,newPairCRC32@16. Bank/raw result: full targetCRC32@8; other reserved bytes0. Read GET_SAVE_STATUS separately; normalPairReady does not assert that unrelated new runtime edits are durable.

Status: schema1@0,kind@1,target@2,copy@3,active@4,state@5(0prepared,1committed,2failed,3cancelled,4rawstream),error@6,completedErasedSectors@7,rawDriverRC32signed@8,base32@12,length32@16,originalSnapshotCRC32@20,seed/sourceCRC32@24,transferProgress32@28. High bit of progress means raw AUTOSAVE hold; mask with0x7FFFFFFF for offset. The hold survives error, disconnect, CANCEL, expiry and FINISH; only real cold boot or successful explicitly confirmed AUTOSAVE retirement clears it. WAIT is returned while held, never SAVED from RAM equality.

All staged operations expire after120s inactivity without starting a write. During a staged plan, musical edits/upload/restore/SAVE and staging-overwriting backup commands are refused. Read-state hashing uses separate existing scratch, preserving captured seed. Physical panel changes are detected by seed CRC at commit. Full proof/seed/token/source CRC/current pair/runtime/dependencies are rechecked before destructive work. Error clears proof; fresh backup/prepare/review needed. On timeout inspect status/raw bytes; never infer failure means nothing was written.

The browser archive contains complete originals, parsed plan and modern seed with CRC checks; client verifies sector geometry/backup CRCs and requires an acknowledgment plus exact target phrase. A new backup and new confirmation are mandatory for each A/B step. Raw rollback is an explicit owner tool and is never invoked automatically. All real physical operations need separate owner authorization; mock/host/emulator results are not USB hardware verification.
