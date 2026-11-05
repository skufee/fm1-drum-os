import {defineConfig} from '@playwright/test';
import {resolve} from 'node:path';
import base from './playwright.config';
const artifacts=resolve('../../artifacts/release-1.0-prep/editor');
export default defineConfig({...base,testMatch:process.env.FM1_BROWSER_SUITE==='regression'?'**/editor.browser.spec.ts':'**/release.browser.spec.ts',timeout:90000,reporter:[['list'],['json',{outputFile:resolve(artifacts,process.env.FM1_BROWSER_SUITE==='regression'?'browser-regression-results.json':'browser-results.json')}]],outputDir:resolve(artifacts,'browser-output')});
