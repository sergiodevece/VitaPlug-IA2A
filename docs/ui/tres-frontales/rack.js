"use strict";
const main=document.querySelector("#mount"), search=new URLSearchParams(location.search);
const dimensions={A:1060,B:1100,C:850};
const descriptions={A:"Conversación · La petición y la respuesta ocupan la pantalla principal. Los medidores quedan a un lado.",B:"Centro y lados · Dos trazas muestran dónde actúa la compresión, junto a la explicación en lenguaje musical.",C:"Escucha guiada · Un rack compacto con tres pasos y un vúmetro de reducción. La pantalla muestra una decisión cada vez."};
const drum={ask:"Los platos están muy sucios.",title:"Más espacio para los platos",reply:"Sujeto el centro del kit y dejo respirar los lados. Compara cómo cae el plato después de cada golpe.",short:"Escucha la cola del plato después del golpe. El centro del kit queda más sujeto."};
const voice={ask:"Pon la voz delante y que encaje en la mezcla.",title:"Una voz más estable",reply:"Acerco las frases suaves a las fuertes y añado un poco de AIR. Escucha si cada palabra se entiende sin perder expresión.",short:"Acerca las frases suaves y abre un poco el agudo. Compara las consonantes."};
const data={};let current="A",ticket=0;
const controls=(s)=>s.source==="Voz"?voice:drum;
const fresh=()=>({source:"Bus de batería",reference:false,phase:"proposal",listen:"preview",match:true,step:1,history:[],applied:0,asked:"",note:"",text:"",variation:0});
const esc=t=>String(t).replaceAll("&","&amp;").replaceAll('"',"&quot;").replaceAll("<","&lt;").replaceAll(">","&gt;");
const selected=(a,b)=>a===b?"selected":"";
function key(action,label,pressed,disabled=false,extra=""){return '<button class="key '+extra+'" data-act="'+action+'" '+(pressed==null?'':'aria-pressed="'+pressed+'"')+(disabled?' disabled':'')+'>'+label+'</button>'}
function head(s){return '<div class="head"><div class="logo"><span class="brand">IA<i>2A</i></span><span class="maker">VitaPlug<small>OPTICAL COMPRESSOR</small></span></div><div class="inline"><label class="label" for="src">Fuente</label><select id="src"><option '+selected(s.source,"Bus de batería")+'>Bus de batería</option><option '+selected(s.source,"Voz")+'>Voz</option></select><button class="ref" data-act="ref" aria-pressed="'+s.reference+'"><i class="pilot"></i>'+(s.reference?'Ref. mezcla activa':'Ref. mezcla inactiva')+'</button></div></div>'}
function footer(s){const pending=s.phase==='proposal';return '<div class="footer"><div class="keygroup">'+key('before','Antes',s.listen==='applied',!pending)+key('after','Después',s.listen==='preview',!pending)+'<label><select id="level" aria-label="Nivel de comparación"><option value="matched" '+selected(s.match,true)+'>Vol. igualado</option><option value="final" '+selected(s.match,false)+'>Nivel final</option></select></label></div><div class="keygroup">'+key('undo','Deshacer',null,!s.history.length)+key('drop','Descartar',null,!pending)+key('apply','Aplicar',null,!pending,'apply')+'</div></div>'}
function ask(s){return '<form class="ask">'+key('capture','Escuchar',null)+'<input id="prompt" aria-label="Petición al asistente" maxlength="180" value="'+esc(s.text)+'" placeholder="Pide un cambio con tus palabras…"><button class="key" type="submit">Enviar</button></form>'}
function info(s){const z=controls(s), p=s.phase==='proposal',idle=s.phase==='ready', active=s.listen==='preview'?s.variation:s.applied;
 const title=idle?"¿Qué quieres cambiar?":s.phase==='applied'?"Ajuste aplicado":s.phase==='discarded'?"Propuesta descartada":s.phase==='undone'?"Ajuste recuperado":z.title;
 const reply=s.note||(p?(s.variation>1?"He preparado una variación más suave para comparar con el ajuste anterior.":z.reply):idle?"Describe lo que te gustaría oír en esta pista.":"Puedes escuchar otro fragmento o pedir una corrección.");
 const short=s.note||(p&&s.variation===1?z.short:reply);
 const heading=p?"Propuesta "+String(s.variation).padStart(2,'0'):idle?"Captura lista":"Ajuste actual";
 return {title,reply,short,heading,request:s.asked||(idle?'Fragmento de '+s.source.toLowerCase():z.ask),active,domain:s.source==="Voz"?"MONO":"MID / SIDE"};}
