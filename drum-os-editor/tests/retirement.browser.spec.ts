import {test,expect} from '@playwright/test';
import {mkdirSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createProject,fromBase64} from '../src/model.ts';
import {EMBEDDED} from '../src/metadata.ts';
import {crc32} from '../src/protocol.ts';
import {retirementPair,retirementSeed} from './retirement-fixtures.ts';
import {sampleBanks} from './nor-fixtures.ts';
import {installMockMIDI} from './mock-midi.ts';
const artifacts=resolve('test-results/evidence');mkdirSync(artifacts,{recursive:true});
const base={project:createProject(),embedded:[...EMBEDDED],userSlots:16,capacity:19968,permission:'prompt',norDiagnostics:true,retirement:true,retirementRawPair:Array.from(retirementPair()),retirementSeed:Array.from(retirementSeed())};
const connect=async(page:any)=>{await page.goto('./');await page.locator('[data-action="connect"]').first().click();await expect(page.locator('header')).toContainText('FM1 CONNECTED');};
const commits=()=>{return (window as any).__fm1Mock.calls.filter((c:any)=>c.op===30&&[2,4].includes(c.payload[0]));};

test('journal retirement downloads complete raw/seed archive, shows one exact range, and requires typed separate confirmation',async({page})=>{
 await page.addInitScript(installMockMIDI,base);await connect(page);expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===30))).toBe(false);
 const [dl]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="retire-prepare-journal"]').click()]);const file=resolve(artifacts,'synthetic-retirement-plan.json');await dl.saveAs(file);const archive=JSON.parse(readFileSync(file,'utf8'));expect(fromBase64(archive.backup.raw)).toEqual(retirementPair());expect(fromBase64(archive.backup.seed)).toEqual(retirementSeed());expect(archive.backup.rawCRC32).toBe(crc32(retirementPair()));expect(archive.backup.seedCRC32).toBe(crc32(retirementSeed()));
 await expect(page.getByText(/Target: 0x9F000–0x9FFFF/)).toBeVisible();await expect(page.getByText(/Preserved on-chip survivor: copy B/)).toBeVisible();await expect(page.getByText(/current device runtime/).first()).toBeVisible();await expect(page.locator('[data-action="retire-commit"]')).toBeDisabled();
 await page.locator('[data-ui="retirementAcknowledged"]').check();await page.getByLabel('Exact retirement confirmation').fill('YES');await expect(page.locator('[data-action="retire-commit"]')).toBeDisabled();expect(await page.evaluate(commits)).toHaveLength(0);
 await page.getByLabel('Exact retirement confirmation').fill('RETIRE AUTOSAVE A');await expect(page.locator('[data-action="retire-commit"]')).toBeEnabled();await page.screenshot({path:resolve(artifacts,'one-copy-review.png'),fullPage:true});await page.locator('[data-action="retire-commit"]').click();await expect(page.getByText(/One verified step completed/)).toBeVisible();
 expect(await page.evaluate(commits)).toHaveLength(1);await expect(page.getByText(/other historical copy remains protected/).first()).toBeVisible();await expect(page.locator('[data-action="retire-commit"]')).toHaveCount(0);await page.waitForTimeout(1600);expect(await page.evaluate(commits)).toHaveLength(1);expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.filter((c:any)=>c.op===30&&c.payload[0]===0).length)).toBe(1);
});

test('cancel after backup consumes RAM plan without any write, and old firmware cannot prepare retirement',async({page})=>{
 await page.addInitScript(installMockMIDI,base);await connect(page);await Promise.all([page.waitForEvent('download'),page.locator('[data-action="retire-prepare-journal"]').click()]);await page.locator('[data-action="retire-cancel"]').click();await expect(page.getByText('RAM proof cancelled. Previous NOR writes are not undone; read status and backups.')).toBeVisible();expect(await page.evaluate(commits)).toHaveLength(0);await expect(page.locator('[data-action="retire-commit"]')).toHaveCount(0);
 await page.close();
});

test('unsupported capability never sends retirement probes on connect or enables preparation',async({page})=>{
 await page.addInitScript(installMockMIDI,{...base,retirement:false});await connect(page);await expect(page.locator('[data-action="retire-prepare-journal"]')).toBeDisabled();await expect(page.locator('[data-action="retire-prepare-bank"]')).toBeDisabled();expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===30))).toBe(false);
});

