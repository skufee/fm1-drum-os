import {test,expect} from '@playwright/test';

test('default editor hides recovery tools while retaining samples and projects', async ({page})=>{
 await page.goto('./');
 for(const text of ['Owner-confirmed storage retirement','Read-only NOR diagnosis','Storage diagnosis & proof-gated recovery'])
  await expect(page.getByText(text,{exact:false})).toHaveCount(0);
 await page.getByRole('navigation').getByRole('button',{name:/Samples/}).click();
 await expect(page.getByRole('heading',{name:/User sample slot/i})).toBeVisible();
 await expect(page.getByRole('heading',{name:'Sample library backup'})).toBeVisible();
 for(const action of ['download-sample','nor-sample-report'])await expect(page.locator(`[data-action="${action}"]`)).toHaveCount(0);
 await page.getByRole('navigation').getByRole('button',{name:/Projects/}).click();
 await expect(page.getByText('Four independent device slots')).toBeVisible();
 await expect(page.getByText('Working project / device')).toHaveCount(0);
 await expect(page.getByText('Restore checked DRM backup')).toHaveCount(0);
});

test('service mode retains recovery tooling without weakening firmware checks',async({page})=>{
 await page.goto('./?service=1');
 await expect(page.getByText('Read-only NOR diagnosis')).toBeVisible();
 await expect(page.getByText('Storage diagnosis & proof-gated recovery')).toBeVisible();
 await expect(page.locator('[data-action="retire-prepare-journal"]')).toBeDisabled();
});
