import {test,expect} from '@playwright/test';
import {mkdirSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createProject,fromBase64} from '../src/model.ts';
import {EMBEDDED} from '../src/metadata.ts';
import {crc32,le32} from '../src/protocol.ts';
import {wavPCM16} from '../src/audio.ts';
import {projectRaw,projectReport,sampleBanks,sampleReport} from './nor-fixtures.ts';
import {installMockMIDI} from './mock-midi.ts';
const artifacts=resolve('../../artifacts/physical-nor/editor');mkdirSync(artifacts,{recursive:true});
const noSave=()=>{const b=new Uint8Array(20);b.set([2,3,7,0,5]);b.set(le32(0x856014),16);return Array.from(b);};
const base={project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt'};
const connect=async(page:any)=>{await page.goto('./');await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');};
const writes=()=>{const m=(window as any).__fm1Mock;return m.calls.filter((c:any)=>[4,6,8,10,12,13,14,15,16,20,21,22,24,25,28,29].includes(c.op)||c.op===27&&c.payload[0]<3);};

test('NO SAVE shows proved valid-outer cause and explicit read-only full A/B archive has CRC, with separate raw files',async({page})=>{
 const raw=projectRaw();await page.addInitScript(installMockMIDI,{...base,norDiagnostics:true,saveDiagnostic:noSave(),norProjectRaw:Array.from(raw),norProjectReport:Array.from(projectReport(raw))});await connect(page);
 await expect(page.getByText(/AUTOSAVE A has a valid outer FELU record/)).toBeVisible();expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===27))).toBeFalsy();
 await page.locator('[data-action="nor-report"]').click();await expect(page.getByText('Copy A / 0x9F000 / VALID / UNKNOWN PAYLOAD')).toBeVisible();await expect(page.getByText('Copy B / 0xFE000 / ERASED / ERASED')).toBeVisible();
 const [download]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="nor-autosave-backup"]').click()]);const file=resolve(artifacts,'synthetic-autosave-backup.json');await download.saveAs(file);const archive=JSON.parse(readFileSync(file,'utf8'));expect(archive.readOnly).toBe(true);expect(archive.automaticMigration).toBe(false);expect(fromBase64(archive.rawPair)).toEqual(raw);expect(archive.autosave.pairCRC32).toBe(crc32(raw));
 const [a]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="nor-copy-download"][data-i="0"]').click()]);const afile=resolve(artifacts,'synthetic-autosave-A.bin');await a.saveAs(afile);expect(readFileSync(afile)).toEqual(Buffer.from(raw.slice(0,4096)));expect(await page.evaluate(writes)).toEqual([]);await page.screenshot({path:resolve(artifacts,'readonly-autosave.png'),fullPage:true});
});

test('unknown sample banks export losslessly with16slot reasons and indeterminate persisted references, no writes',async({page})=>{
 const banks=sampleBanks();await page.addInitScript(installMockMIDI,{...base,norDiagnostics:true,norSampleReports:Array.from({length:16},(_,s)=>Array.from(sampleReport(s,banks))),rawSlots:banks.map((raw,slot)=>({slot,raw:Array.from(raw)})),protectedIds:[55,58,59,62,63,66,67,70]});await connect(page);
 const [download]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="nor-sample-backup"]').click()]);const file=resolve(artifacts,'synthetic-sample-banks-backup.json');await download.saveAs(file);const archive=JSON.parse(readFileSync(file,'utf8'));expect(archive.samples).toHaveLength(16);expect(archive.banks).toHaveLength(4);expect(archive.samples.filter((r:any)=>r.protected).map((r:any)=>r.slot+1)).toEqual([1,4,5,8,9,12,13,16]);archive.banks.forEach((b:any,i:number)=>{expect(fromBase64(b.raw)).toEqual(banks[i]);expect(b.crc32).toBe(crc32(banks[i]));});
 await expect(page.getByText('SLG2 signature only / schema unknown',{exact:true})).toBeVisible();await page.getByRole('navigation').getByRole('button',{name:/Samples/}).click();await page.locator('[data-ui="userSample"]').selectOption('59');await expect(page.getByText(/Parent bank is unknown or unsupported/)).toBeVisible();await expect(page.getByText(/Cannot prove absence of references/)).toBeVisible();await expect(page.locator('[data-action="upload"]')).toBeDisabled();expect(await page.evaluate(writes)).toEqual([]);await page.screenshot({path:resolve(artifacts,'readonly-sample-protection.png'),fullPage:true});
});

test('old firmware disables unsupported read-only probes and corrupted raw export remains an error',async({page})=>{
 await page.addInitScript(installMockMIDI,base);await connect(page);await expect(page.locator('[data-action="nor-report"]')).toBeDisabled();await expect(page.locator('[data-action="nor-autosave-backup"]')).toBeDisabled();expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===27))).toBe(false);expect(await page.evaluate(writes)).toEqual([]);
});

test('Auto-fit remains local for quality trim and duration resample, respects16slot capacity',async({page})=>{
 const pcm=Float32Array.from({length:44100*4},(_,i)=>.3*Math.sin(i*.1)),wav=wavPCM16(pcm,44100);await page.goto('./');await page.getByRole('navigation').getByRole('button',{name:/Samples/}).click();await page.locator('[data-ui="userSample"]').selectOption('55');await page.locator('#wav-file').setInputFiles({name:'four-seconds.wav',mimeType:'audio/wav',buffer:Buffer.from(wav)});await expect(page.locator('[data-action="auto-fit-quality"]')).toBeVisible();await page.locator('[data-action="auto-fit-quality"]').click();await expect(page.locator('#status')).toContainText('Device not modified');let sample=await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project.samples[0]);expect(sample.rate).toBe(22050);expect(sample.frames).toBeLessThanOrEqual(39936);expect(fromBase64(sample.adpcm).length).toBeLessThanOrEqual(19968);expect(crc32(fromBase64(sample.adpcm))).toBe(sample.crc);
 await page.locator('[data-ui="trimEnd"]').fill('4');await page.locator('[data-ui="trimEnd"]').dispatchEvent('change');await page.locator('[data-action="auto-fit-duration"]').click();sample=await page.evaluate(()=>JSON.parse(localStorage.getItem('fm1-drum-project')!).project.samples[0]);expect(sample.rate).toBe(8000);expect(sample.frames).toBe(32000);expect(fromBase64(sample.adpcm).length).toBeLessThanOrEqual(19968);await expect(page.locator('[data-ui="userSample"] option')).toHaveCount(16);
});