test('seed CRC failure and bank dependency NACK never become a prepared destructive action',async({page})=>{
 await page.addInitScript(installMockMIDI,{...base,retirementBadSeed:true,retirementDependencies:true,rawSlots:[{slot:0,raw:Array.from(sampleBanks()[0])}]});await connect(page);await page.locator('[data-action="retire-prepare-journal"]').click();await expect(page.getByRole('alert')).toContainText('CRC');await expect(page.locator('[data-action="retire-commit"]')).toHaveCount(0);expect(await page.evaluate(commits)).toHaveLength(0);await page.locator('[data-action="retire-prepare-bank"]').click();await expect(page.getByRole('alert')).toContainText('dependencies');expect(await page.evaluate(commits)).toHaveLength(0);
});

test('bank retirement is a separately confirmed80KiB target and refreshes freed slot state',async({page})=>{
 const banks=sampleBanks();await page.addInitScript(installMockMIDI,{...base,rawSlots:banks.map((raw,slot)=>({slot,raw:Array.from(raw)})),protectedIds:[55,59,63,67]});await connect(page);
 const [dl]=await Promise.all([page.waitForEvent('download'),page.locator('[data-action="retire-prepare-bank"]').click()]);const file=resolve(artifacts,'synthetic-bank-retirement.json');await dl.saveAs(file);const archive=JSON.parse(readFileSync(file,'utf8'));expect(fromBase64(archive.backup.raw)).toEqual(banks[0]);expect(archive.backup.plan.base).toBe(0xa0000);expect(archive.backup.plan.length).toBe(81920);await expect(page.getByText(/Target: 0xA0000–0xB3FFF/)).toBeVisible();
 await page.locator('[data-ui="retirementAcknowledged"]').check();await page.getByLabel('Exact retirement confirmation').fill('RETIRE BANK 1');await page.locator('[data-action="retire-commit"]').click();await expect(page.getByText(/One verified step completed/)).toBeVisible();expect(await page.evaluate(commits)).toHaveLength(1);await page.getByRole('navigation').getByRole('button',{name:/Samples/}).click();await page.locator('[data-ui="userSample"]').selectOption('55');await expect(page.locator('[data-action="upload"]')).toBeEnabled();
});

test('already erased bank is not mislabeled as requiring retirement and sends no prepare/erase',async({page})=>{await page.addInitScript(installMockMIDI,base);await connect(page);await page.locator('[data-ui="retirementBank"]').selectOption('1');await page.locator('[data-action="retire-prepare-bank"]').click();await expect(page.getByRole('alert')).toContainText('already erased');await expect(page.getByRole('alert')).toContainText('persisted references');expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.some((c:any)=>c.op===30&&[3,4].includes(c.payload[0])))).toBe(false);});


test('failed commit leaves an explicit RAM-cancel path after local proof is consumed, with no write retry',async({page})=>{
 await page.addInitScript(installMockMIDI,{...base,retirementCommitBusy:true});await connect(page);
 await Promise.all([page.waitForEvent('download'),page.locator('[data-action="retire-prepare-journal"]').click()]);
 await page.locator('[data-ui="retirementAcknowledged"]').check();await page.getByLabel('Exact retirement confirmation').fill('RETIRE AUTOSAVE A');await page.locator('[data-action="retire-commit"]').click();
 await expect(page.getByRole('alert')).toBeVisible();await expect(page.locator('[data-action="retire-commit"]')).toHaveCount(0);expect(await page.evaluate(commits)).toHaveLength(1);
 await expect(page.locator('[data-action="retire-cancel"]')).toBeEnabled();await page.locator('[data-action="retire-cancel"]').click();
 await expect(page.getByText('RAM proof cancelled. Previous NOR writes are not undone; read status and backups.')).toBeVisible();
 expect(await page.evaluate(()=>(window as any).__fm1Mock.calls.filter((c:any)=>c.op===30&&c.payload[0]===6).length)).toBe(1);expect(await page.evaluate(commits)).toHaveLength(1);
 await Promise.all([page.waitForEvent('download'),page.locator('[data-action="retire-prepare-journal"]').click()]);await expect(page.getByLabel('Exact retirement confirmation')).toBeVisible();expect(await page.evaluate(commits)).toHaveLength(1);
});
