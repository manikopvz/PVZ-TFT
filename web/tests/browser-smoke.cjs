const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require('playwright');

async function main() {
  const target = process.argv[2] || 'build-web/dist';
  const liveUrl = target.startsWith('https://') ? target : null;
  const root = liveUrl ? null : path.resolve(target);
  const errors = [];
  const mime = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.data':'application/octet-stream','.json':'application/json'};
  const server = liveUrl ? null : http.createServer((req,res) => {
    let pathname;
    try { pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname); } catch {res.writeHead(400).end();return;}
    if(!pathname.startsWith('/PVZ-TFT/')) {res.writeHead(404).end();return;}
    const file=path.resolve(root,pathname.slice('/PVZ-TFT/'.length)||'index.html');
    if(!file.startsWith(root+path.sep)) {res.writeHead(403).end();return;}
    if(!fs.existsSync(file) || !fs.statSync(file).isFile()) {res.writeHead(404).end();return;}
    res.setHeader('Content-Type',mime[path.extname(file)]||'application/octet-stream');fs.createReadStream(file).pipe(res);
  });
  if(server)await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader',...(process.env.PVZ_BROWSER_SINGLE_PROCESS?['--single-process','--no-zygote']:[])]});
    const context=await browser.newContext({viewport:{width:1360,height:1000}});
    const page=await context.newPage();
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    page.on('response',response=>{if(response.status()>=400 && !response.url().endsWith('favicon.ico'))errors.push(`${response.status()} ${response.url()}`);});
    const url=liveUrl || `http://127.0.0.1:${server.address().port}/PVZ-TFT/`;
    await page.goto(url);
    await page.locator('#start:enabled').waitFor({timeout:120000});
    await page.locator('#start').click();
    try {
      await page.waitForFunction(()=>window.Module && Module._pvz_web_ready && Module._pvz_web_ready()===1,null,{timeout:120000});
    } catch(error) {console.error(await page.locator('#log').textContent());throw error;}
    // Check the actual GL viewport, not merely the canvas dimensions (the old
    // renderer squeezed a 16:9 projection into a 4:3 presentation rectangle).
    assert.deepEqual(await page.evaluate(()=>{const gl=Module.canvas.getContext('webgl');return Array.from(gl.getParameter(gl.VIEWPORT));}),[0,0,1280,720]);
    await page.waitForFunction(()=>Module._pvz_web_music_playing()>0 && Module._pvz_web_music_samples()>1000,null,{timeout:30000});
    const samples=await page.evaluate(()=>Module._pvz_web_music_samples());
    await page.waitForFunction(before=>Module._pvz_web_music_samples()>before+1000,samples,{timeout:10000});
    assert.equal(await page.evaluate(()=>SDL2.audioContext.state),'running');
    // Enter the real new-profile dialog using game input, then exercise the
    // mobile input bridge, native deletion/length validation, and Enter.
    await page.waitForTimeout(1500);
    await page.locator('#canvas').click({position:{x:300,y:200}});
    await page.waitForTimeout(1000);
    await page.locator('#keyboard').click();
    await page.waitForFunction(()=>document.activeElement.id==='pvz-soft-keyboard');
    await page.locator('#pvz-soft-keyboard').fill('MobileTest');
    await page.locator('#pvz-soft-keyboard').fill('Mobile');
    await page.locator('#keyboard').click();
    await page.locator('#keyboard').click();
    assert.equal(await page.locator('#pvz-soft-keyboard').inputValue(),'Mobile');
    await page.locator('#pvz-soft-keyboard').press('Enter');
    await page.waitForTimeout(600);
    assert.equal(await page.evaluate(()=>document.activeElement.id),'canvas');
    // No labels, headers, save toolbar, or footer around the game.
    assert.equal(await page.locator('button:visible').count(),2);
    assert.deepEqual(await page.locator('button:visible').allTextContents(),['','']);
    for(const viewport of [{width:390,height:844},{width:844,height:390},{width:1360,height:1000}]) {
      await page.setViewportSize(viewport);
      const box=await page.locator('#canvas').boundingBox();
      assert(Math.abs(box.width/box.height-16/9)<0.01);
      assert(box.width<=viewport.width+1 && box.height<=viewport.height+1);
      assert.deepEqual(await page.evaluate(()=>{const gl=Module.canvas.getContext('webgl');return Array.from(gl.getParameter(gl.VIEWPORT));}),[0,0,1280,720]);
    }
    if(process.env.PVZ_SMOKE_SCREENSHOT)await page.screenshot({path:process.env.PVZ_SMOKE_SCREENSHOT});
    assert.equal(await page.locator('#canvas').getAttribute('width'),'1280');
    assert.equal(await page.locator('#canvas').getAttribute('height'),'720');
    assert.equal(await page.locator('#status').evaluate(el=>el.classList.contains('error')),false);
    // Persist a marker alongside real saves, reload, and verify IndexedDB restores it.
    await page.evaluate(async()=>{Module.FS.writeFile('/saves/web-smoke.txt','persistent');await save(false);});
    await page.reload();await page.locator('#start:enabled').waitFor({timeout:120000});
    assert.equal(await page.evaluate(()=>Module.FS.readFile('/saves/web-smoke.txt',{encoding:'utf8'})),'persistent');
    const downloadPromise=page.waitForEvent('download');await page.evaluate(()=>exportSave());
    const download=await downloadPromise;
    const archive=JSON.parse(fs.readFileSync(await download.path(),'utf8'));
    assert.equal(archive.format,'pvz-tft-web-save');assert(archive.files.some(x=>x.path==='web-smoke.txt'));
    await page.evaluate(()=>Module.FS.unlink('/saves/web-smoke.txt'));
    await page.evaluate(data=>importSave(new File([JSON.stringify(data)],'backup.json',{type:'application/json'})),archive);
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Đã khôi phục'));
    assert.equal(await page.evaluate(()=>Module.FS.readFile('/saves/web-smoke.txt',{encoding:'utf8'})),'persistent');
    await page.evaluate(()=>importSave(new File([JSON.stringify({format:'pvz-tft-web-save',version:1,files:[{path:'../outside',data:'AA=='}]})],'invalid.json',{type:'application/json'})));
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Không khôi phục được'));
    assert.equal(await page.evaluate(()=>Module.FS.analyzePath('/outside').exists),false);
    assert.equal(await page.locator('#start').isEnabled(),true);
    const version=await page.evaluate(()=>typeof SharedArrayBuffer);
    console.log(`Browser startup, full 1280x720 GL viewport, decoded music PCM, mobile username editing, responsive canvas, Pages subpath, IndexedDB, save backup helpers and traversal rejection passed (SharedArrayBuffer: ${version}).`);
    assert.deepEqual(errors,[]);
    await context.close();
    if(!liveUrl) {
      const graphicsContext=await browser.newContext();
      await graphicsContext.addInitScript(()=>{const get=HTMLCanvasElement.prototype.getContext;HTMLCanvasElement.prototype.getContext=function(type,...args){return ['webgl','webgl2','experimental-webgl'].includes(type)?null:get.call(this,type,...args);};});
      const graphicsPage=await graphicsContext.newPage();await graphicsPage.goto(url);
      await graphicsPage.locator('#start:enabled').waitFor({timeout:120000});await graphicsPage.locator('#start').click();
      await graphicsPage.waitForFunction(()=>document.getElementById('status').classList.contains('error') && document.getElementById('status').textContent.includes('WebGL'));
      assert.equal(await graphicsPage.evaluate(()=>Module._pvz_web_ready()),-2);
      await graphicsContext.close();console.log('Unavailable WebGL reports a clear startup error.');
    }
  } finally {if(browser)await browser.close();if(server)await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});


