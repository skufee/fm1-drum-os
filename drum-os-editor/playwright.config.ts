import {defineConfig} from '@playwright/test';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
const artifacts=resolve(dirname(fileURLToPath(import.meta.url)),'../../artifacts/release-1.0-prep/editor');
export default defineConfig({testDir:'tests',testMatch:'**/*.browser.spec.ts',timeout:45000,expect:{timeout:10000},fullyParallel:false,workers:1,retries:0,reporter:[['list'],['json',{outputFile:resolve(artifacts,'browser-results.json')}]],outputDir:resolve(artifacts,'browser-output'),use:{baseURL:'http://127.0.0.1:4179/fm1-editor/',headless:true,channel:process.platform==='win32'?'chrome':undefined,viewport:{width:1440,height:1050},trace:'retain-on-failure'},webServer:{command:'node tests/serve-dist.mjs',url:'http://127.0.0.1:4179/fm1-editor/',reuseExistingServer:false,timeout:30000}});
