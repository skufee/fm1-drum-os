import {defineConfig} from '@playwright/test';
import {resolve} from 'node:path';
import base from './playwright.config';
const artifacts=resolve('../../artifacts/corrective-pass/editor');
export default defineConfig({...base,testMatch:'**/corrective.browser.spec.ts',reporter:[['list'],['json',{outputFile:resolve(artifacts,'browser-results.json')}]],outputDir:resolve(artifacts,'browser-output')});
