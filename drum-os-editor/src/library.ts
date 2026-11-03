// SPDX-License-Identifier: GPL-3.0-only
import {createProject,validateProject,clone,toBase64,fromBase64,type LocalSample,type Project} from './model.ts';
import {crc32,u16,u32,ascii} from './protocol.ts';
import type {Device,Sample} from './device.ts';

export interface LibrarySlot {slot:number;raw:string;crc32:number;metadata:Sample}
export interface DeviceLibrary {identity:string;firmware:string;userSlots:4|16;slots:LibrarySlot[]}
export interface Library {localSources:LocalSample[];deviceSnapshot?:DeviceLibrary}
const MAGIC=0x504d5346,CHILD=0x31535244;
const integer=(n:unknown,min:number,max:number):n is number=>typeof n==='number'&&Number.isInteger(n)&&n>=min&&n<=max;
function validRawSource(raw:Uint8Array,slot:number):boolean {
 if(!integer(slot,0,15)||raw.length!==(slot<4?81920:20480)||u32(raw)!==MAGIC||u16(raw,4)!==1||raw[6]!==1)return false;
 const length=u32(raw,16),frames=u32(raw,36),rate=u32(raw,48);
 return length>0&&length<=raw.length-512&&frames>=2&&Math.ceil(frames/2)===length&&u32(raw,32)===0&&u32(raw,40)===0&&u32(raw,44)===frames-1&&raw[56]<=88&&raw[57]<=raw[58]&&raw[59]===0&&rate>0&&rate<=4*65536&&(slot<4||(u32(raw,24)===CHILD&&u32(raw,28)===slot))&&crc32(raw.slice(512,512+length))===u32(raw,20);
}

/** Decode only the validated one-zone representation supported by this editor.
 * Unknown/multizone raw data remains in the archive, never silently converted. */
export function sourceFromRaw(raw:Uint8Array,slot:number):{source:LocalSample;fingerprint:number}|null {
 if(!validRawSource(raw,slot))return null;
 const length=u32(raw,16),frames=u32(raw,36),rate=Math.round(u32(raw,48)*44100/65536);
 if(!length||length>raw.length-512||frames<2||Math.ceil(frames/2)!==length||u32(raw,32)!==0||u32(raw,40)!==0||u32(raw,44)!==frames-1||raw[59]!==0||!integer(rate,4000,44100))return null;
 if(slot>=4&&(u32(raw,24)!==CHILD||u32(raw,28)!==slot))return null;
 const data=raw.slice(512,512+length),dataCRC=crc32(data);
 if(dataCRC!==u32(raw,20))return null;
 const name=ascii(raw.slice(8,16)).trim()||`USER${slot+1}`;
 if(!/^[ -~]{1,8}$/.test(name))return null;
 return {source:{id:55+slot,name,rate,frames,crc:dataCRC,adpcm:toBase64(data)},fingerprint:crc32(raw.slice(0,480))||1};
}

function validateSources(sources:LocalSample[]) {const p=createProject();p.samples=clone(sources);validateProject(p);}
export function validateLibrary(lib:Library):Library {
 if(!lib||!Array.isArray(lib.localSources))throw Error('Invalid sample library');
 validateSources(lib.localSources);
 const d=lib.deviceSnapshot;if(!d)return lib;
 if(typeof d.identity!=='string'||d.identity.length>64||typeof d.firmware!=='string'||d.firmware.length>64||(d.userSlots!==4&&d.userSlots!==16)||!Array.isArray(d.slots)||d.slots.length!==d.userSlots)throw Error('Invalid device library metadata');
 const raws:Uint8Array[]=[];
 for(let i=0;i<d.userSlots;i++){
  const s=d.slots[i],m=s?.metadata;
  if(!s||s.slot!==i||typeof s.raw!=='string'||s.raw.length>110000||!integer(s.crc32,0,0xffffffff)||!m||m.id!==55+i||typeof m.name!=='string'||m.name.length>16||!integer(m.flags,0,7)||!integer(m.category,0,5)||!integer(m.frames,0,162816)||!integer(m.rate,0,176400)||!integer(m.crc,0,0xffffffff)||!integer(m.capacity,0,81408)||!integer(m.nz,0,16))throw Error('Invalid library slot');
  const raw=fromBase64(s.raw);if(raw.length!==(i<4?81920:20480)||crc32(raw)!==s.crc32)throw Error('Library raw slot CRC/length mismatch');
  // Raw backup integrity is independent of sample validity: already corrupt
  // or stale-cached source metadata must not prevent preserving its bytes.
  raws.push(raw);
 }
 // Duplicated child/primary bytes must agree: detects a transfer changing
 // midway instead of presenting a mixed-bank backup as a consistent image.
 if(d.userSlots===16)for(let i=4;i<16;i++){
  const parent=raws[i%4].subarray(Math.floor(i/4)*20480,(Math.floor(i/4)+1)*20480);
  if(parent.some((v,j)=>v!==raws[i][j]))throw Error('Library bank changed during backup');
 }
 return lib;
}
export function exportLibrary(lib:Library):string {
 validateLibrary(lib);const body=JSON.stringify(lib);
 return JSON.stringify({format:'FM1-SAMPLE-LIBRARY',version:1,crc32:crc32(new TextEncoder().encode(body)),library:lib});
}
export function importLibrary(text:string):Library {
 if(text.length>4000000)throw Error('Sample library file too large');
 const x=JSON.parse(text);if(x?.format!=='FM1-SAMPLE-LIBRARY'||x.version!==1||!integer(x.crc32,0,0xffffffff)||crc32(new TextEncoder().encode(JSON.stringify(x.library)))!==x.crc32)throw Error('Sample library format/CRC mismatch');
 return validateLibrary(x.library);
}
export function librarySources(lib:Library,preferLocal=true):LocalSample[] {
 validateLibrary(lib);const result=new Map<number,LocalSample>();
 for(const entry of lib.deviceSnapshot?.slots??[]){if(!(entry.metadata.flags&2))continue;const s=sourceFromRaw(fromBase64(entry.raw),entry.slot);if(s&&s.fingerprint===entry.metadata.crc)result.set(s.source.id,s.source);}
 for(const source of lib.localSources)if(preferLocal||!result.has(source.id))result.set(source.id,clone(source));
 return [...result.values()].sort((a,b)=>a.id-b.id);
}
export async function captureLibrary(device:Device,localSources:LocalSample[],progress=(n:number)=>{}):Promise<Library> {
 const state=await device.state();if(state.playing||state.recording||state.armed)throw Error('STOP and disarm before backing up the sample library');
 const before=await device.sampleList(),count=device.caps?.userSlots;
 if(count!==4&&count!==16)throw Error('Read device capabilities before library backup');
 const slots:LibrarySlot[]=[];
 for(let slot=0;slot<count;slot++){
  const raw=await device.backup(2,slot,n=>progress((slot+n)/count));
  slots.push({slot,raw:toBase64(raw),crc32:crc32(raw),metadata:clone(before[55+slot])});
 }
 const after=await device.sampleList();
 if(JSON.stringify(before.slice(55))!==JSON.stringify(after.slice(55)))throw Error('Device sample library changed during backup; retry while stopped');
 return validateLibrary({localSources:clone(localSources),deviceSnapshot:{identity:device.caps!.identity,firmware:device.caps!.firmware,userSlots:count,slots}});
}

