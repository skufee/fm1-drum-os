import {test,expect} from '@playwright/test';
import {mkdirSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createProject,exportProject,toBase64} from '../src/model.ts';
import {EMBEDDED} from '../src/metadata.ts';
import {crc32,le16,le32} from '../src/protocol.ts';
import {exportLibrary} from '../src/library.ts';
import {installMockMIDI} from './mock-midi.ts';
const artifacts=resolve('../../artifacts/release-1.0-prep/editor');mkdirSync(artifacts,{recursive:true});
const source=(fill=0x22)=>{const bytes=new Uint8Array(256).fill(fill);return {id:55,name:'NEWWAV',rate:22050,frames:512,crc:crc32(bytes),adpcm:toBase64(bytes)};};
function rawSlot(){const raw=new Uint8Array(81920).fill(255),h=new Uint8Array(480),data=new Uint8Array(256).fill(0x33);h.set(le32(0x504d5346));h.set(le16(1),4);h[6]=1;h.set(new TextEncoder().encode('OLDWAV  '),8);h.set(le32(data.length),16);h.set(le32(crc32(data)),20);h.set(le32(512),36);h.set(le32(511),44);h.set(le32(32768),48);h.set(le16(960),52);h[58]=127;raw.set(h);raw.set(data,512);return raw;}
const nav=async(page:any)=>page.getByRole('navigation').getByRole('button',{name:/Samples/}).click();

test('first MIDI connect is one SysEx-permission action and startup auto-connect requires previouslygranted',async({page})=>{
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt'});
 await page.goto('./');await expect.poll(()=>page.evaluate(()=>(window as any).__fm1Mock.requestCount)).toBe(0);
 await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');
 expect(await page.evaluate(()=>(window as any).__fm1Mock.requestCount)).toBe(1);
 await page.screenshot({path:resolve(artifacts,'one-click-connected.png'),fullPage:true});
});
test('granted MIDI permission auto-connects without an extra click and never syncs local work',async({page})=>{
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'granted'});
 await page.goto('./');await expect(page.locator('header')).toContainText('FM1 CONNECTED');expect(await page.evaluate(()=>(window as any).__fm1Mock.requestCount)).toBe(1);
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>[4,6,8,10,12,13,14,15,16,20,21,22,24,25,28,29].includes(c.op)))).toBeFalsy();
});
test('empty upload skipsbackup; occupiedupload downloadscompletebackup and explicitconfirm while retainingnewlocalaudio',async({page})=>{
 const p=createProject();p.samples=[source()];await page.addInitScript(({cache})=>localStorage.setItem('fm1-drum-project',cache),{cache:exportProject(p)});
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt'});
 await page.goto('./');await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');await nav(page);
 await page.locator('[data-ui="userSample"]').selectOption('55');await page.locator('[data-action="upload"]').click();await expect(page.locator('#status')).toContainText('CRC-verified sample committed');
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.filter((c:any)=>c.op===19).length)).toBe(0);
 const original=source().crc;expect(await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project.samples[0].crc)).toBe(original);
 const confirms:string[]=[];page.on('dialog',async d=>{confirms.push(d.message());await d.accept();});
 const [download]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="upload"]').click()]);await download.saveAs(resolve(artifacts,'before-replace-raw.bin'));
 await expect(page.locator('#status')).toContainText('CRC-verified sample committed');expect(readFileSync(resolve(artifacts,'before-replace-raw.bin')).length).toBe(81920);expect(confirms.join(' ')).toContain('Full raw backup downloaded');
 expect(await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project.samples[0].crc)).toBe(original);
 const calls=await page.evaluate(()=>(window as any).__fm1Mock.calls);const starts=calls.map((c:any,i:number)=>c.op===12?i:-1).filter((n:number)=>n>=0);expect(starts).toHaveLength(2);expect(calls[starts[1]].payload[1]).toBe(1);expect(calls.slice(starts[0]+1,starts[1]).filter((c:any)=>c.op===19)).toHaveLength(320);
});
test('occupiedbackup cancellation sends noUPLOAD_BEGIN and leavesnewlocalsource unchanged',async({page})=>{
 const p=createProject();p.samples=[source()];await page.addInitScript(({cache})=>localStorage.setItem('fm1-drum-project',cache),{cache:exportProject(p)});
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt',rawSlots:[{slot:0,raw:Array.from(rawSlot())}]});
 await page.goto('./');await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');await nav(page);await page.locator('[data-ui="userSample"]').selectOption('55');
 page.on('dialog',d=>d.dismiss());await Promise.all([page.waitForEvent('download'),page.locator('[data-action="upload"]').click()]);await expect(page.locator('.progress')).toHaveCount(0);
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===12))).toBeFalsy();expect(await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project.samples[0].crc)).toBe(source().crc);
});
test('offline full library import/export retainsraw16-slotbytes and makes no device write',async({page})=>{
 const banks=Array.from({length:4},()=>new Uint8Array(81920).fill(255));
 const lib={localSources:[source()],deviceSnapshot:{identity:'FM-1_900',firmware:'FM1 TEST',userSlots:16 as const,slots:Array.from({length:16},(_,slot)=>{const raw=slot<4?banks[slot]:banks[slot%4].slice(Math.floor(slot/4)*20480,(Math.floor(slot/4)+1)*20480);return {slot,raw:toBase64(raw),crc32:crc32(raw),metadata:{id:55+slot,flags:0,category:5,name:'USER'+(slot+1),frames:0,rate:0,crc:0,capacity:19968,nz:0}};})}};
 await page.goto('./');await nav(page);page.on('dialog',d=>d.accept());await page.locator('[data-action="library-import"]').click();await page.locator('#library-file').setInputFiles({name:'all.fm1lib.json',mimeType:'application/json',buffer:Buffer.from(exportLibrary(lib))});
 await expect(page.locator('#status')).toContainText('Library imported locally');await expect(page.getByText('1 local sources · 16 device slots in imported backup.',{exact:false})).toBeVisible();
 const [download]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="library-export"]').click()]);const file=resolve(artifacts,'roundtrip-library.fm1lib.json');await download.saveAs(file);expect(JSON.parse(readFileSync(file,'utf8')).library).toEqual(lib);
 await page.screenshot({path:resolve(artifacts,'library-import-export.png'),fullPage:true});
});
test('imported local library restores an empty source through checked uploader and downloads per-slot result',async({page})=>{
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt'});
 await page.goto('./');await nav(page);page.on('dialog',d=>d.accept());await page.locator('[data-action="library-import"]').click();await page.locator('#library-file').setInputFiles({name:'local.fm1lib.json',mimeType:'application/json',buffer:Buffer.from(exportLibrary({localSources:[source()]}))});
 await expect(page.locator('#status')).toContainText('Library imported locally');await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');
 const [download]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="library-restore"]').click()]);const file=resolve(artifacts,'library-restore-results.json');await download.saveAs(file);const result=JSON.parse(readFileSync(file,'utf8'));expect(result.rawBankRestore).toBe(false);expect(result.results).toHaveLength(1);expect(result.results[0]).toMatchObject({id:55,status:'restored'});expect(result.results[0].fingerprint).toBeGreaterThan(0);
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===19))).toBeFalsy();await expect(page.locator('#status')).toContainText('1 restored');
});