function topLine(title,right){return '<div class="eyeline"><span>'+title+'</span><em>'+right+'</em></div>'}
function full(s){const i=info(s);return head(s)+'<div class="primary"><section class="screen">'+topLine('Conversación · '+s.source,i.heading)+'<div class="request">“'+esc(i.request)+'”</div><h2>'+i.title+'</h2><p>'+esc(i.reply)+'</p></section><aside class="bay"><h3>NIVELES</h3><canvas class="levels" data-plot="led" aria-label="Niveles ilustrativos de entrada y salida"></canvas><div class="rule"></div><div class="label">Reducción · máx.</div><div class="read">'+(i.active?'3,8':'0,0')+' <small>dB</small></div></aside></div><section class="console">'+ask(s)+footer(s)+'</section>'}
function detailed(s){const i=info(s),v=s.source==='Voz';return head(s)+'<div class="primary"><section class="screen scope">'+topLine('Reducción de ganancia','12 s · '+i.domain)+'<canvas class="trace" data-plot="trace" aria-label="Historia ilustrativa de la reducción de ganancia"></canvas><div class="legend"><span>━ '+(v?'Voz':'Mid · centro')+'</span>'+(v?'':'<span>━ Side · lados</span>')+'</div></section><section class="screen word">'+topLine('Asistente',i.heading)+'<div class="request">“'+esc(i.request)+'”</div><h2>'+i.title+'</h2><p>'+esc(i.short)+'</p></section></div><section class="console">'+ask(s)+footer(s)+'</section>'}
function compact(s) {
 const i=info(s);
 const step=s.phase==='ready'?0:s.phase==='applied'?2:1;
 const steps=['1 · Escucha','2 · Compara','3 · Aplica'].map((label,n)=>
   '<span class="'+(n===step?'on':'')+'">'+label+'</span>').join('');
 const meter='<aside class="dialcase"><span class="dialname">Reducción de ganancia</span><canvas data-plot="dial" aria-label="Vúmetro ilustrativo de reducción en decibelios"></canvas></aside>';
 const display='<section class="screen">'+topLine(i.heading,i.domain)+'<div class="request">“'+esc(i.request)+'”</div><h2>'+i.title+'</h2><p>'+esc(i.short)+'</p></section>';
 return head(s)+'<div class="guide" aria-label="Pasos">'+steps+'</div><div class="primary">'+meter+display+'</div><section class="console">'+footer(s)+ask(s)+'</section>';
}
async function picture(url){const img=new Image();img.src=url;await img.decode();return img}
const surfaces=Promise.all([picture('assets/iaq8.png'),picture('assets/lab.png')]);
async function chassis(canvas,id){const [ia,lab]=await surfaces;const r=canvas.parentNode;if(!r?.isConnected)return;const w=r.offsetWidth,h=r.offsetHeight;canvas.width=w*2;canvas.height=h*2;const c=canvas.getContext('2d');c.scale(2,2);
 const src=id==='B'?lab:ia,patch=id==='B'?[1240,85,425,105]:[421,49,726,94];const [x,y,pw,ph]=patch;
 for(let j=0;j<h/78;j++)for(let k=0;k<w/444;k++){c.save();c.translate(k*444+(k%2?444:0),j*78+(j%2?78:0));c.scale(k%2?-1:1,j%2?-1:1);c.drawImage(src,x,y,pw,ph,0,0,444.5,78.5);c.restore()}
 if(id==='B'){c.drawImage(lab,4,0,1750,16,0,0,w,10);c.drawImage(lab,4,698,1750,18,0,h-12,w,12);c.drawImage(lab,0,8,16,691,0,3,9,h-10);c.drawImage(lab,1742,8,18,691,w-10,3,10,h-10);
 [[0,0],[w-30,0],[0,h-27],[w-30,h-27]].forEach(([dx,dy])=>{c.save();c.beginPath();c.arc(dx+15,dy+13,8,0,Math.PI*2);c.clip();c.drawImage(lab,3,24,36,37,dx+4,dy+2,22,22);c.restore()});}
 else {const e=id==='C'?26:36;c.drawImage(ia,85,30,1500,14,e,0,w-2*e,8);c.drawImage(ia,85,888,1500,14,e,h-9,w-2*e,9);c.drawImage(ia,4,33,77,867,0,0,e,h);c.drawImage(ia,1591,33,77,867,w-e,0,e,h);}
}
function art(el,s){const box=el.getBoundingClientRect(),w=el.clientWidth,h=el.clientHeight;if(!w||!h)return;el.width=w*2;el.height=h*2;const c=el.getContext('2d');c.scale(2,2);const i=info(s),on=i.active>0;
 if(el.dataset.plot==='led'){['IN','OUT'].forEach((label,row)=>{const yy=10+row*37;c.fillStyle='#becab8';c.font='13px monospace';c.fillText(label,0,yy+11);const n=17;for(let a=0;a<n;a++){c.fillStyle=a<(row===0?13:on?12:13)?(a>=12?'#e0c777':'#88cd87'):'#28372d';c.fillRect(33+a*(w-33)/n,yy,(w-33)/n-2,13)}});return}
 if(el.dataset.plot==='dial'){const bg=c.createLinearGradient(0,0,0,h);bg.addColorStop(0,'#b3a280');bg.addColorStop(.45,'#eddbac');bg.addColorStop(1,'#c7ae7b');c.fillStyle=bg;c.fillRect(0,0,w,h);const ox=w/2,oy=h*1.35,r=h*1.02;const angle=d=>(-135+(d/10)*90)*Math.PI/180;
 for(let n=0;n<=20;n++){const a=angle(n/2),major=n%4===0;c.strokeStyle=n>15?'#a74732':'#413b2f';c.lineWidth=major?1.7:1;c.beginPath();c.moveTo(ox+r*Math.cos(a),oy+r*Math.sin(a));c.lineTo(ox+(r-(major?10:5))*Math.cos(a),oy+(r-(major?10:5))*Math.sin(a));c.stroke();if(major){c.font='9px monospace';c.textAlign='center';c.fillStyle='#332f28';c.fillText(n/2,ox+(r+9)*Math.cos(a),oy+(r+9)*Math.sin(a)+3)}}const a=angle(on?3.8:0);c.strokeStyle='#743524';c.lineWidth=2;c.beginPath();c.moveTo(ox,oy);c.lineTo(ox+(r+2)*Math.cos(a),oy+(r+2)*Math.sin(a));c.stroke();c.fillStyle='#524631';c.textAlign='center';c.font='12px Georgia';c.fillText('GR  ·  dB',w/2,h-8);return}
 const pad=29,ww=w-pad-8; c.font='10px monospace';for(let k=0;k<4;k++){const yy=16+k*(h-34)/3;c.strokeStyle='#304738';c.beginPath();c.moveTo(pad,yy);c.lineTo(w-6,yy);c.stroke();c.fillStyle='#8ca597';c.fillText(k===0?'0':('−'+k*2),2,yy+3)}
 for(let n=0;n<=12;n++){c.strokeStyle='#25392c';c.beginPath();c.moveTo(pad+n*ww/12,12);c.lineTo(pad+n*ww/12,h-13);c.stroke()}
 let tracks=s.source==='Voz'?1:2;for(let d=0;d<tracks;d++){c.strokeStyle=d?'#ebb675':'#91e4af';c.lineWidth=d?2:2.4;c.beginPath();for(let x=0;x<=ww;x++){const t=x/ww*12,cycle=(t+.11)%1.47;const env=(1-Math.exp(-cycle*16))*Math.exp(-cycle*(d?4.3:2.6));const wobble=(1+.16*Math.sin(t*5)+.08*Math.sin(t*12));const yy=16+(on?(d?8:44)*env*wobble+(d?3:7):0);x?c.lineTo(pad+x,yy):c.moveTo(pad,yy)}c.stroke()}c.fillStyle='#8ca597';c.fillText('0 s',pad,h-1);c.fillText('12 s',w-32,h-1)}
