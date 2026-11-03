// SPDX-License-Identifier: GPL-3.0-only
// Read-only application diagnosis. Unknown signatures never authorize a write.
import {u32,crc32} from './protocol.ts';

export const SAMPLE_BANKS=[0xa0000,0xb4000,0xc8000,0xe7000] as const;
export const hex=(n:number)=>'0x'+(n>>>0).toString(16).toUpperCase().padStart(5,'0');
const bytesHex=(b:Uint8Array)=>Array.from(b,x=>x.toString(16).padStart(2,'0')).join(' ');
const validObject=(obj:number)=>Number.isInteger(obj)&&(obj===7||obj>=1&&obj<=4);
export const copyStateName=(state:number)=>['ERASED','VALID','CORRUPT','UNKNOWN / READ FAILED'][state]??'UNKNOWN';
export function projectFormatName(format:number):string {return format===0xfe?'ERASED':format>=1&&format<=6?'DRM'+format:format>=0x11&&format<=0x16?'FUN'+(format-0x10)+' legacy / protected':'UNKNOWN PAYLOAD';}
export const PROJECT_REASONS=['Recognized DRM record','Erased sector','Read failed; format unknown','Outer FELU header invalid','Payload CRC invalid','Valid outer FELU record; payload is not a recognized DRM record','Recognized legacy FUN; protected against implicit conversion','DRM signature exists but complete record validation failed'] as const;
export const SAMPLE_FORMATS=['UNKNOWN','ERASED','FSMP one-zone','FSMP multizone / unsupported','FSMP unsupported or invalid','SLG2 signature only / schema unknown','FUP1 interrupted upload'] as const;
export const SAMPLE_REASONS=['Validated playable source','Erased logical slice','Parent bank is unknown or unsupported; children remain protected','Overlaps the verified parent source','Unknown nonempty header','Unsupported FSMP version','Multizone FSMP is not a drum one-zone source','FSMP bounds/zone invalid','Payload CRC invalid','Child DRS1 tag or slot identity invalid','Authenticated interrupted upload','Read failed; format unknown','Looped FSMP is not supported by drum playback'] as const;
export interface ProjectCopyDiagnosis {copy:number;state:number;stateName:string;format:number;formatName:string;ownedDRM:boolean;reasonCode:number;reason:string;offset:number;length:number;sequence:number;payloadCRC32:number;rawCRC32:number;rawHeaderHex:string;payloadPrefixHex:string;payloadMagic:number}
export interface ProjectNorDiagnosis {schema:number;readOnly:true;object:number;totalBytes:number;pairCRC32:number;selectedCopy:number|null;readOK:boolean;repairable:boolean;copies:ProjectCopyDiagnosis[]}
export interface SampleNorDiagnosis {schema:number;readOnly:true;slot:number;bank:number;offset:number;length:number;backupLength:number;bankOffset:number;bankLength:number;logicalCRC32:number;bankCRC32:number;declaredDataLength:number;declaredPayloadCRC32:number;actualPayloadCRC32:number;validParentExtent:number;cachedClass:number;format:number;formatName:string;reasonCode:number;reason:string;access:boolean;ready:boolean;protected:boolean;payloadCRCChecked:boolean;liveLaneMask:number;userKitMask:number;persistedDependencies:number;dependencyReason:string;rawHeaderHex:string;parentHeaderHex:string;fingerprint:number;firstNonFF:number|null}
export function parseProjectDiagnosis(b:Uint8Array,object:number):ProjectNorDiagnosis {
 if(!validObject(object)||b.length!==152||b[0]!==1||b[1]!==0||b[2]!==object||b[3]!==2||u32(b,4)!==8192||![0,1,255].includes(b[20])||b[21]>1||b[22]>1)throw Error('Invalid NOR project diagnostic reply');
 const base=object===7?0x9f000:0x97000+(object-1)*8192,second=object===7?0xfe000:base+4096;
 if(u32(b,8)!==base||u32(b,12)!==second)throw Error('NOR project addresses differ from the protected layout');
 const copies=Array.from({length:2},(_,copy)=>{const o=24+copy*64,state=b[o],format=b[o+1],reasonCode=b[o+3];if(state>3||b[o+2]>1||reasonCode>=PROJECT_REASONS.length||![0,1,2,3,4,5,6,17,18,19,20,21,22,254].includes(format)||u32(b,o+4)!==(copy?second:base))throw Error('Invalid NOR copy classification');return {copy,state,stateName:copyStateName(state),format,formatName:format===0&&u32(b,o+56)===0x46554e42?'UNKNOWN PAYLOAD (FUNB signature / schema unknown)':projectFormatName(format),payloadMagic:u32(b,o+56),ownedDRM:!!b[o+2],reasonCode,reason:PROJECT_REASONS[reasonCode],offset:u32(b,o+4),length:u32(b,o+8),sequence:u32(b,o+12),payloadCRC32:u32(b,o+16),rawCRC32:u32(b,o+20),rawHeaderHex:bytesHex(b.slice(o+24,o+56)),payloadPrefixHex:bytesHex(b.slice(o+56,o+64))};});
 return {schema:1,readOnly:true,object,totalBytes:8192,pairCRC32:u32(b,16),selectedCopy:b[20]===255?null:b[20],readOK:!!b[21],repairable:!!b[22],copies};
}
export function parseSampleDiagnosis(b:Uint8Array,slot:number):SampleNorDiagnosis {
 if(!Number.isInteger(slot)||slot<0||slot>=16||b.length!==160||b[0]!==1||b[1]!==1||b[2]!==slot||b[3]!==slot%4||u32(b,4)!==SAMPLE_BANKS[slot%4]+Math.floor(slot/4)*20480||u32(b,8)!==20480||u32(b,12)!==(slot<4?81920:20480)||u32(b,16)!==SAMPLE_BANKS[slot%4]||u32(b,20)!==81920||b[48]>3||b[49]>=SAMPLE_FORMATS.length||b[50]>=SAMPLE_REASONS.length||b[51]>15||![0,1,255].includes(b[54])||(u32(b,156)!==0xffffffff&&u32(b,156)>=20480))throw Error('Invalid NOR sample diagnostic reply');
 const deps=b[54]===255?-1:b[54];
 return {schema:1,readOnly:true,slot,bank:b[3],offset:u32(b,4),length:u32(b,8),backupLength:u32(b,12),bankOffset:u32(b,16),bankLength:u32(b,20),logicalCRC32:u32(b,24),bankCRC32:u32(b,28),declaredDataLength:u32(b,32),declaredPayloadCRC32:u32(b,36),actualPayloadCRC32:u32(b,40),validParentExtent:u32(b,44),cachedClass:b[48],format:b[49],formatName:SAMPLE_FORMATS[b[49]],reasonCode:b[50],reason:SAMPLE_REASONS[b[50]],access:!!(b[51]&1),ready:!!(b[51]&2),protected:!!(b[51]&4),payloadCRCChecked:!!(b[51]&8),liveLaneMask:b[52],userKitMask:b[53],persistedDependencies:deps,dependencyReason:deps<0?'Cannot prove absence of references: a persisted project/AUTOSAVE copy is legacy, unknown, corrupt or unreadable':deps?'Referenced by a persisted project or rollback copy':'No persisted references found',rawHeaderHex:bytesHex(b.slice(56,120)),parentHeaderHex:bytesHex(b.slice(120,152)),fingerprint:u32(b,152),firstNonFF:u32(b,156)===0xffffffff?null:u32(b,156)};
}
export function verifyProjectBackup(report:ProjectNorDiagnosis,raw:Uint8Array):void {
 if(raw.length!==8192||crc32(raw)!==report.pairCRC32||report.copies.some((c,i)=>crc32(raw.slice(i*4096,(i+1)*4096))!==c.rawCRC32))throw Error('Raw AUTOSAVE/project backup CRC mismatch; nothing was authorized');
}
export function verifySampleBank(report:SampleNorDiagnosis,raw:Uint8Array):void {
 if(raw.length!==81920||crc32(raw)!==report.bankCRC32)throw Error('Raw sample bank CRC mismatch');
 const within=report.offset-report.bankOffset;if(crc32(raw.slice(within,within+report.length))!==report.logicalCRC32)throw Error('Logical sample slice CRC mismatch');
}
export function saveProtectionReason(s:{code:number;object:number;copy:number;stage:number;raw:number},report:ProjectNorDiagnosis|null=null):string {
 if(s.code!==3)return '';
 const copy=report?.object===s.object?report.copies.find(c=>c.copy===s.copy):null;
 if(copy)return copy.reason+' ('+copy.formatName+', '+hex(copy.offset)+'). Original bytes stay protected.';
 if(s.object===7&&s.copy===0&&s.stage===5&&s.raw===0)return 'AUTOSAVE A has a valid outer FELU record, but its payload is not accepted as DRM1–DRM6. Read the raw A/B report to identify the payload; no format or safe erase is assumed.';
 return 'Destination is protected. Read both raw copies to distinguish legacy data, CRC damage and unknown content. No automatic erase or migration.';
}