test('device snapshot A restore keeps pending local B unbound and blocks mismatched runtime Sync',async({page})=>{
 const p=createProject();p.pads[0].sampleId=55;p.pads[0].ref=0;p.samples=[source()];
 const raw=rawSlot(),fp=crc32(raw.slice(0,480)),banks=Array.from({length:4},(_,i)=>i===0?raw:new Uint8Array(81920).fill(255));
 const lib={localSources:[source()],deviceSnapshot:{identity:'FM-1_900',firmware:'TEST',userSlots:16 as const,slots:Array.from({length:16},(_,slot)=>{const bytes=slot<4?banks[slot]:banks[slot%4].slice(Math.floor(slot/4)*20480,(Math.floor(slot/4)+1)*20480);return {slot,raw:toBase64(bytes),crc32:crc32(bytes),metadata:{id:55+slot,flags:slot===0?2:0,category:5,name:slot===0?'OLDWAV':'EMPTY',frames:slot===0?512:0,rate:slot===0?22050:0,crc:slot===0?fp:0,capacity:19968,nz:slot===0?1:0}};})}};
 await page.addInitScript(({cache})=>localStorage.setItem('fm1-drum-project',cache),{cache:exportProject(p)});
 await page.addInitScript(installMockMIDI,{project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt',rawSlots:[{slot:0,raw:Array.from(raw)}]});
 await page.goto('./');await nav(page);page.on('dialog',d=>d.accept());await page.locator('[data-action="library-import"]').click();await page.locator('#library-file').setInputFiles({name:'two-sources.fm1lib.json',mimeType:'application/json',buffer:Buffer.from(exportLibrary(lib))});
 await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');
 await Promise.all([page.waitForEvent('download'),page.locator('[data-action="library-restore"]').click()]);await expect(page.locator('#status')).toContainText('1 pending local sources kept unbound');
 const stored=await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project);expect(stored.samples[0].crc).toBe(source().crc);expect(stored.pads[0].ref).toBe(0);
 await page.locator('[data-action="sync"]').first().click();await expect(page.locator('[role="alert"]')).toBeVisible();
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===4))).toBe(false);
});