export interface LibraryRestoreResult {id:number;status:'already present'|'restored'|'skipped'|'failed';detail?:string;fingerprint?:number}
const sameSource=(a:LocalSample,b:LocalSample)=>a.id===b.id&&a.name===b.name&&a.rate===b.rate&&a.frames===b.frames&&a.crc===b.crc&&a.adpcm===b.adpcm;
/** Keep a pending local edit distinct from the restored device snapshot. A
 * fingerprint may bind only to the source whose bytes/metadata were restored. */
export function bindLibraryRestore(project:Project,lib:Library,results:LibraryRestoreResult[]):number {
 const restored=new Map(librarySources(lib,false).map(s=>[s.id,s]));let pending=0;
 for(const r of results){if(!r.fingerprint||(r.status!=='restored'&&r.status!=='already present'))continue;
  const source=restored.get(r.id);if(!source)continue;
  const local=project.samples.find(s=>s.id===r.id),conflict=!!local&&!sameSource(local,source);
  if(conflict)pending++;else if(!local)project.samples.push(clone(source));
  project.pads.forEach(p=>{if(p.sampleId===r.id)p.ref=conflict?0:r.fingerprint!;});
  project.users.forEach(k=>k?.pads.forEach(p=>{if(p.sampleId===r.id)p.ref=conflict?0:r.fingerprint!;}));
 }
 return pending;
}
/** Guarded source restore, never a raw-bank writer. Dependencies/protection
 * can refuse a slot; that refusal is reported without destructive cleanup. */
export async function restoreLibrary(device:Device,lib:Library,confirmReplace:(id:number,backup:Uint8Array)=>Promise<boolean>,progress=(n:number)=>{}):Promise<LibraryRestoreResult[]> {
 validateLibrary(lib);const state=await device.state();if(state.playing||state.recording||state.armed)throw Error('STOP and disarm before restoring a sample library');
 if(lib.deviceSnapshot&&device.caps?.identity&&lib.deviceSnapshot.identity!==device.caps.identity)throw Error('Library device family differs; import locally without writing the device');
 const sources=librarySources(lib,false),results:LibraryRestoreResult[]=[],ids=new Set(sources.map(s=>s.id));
 for(const entry of lib.deviceSnapshot?.slots??[]){if(ids.has(55+entry.slot))continue;const raw=fromBase64(entry.raw);if(raw.subarray(0,entry.slot<4?20480:raw.length).some(v=>v!==255))results.push({id:55+entry.slot,status:'skipped',detail:'Raw backup retained only: unknown, corrupt, multizone or unsupported source representation'});}
 for(let i=0;i<sources.length;i++){
  const source=sources[i];try{
   const list=await device.sampleList(),current=list.find(s=>s.id===source.id);
   if(!current)throw Error('Slot is outside connected device capabilities');
   const archived=lib.deviceSnapshot?.slots[source.id-55];
   const archivedSource=archived?sourceFromRaw(fromBase64(archived.raw),archived.slot):null;
   if(archivedSource&&sameSource(source,archivedSource.source)&&(current.flags&2)&&current.crc===archivedSource.fingerprint&&current.crc===archived!.metadata.crc){results.push({id:source.id,status:'already present',fingerprint:current.crc});continue;}
   if(device.caps?.userSlots===16&&(current.flags&4))throw Error('Protected/overlapping/unknown slot');
   if(fromBase64(source.adpcm).length>device.sampleCapacity(source.id))throw Error('Archived source exceeds current slot capacity; raw backup retained');
   const replace=!!(current.flags&2);
   if(replace){const raw=await device.backup(2,source.id-55);if(!await confirmReplace(source.id,raw)){results.push({id:source.id,status:'skipped',detail:'Replacement not confirmed'});continue;}}
   const fingerprint=await device.upload(source,replace,n=>progress((i+n)/Math.max(1,sources.length)));
   results.push({id:source.id,status:'restored',fingerprint});
  }catch(e){results.push({id:source.id,status:'failed',detail:e instanceof Error?e.message:String(e)});}
  finally{progress((i+1)/Math.max(1,sources.length));}
 }
 return results;
}
