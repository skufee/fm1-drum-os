import {defineConfig} from '@playwright/test';
import {resolve} from 'node:path';
import base from './playwright.config';
const artifacts=resolve('../../artifacts/storage-recovery/editor');
export default defineConfig({...base,testMatch:'**/retirement.browser.spec.ts',timeout:120000,reporter:[['list'],['json',{outputFile:resolve(artifacts,'browser-results.json')}]],outputDir:resolve(artifacts,'browser-output')});
