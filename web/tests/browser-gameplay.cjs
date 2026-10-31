const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const http=require('node:http');
const {chromium}=require('playwright');

async function main(){
  const root=path.resolve(process.argv[2]||'build-web/dist');
  const server=http.createServer((req,res)=>{
    const url=new URL(req.url,'http://localhost');
    const file=path.resolve(root,url.pathname.slice(1)||'index.html');
    if(!file.startsWith(root+path.sep)||!fs.existsSync(file)){res.writeHead(404).end();return;}
    res.setHeader('Content-Type',({'.wasm':'application/wasm','.js':'text/javascript','.html':'text/html'})[path.extname(file)]||'application/octet-stream');
    fs.createReadStream(file).pipe(res);
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({args:['--no-sandbox','--enable-unsafe-swiftshader']});
  let failed=false;
  for(const [scene,name] of [[0,'almanac'],[1,'right-click'],[2,'augment-wave']]){
    const context=await browser.newContext({viewport:{width:1280,height:720},hasTouch:true});
    context.setDefaultTimeout(10000);
    const watchdog=setTimeout(()=>{console.error(`${name}: renderer stopped responding; closing isolated context.`);context.close().catch(()=>{});},45000);
    const page=await context.newPage();const errors=[];
    page.on('pageerror',e=>errors.push(e.stack));
    try{
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      await page.locator('#start:enabled').waitFor({timeout:120000});await page.locator('#start').click();
      await page.waitForFunction(()=>Module._pvz_web_ready?.()===1,null,{timeout:120000});
      await page.evaluate(scene=>Module._pvz_web_test_setup(scene),scene);
      const state=()=>page.evaluate(()=>{Module._pvz_web_test_state();return Module.testState;});
      const healthy=async()=>{const n=await page.evaluate(()=>Module._pvz_web_updates());await page.waitForFunction(n=>Module._pvz_web_updates()>n+5,n,{timeout:5000});assert.deepEqual(errors,[]);};
      const click=async(x,y,options={})=>{const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+x*b.width/1280,b.y+y*b.height/720,options);};
      const hold=async(x,y)=>{
        await page.evaluate(({x,y})=>{const c=Module.canvas,r=c.getBoundingClientRect();const t=new Touch({identifier:1,target:c,clientX:r.left+x*r.width/1280,clientY:r.top+y*r.height/720});window.testTouch=t;c.dispatchEvent(new TouchEvent('touchstart',{bubbles:true,cancelable:true,touches:[t],changedTouches:[t]}));},{x,y});
        await page.waitForTimeout(380);
        await page.evaluate(()=>Module.canvas.dispatchEvent(new TouchEvent('touchend',{bubbles:true,cancelable:true,touches:[],changedTouches:[window.testTouch]})));
        await page.waitForTimeout(50);
      };
      await healthy();
      if(scene===0){
        await page.screenshot({path:'build-web/gameplay-almanac-check.png'});
        assert.equal((await state()).selected,0);
        await click(365,190);await healthy();assert.equal((await state()).selected,1);
        await click(520,640);await healthy();assert.equal((await state()).selected,8);
        await page.screenshot({path:'build-web/gameplay-gifts-check.png'});
      } else if(scene===1){
        const p=(await state()).packet;
        await click(p.x,p.y,{button:'right'});await healthy();assert.equal((await state()).locked,1,'desktop right-click locks a Shop packet');
        await hold(p.x,p.y);await healthy();assert.equal((await state()).locked,0,'touch hold sends a right-click to the game');
        await hold(p.x,p.y);await healthy();assert.equal((await state()).locked,1,'repeated touch holds remain usable');
        await page.screenshot({path:'build-web/gameplay-right-click-check.png'});
      } else {
        let s=await state();assert.equal(s.choosing,true,'wave 3 enters the Augment chooser');
        const t=s.timer;await page.waitForTimeout(250);await healthy();s=await state();assert(s.timer>t,'Augment animation clock advances');
        await page.screenshot({path:'build-web/gameplay-augment-check.png'});
        const card=s.cards[0];await click(card.rollX,card.rollY);await healthy();
        await click(card.x+card.w/2,card.y+card.h/2);
        await page.waitForFunction(()=>{Module._pvz_web_test_state();return !Module.testState.choosing;},null,{timeout:5000});
        await healthy();assert((await state()).active>=0,'chosen Augment is applied and gameplay resumes');
      }
      console.log(`${name}: native gameplay regression passed.`);
    }catch(e){
      failed=true;console.error(`${name}:`,e.stack);console.error(errors);
      console.error(await page.locator('#log').textContent().catch(()=>''));
      await page.screenshot({path:`build-web/gameplay-${name}-check.png`,timeout:5000}).catch(()=>{});
    }finally{clearTimeout(watchdog);await context.close();}
  }
  await browser.close();await new Promise(r=>server.close(r));
  if(failed)throw new Error('Native gameplay regressions failed.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