function scale(){const r=main.querySelector('.rack');if(!r)return;const k=Math.min(1,main.clientWidth/dimensions[current]);r.style.transform='scale('+k+')';main.style.height=(r.offsetHeight*k)+'px';}
function draw(id){current=id;const s=data[id]??=fresh();if(!s.variation)s.variation=1;const width=dimensions[id];main.style.maxWidth=width+'px';main.innerHTML='<article aria-label="Frontal '+id+'" class="rack '+id+'" style="width:'+width+'px"><canvas aria-hidden="true" class="plate"></canvas>'+({A:full,B:detailed,C:compact})[id](s)+'</article>';scale();main.querySelectorAll('[data-plot]').forEach(el=>art(el,s));const tick=++ticket;chassis(main.querySelector('.plate'),id).then(()=>{if(ticket===tick)document.body.dataset.rendered=id});document.querySelectorAll('[data-version]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.version===id));document.querySelector('#caption').textContent=descriptions[id]}
main.addEventListener('input',e=>{if(e.target.id==='prompt')data[current].text=e.target.value});
main.addEventListener('change',e=>{const s=data[current];if(e.target.id==='src'){const src=e.target.value;data[current]={...fresh(),source:src,variation:1}}else if(e.target.id==='level')s.match=e.target.value==='matched';draw(current)});
main.addEventListener('submit',e=>{e.preventDefault();const s=data[current];const typed=s.text.trim();if(!typed){main.querySelector('#prompt').focus();return}Object.assign(s,{asked:typed,text:'',phase:'proposal',variation:s.variation+1,listen:'preview',note:'Variación de ejemplo lista para comparar.'});draw(current)});
main.addEventListener('click',e=>{const b=e.target.closest('[data-act]');if(!b)return;e.preventDefault();const s=data[current];switch(b.dataset.act){
case 'ref':s.reference=!s.reference;break;
case 'before':s.listen='applied';break;
case 'after':s.listen='preview';break;
case 'capture':Object.assign(s,{phase:'ready',listen:'applied',asked:'',note:'Fragmento de ejemplo preparado. Escribe qué quieres cambiar.'});break;
case 'apply':s.history.push(s.applied);s.applied=s.variation;Object.assign(s,{phase:'applied',listen:'applied',note:'Propuesta confirmada en esta maqueta. Puedes deshacerla.'});break;
case 'undo':s.applied=s.history.pop()??0;Object.assign(s,{phase:'undone',listen:'applied',note:'Has recuperado el ajuste anterior.'});break;
case 'drop':Object.assign(s,{phase:'discarded',listen:'applied',note:'Puedes pedir otra propuesta para este fragmento.'});break;
}draw(current)});
document.querySelector('nav').addEventListener('click',e=>{const b=e.target.closest('[data-version]');if(b){draw(b.dataset.version);history.replaceState(null,'','#'+current)}});
if(search.has('capture'))document.body.classList.add('capture');let start=(search.get('capture')||location.hash.slice(1)||'A').toUpperCase();if(!dimensions[start])start='A';draw(start);new ResizeObserver(scale).observe(main);document.fonts.ready.then(()=>draw(current));
