'use strict';
const $=id=>document.getElementById(id);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const last={preview:'',pair:''};
let currentPair=null;
function info(buffer){
  const d=new DataView(buffer),u64=o=>d.getBigUint64(o,false);
  return {d,width:d.getUint16(76),height:d.getUint16(78),model:d.getUint32(72),format:d.getUint16(80),flags:d.getUint16(82),shot:u64(24).toString(),
    key:[u64(16),u64(24),d.getUint32(32),d.getUint32(36)].join(':'),trigger:u64(40),sgm:u64(56),done:u64(64)};
}
function canvas(id,w,h,data){
  const c=$(id);c.width=w;c.height=h;c.getContext('2d').putImageData(new ImageData(data,w,h),0,0);
  if(c.parentElement.classList.contains('screen'))c.parentElement.classList.add('ready');
}
function rgb(buffer,offset,n){const a=new Uint8Array(buffer,offset,n*3),out=new Uint8ClampedArray(n*4);for(let i=0;i<n;i++){out[4*i]=a[3*i];out[4*i+1]=a[3*i+1];out[4*i+2]=a[3*i+2];out[4*i+3]=255;}return out;}
const colors=[[20,45,140],[0,184,225],[167,224,68],[255,240,134],[214,54,54]];
function color(value){const t=Math.max(0,Math.min(4,value/16)),i=Math.min(3,Math.floor(t)),f=t-i;return colors[i].map((v,k)=>Math.round(v+(colors[i+1][k]-v)*f));}
function pair(buffer,h){
  currentPair={buffer,h};
  const n=h.width*h.height,sgm=new Uint8Array(buffer,96+3*n,n),valid=new Uint8Array(buffer,96+4*n,n),a=new Uint8ClampedArray(n*4),b=new Uint8ClampedArray(n*4);
  for(let i=0;i<n;i++){
    let sg=h.format>=4?!!(valid[i]&2):!!valid[i],md=h.format>=4?!!(valid[i]&4):true;
    if($('sharedMask').checked)sg=md=sg&&md;
    const invalid=((Math.floor(i/h.width/8)+Math.floor(i%h.width/8))%2)?[90,100,109]:[60,71,81];
    const c=sg?color(sgm[i]):invalid;
    const m=md?color(h.d.getFloat32(96+5*n+4*i,true)):invalid;
    a.set([...c,255],i*4);b.set([...m,255],i*4);
  }
  // Both canvases are updated in the same browser task after one complete packet.
  canvas('sgm',h.width,h.height,a);canvas('model',h.width,h.height,b);canvas('photo',h.width,h.height,rgb(buffer,96,n));
  const frozen=h.format===5,meta=96+9*n,started=frozen?h.d.getBigUint64(meta+16):h.trigger;
  $('resultTitle').textContent=frozen?`按键 #${h.shot} · 源帧 #${h.d.getBigUint64(meta)}`:`最近一次拍照 · #${h.shot}`;
  $('modelLabel').textContent=h.model===11459?'小模型处理后':'final1 处理后';
  $('sgmTime').textContent=(Number(h.sgm-started)/1000).toFixed(1)+' ms';
  $('modelTime').textContent=(Number(h.done-started)/1000).toFixed(1)+' ms';
  $('sourceAge').textContent=frozen?`选中时源帧年龄 ${(Number(h.d.getBigUint64(meta+32)-h.d.getBigUint64(meta+24))/1000).toFixed(1)} ms；按键至选中 ${(Number(h.d.getBigUint64(meta+32)-h.trigger)/1000).toFixed(1)} ms。`:'此结果使用按键后采集协议。';
  $('synthetic').style.display=h.flags&1?'block':'none';
  $('maskNote').textContent=h.format>=4?'灰色方格表示无效区域；已检查双目校正范围。取消勾选可查看各自有效区域及模型填补结果。':'此数据未提供校正范围掩码；共同区域仅依据 SGM 有效标记。';
}
function preview(buffer,h){
  const n=h.width*h.height;
  if(h.format===1){canvas('preview',h.width,h.height,rgb(buffer,96,n));return;}
  const src=new Uint8Array(buffer,96),out=new Uint8ClampedArray(n*4);
  for(let i=0;i<n;i+=2){const u=src[i*2+1]-128,v=src[i*2+3]-128;for(let k=0;k<2;k++){const y=src[i*2+2*k]-16,o=(i+k)*4;out[o]=1.164*y+1.596*v;out[o+1]=1.164*y-.392*u-.813*v;out[o+2]=1.164*y+2.017*u;out[o+3]=255;}}
  canvas('preview',h.width,h.height,out);
}
async function images(kind){
  for(;;){try{const r=await fetch(`/api/${kind}?after=${encodeURIComponent(last[kind])}`,{cache:'no-store'});if(r.status===200){const buffer=await r.arrayBuffer(),h=info(buffer);if(kind==='pair')pair(buffer,h);else preview(buffer,h);last[kind]=h.key;}}catch(e){}await sleep(kind==='preview'?40:150);}
}
async function state(){
  for(;;){try{const s=await(await fetch('/api/state',{cache:'no-store'})).json();
    $('connection').textContent=s.links.preview&&s.links.result?'设备已连接':s.links.preview||s.links.result?'部分通道已连接':'设备未连接';
    const age=s.ages_seconds.preview;
    $('previewState').textContent=age===null?'尚未收到视频帧':!s.links.preview?'视频已断开，保留最后画面':`最近一帧：${age.toFixed(1)} 秒前`;
    $('resultState').textContent=!s.pair?'尚未收到完整结果':!s.links.result?'结果通道未连接，保留上次完整结果':'两张图来自同一源帧；后台继续计算，按键选择最新完整结果。';
    if(s.links.result&&s.status){const label={1:'已接受拍摄请求',2:'正在处理；首份完整结果就绪后自动发送，无需再次按键',3:'SGM 已就绪，等待模型完成',4:'正在回传结果',5:'上一请求尚未完成，请稍候',6:'处理失败，保留上次结果',7:'结果尚未就绪；本次请求已结束，请稍后再按键'}[s.status.status];if(label)$('resultState').textContent=label;}
  }catch(e){$('connection').textContent='本机接收程序未连接';}await sleep(500);}
}
$('sharedMask').addEventListener('change',()=>{if(currentPair)pair(currentPair.buffer,currentPair.h);});
images('preview');images('pair');state();
