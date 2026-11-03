// SPDX-License-Identifier: GPL-3.0-only
// Explicit retirement of archived data, never a claimed unknown-format migration.
import {crc32,u16,u32,i32} from './protocol.ts';
import {toBase64} from './model.ts';
import {SAMPLE_BANKS} from './nor-diagnostics.ts';
export interface RetirementPlan {schema:1;kind:0|1;target:number;copy:number;base:number;length:number;snapshotCRC:number;token:number;seedCRC:number;seedLength:number;survivorCopy:number;survivorFormat:number;survivorSequence:number;survivorCRC:number;targetCRC:number;flags:number}
export interface PreparedRetirement {plan:RetirementPlan;raw:Uint8Array;seed:Uint8Array}
export interface RetirementStatus {kind:number;target:number;copy:number;active:boolean;state:number;error:number;completedSectors:number;raw:number;base:number;length:number;snapshotCRC:number;seedCRC:number;transferOffset:number;autosaveHold:boolean}
export interface RetirementResult {kind:number;target:number;copy:number;committed:true;normalReady:boolean;sequenceOrBankCRC:number;seedCRC:number;pairCRC:number}
const journal=(n:number)=>Number.isInteger(n)&&(n===7||n>=1&&n<=4);
export function parseRetirementPlan(b:Uint8Array,kind:0|1,target:number):RetirementPlan {
 if(b.length!==48||b[0]!==1||b[1]!==kind||b[2]!==target||!u32(b,16))throw Error('Invalid retirement plan');
 const copy=b[3],base=u32(b,4),length=u32(b,8),seedLength=u16(b,24),survivorCopy=b[26];
 if(kind===0){const a=target===7?0x9f000:0x97000+(target-1)*8192,z=target===7?0xfe000:a+4096;if(!journal(target)||copy>1||survivorCopy!==(copy^1)||base!==(copy?z:a)||length!==4096||seedLength!==1959||![0,1,2,3,4,5,6,17,18,19,20,21,22].includes(b[27]))throw Error('Retirement journal address/survivor/seed mismatch');}
 else if(!Number.isInteger(target)||target<0||target>3||copy!==255||survivorCopy!==255||base!==SAMPLE_BANKS[target]||length!==81920||seedLength||u32(b,20))throw Error('Retirement bank address/length mismatch');
 return {schema:1,kind,target,copy,base,length,snapshotCRC:u32(b,12),token:u32(b,16),seedCRC:u32(b,20),seedLength,survivorCopy,survivorFormat:b[27],survivorSequence:u32(b,28),survivorCRC:u32(b,32),targetCRC:u32(b,36),flags:u32(b,40)};
}
export function retirementPhrase(p:RetirementPlan):string {return p.kind===1?`RETIRE BANK ${p.target+1}`:`RETIRE ${p.target===7?'AUTOSAVE':'PROJECT '+p.target} ${p.copy?'B':'A'}`;}
export function parseRetirementResult(b:Uint8Array,p:RetirementPlan):RetirementResult {
 if(b.length!==20||b[0]!==1||b[1]!==p.kind||b[2]!==p.target||b[3]!==p.copy||b[4]!==1||b[5]>1||p.kind===0&&u32(b,12)!==p.seedCRC||p.kind===1&&(!b[5]||u32(b,8)!==0xc08cbd67))throw Error('Retirement result does not verify the prepared step');
 return {kind:b[1],target:b[2],copy:b[3],committed:true,normalReady:!!b[5],sequenceOrBankCRC:u32(b,8),seedCRC:u32(b,12),pairCRC:u32(b,16)};
}
export function parseRetirementStatus(b:Uint8Array):RetirementStatus {
 if(b.length!==32||b[0]!==1||b[1]>3||b[4]>1||b[5]>4||(b[5]===4&&b[1]<2))throw Error('Retirement status size/schema');
 return {kind:b[1],target:b[2],copy:b[3],active:!!b[4],state:b[5],error:b[6],completedSectors:b[7],raw:i32(b,8),base:u32(b,12),length:u32(b,16),snapshotCRC:u32(b,20),seedCRC:u32(b,24),transferOffset:u32(b,28)&0x7fffffff,autosaveHold:!!(u32(b,28)&0x80000000)};
}
export function exportRetirementBackup(p:PreparedRetirement):string {
 const body={plan:p.plan,rawEncoding:'base64',raw:toBase64(p.raw),rawCRC32:crc32(p.raw),seed:toBase64(p.seed),seedCRC32:crc32(p.seed),meaning:'Historical target bytes cease to be active only after a separately confirmed one-step retirement. Original raw bytes stay in this archive; restoration requires a separate approved recovery procedure. Journal seed is the captured CURRENT DEVICE RUNTIME, not a decoded FUNB migration.'};
 return JSON.stringify({format:'FM1-STORAGE-RETIREMENT-BACKUP',version:1,crc32:crc32(new TextEncoder().encode(JSON.stringify(body))),backup:body},null,2);
}
