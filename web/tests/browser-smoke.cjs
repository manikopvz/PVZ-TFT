const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require('playwright');

async function main() {
  const root = path.resolve(process.argv[2] || 'build-web/dist');
  const errors = [];
  const mime = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.data':'application/octet-stream','.json':'application/json'};
  const server = http.createServer((req,res) => {
    let pathname;
    try { pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname); } catch {res.writeHead(400).end();return;}
    if(!pathname.startsWith('/PVZ-TFT/')) {res.writeHead(404).end();return;}
    const file=path.resolve(root,pathname.slice('/PVZ-TFT/'.length)||'index.html');
    if(!file.startsWith(root+path.sep)) {res.writeHead(403).end();return;}
    if(!fs.existsSync(file) || !fs.statSync(file).isFile()) {res.writeHead(404).end();return;}
    res.setHeader('Content-Type',mime[path.extname(file)]||'application/octet-stream');fs.createReadStream(file).pipe(res);
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader',...(process.env.PVZ_BROWSER_SINGLE_PROCESS?['--single-process','--no-zygote']:[])]});
    const context=await browser.newContext({viewport:{width:1360,height:1000}});
    const page=await context.newPage();
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    page.on('response',response=>{if(response.status()>=400 && !response.url().endsWith('favicon.ico'))errors.push(`${response.status()} ${response.url()}`);});
    const url=`http://127.0.0.1:${server.address().port}/PVZ-TFT/`;
    await page.goto(url);
    await page.locator('#start:enabled').waitFor({timeout:120000});
    await page.locator('#start').click();
    try {
      await page.waitForFunction(()=>window.Module && Module._pvz_web_ready && Module._pvz_web_ready()===1,null,{timeout:120000});
    } catch(error) {console.error(await page.locator('#log').textContent());throw error;}
    if(process.env.PVZ_SMOKE_SCREENSHOT)await page.screenshot({path:process.env.PVZ_SMOKE_SCREENSHOT});
    assert.equal(await page.locator('#canvas').getAttribute('width'),'1280');
    assert.equal(await page.locator('#canvas').getAttribute('height'),'720');
    assert.equal(await page.locator('#status').evaluate(el=>el.classList.contains('error')),false);
    // Persist a marker alongside real saves, reload, and verify IndexedDB restores it.
    await page.evaluate(async()=>{Module.FS.writeFile('/saves/web-smoke.txt','persistent');await save(false);});
    await page.reload();await page.locator('#start:enabled').waitFor({timeout:120000});
    assert.equal(await page.evaluate(()=>Module.FS.readFile('/saves/web-smoke.txt',{encoding:'utf8'})),'persistent');
    const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();
    const download=await downloadPromise;
    const archive=JSON.parse(fs.readFileSync(await download.path(),'utf8'));
    assert.equal(archive.format,'pvz-tft-web-save');assert(archive.files.some(x=>x.path==='web-smoke.txt'));
    const version=await page.evaluate(()=>typeof SharedArrayBuffer);
    console.log(`Browser startup, 1280x720 canvas, Pages subpath, IndexedDB and save export passed (SharedArrayBuffer: ${version}).`);
    assert.deepEqual(errors,[]);
    await context.close();
  } finally {if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
