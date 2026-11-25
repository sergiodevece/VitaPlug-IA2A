import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {publishLocalConnection} from '../src/connection.ts';
import {createTurnServer} from '../src/turn.ts';

// Exercise the running AudioProcessor across a clarification, a proposal, Apply and a new capture.
const requests = [];
const token='a'.repeat(64);
const server=createTurnServer({name:'claude',token,provider:async turn=>{
  requests.push(turn);
  switch (requests.length) {
    case 1:
      return {kind:'question',text:'¿Qué tipo de fuente vamos a procesar?'};
    case 2:
      return {kind:'goal',text:'Voy a sostener las palabras que quedan atrás.',goal:{
        allowLR:true,allowMS:false,
        priorities:[{objective:'stabilise',intensity:1},{objective:'bringForward',intensity:0.8}]
      }};
    case 3:
      throw new Error('mock upstream temporarily failed');
    case 4:
      return {kind:'keep',text:'Conserva la propuesta anterior.'};
    default:
      return {kind:'keep',text:'Conserva el ajuste aplicado en la voz.'};
  }
}});
server.listen(0,'127.0.0.1');
await once(server,'listening');
const testHome=mkdtempSync(join(tmpdir(),'ia2a-dialogue-home-'));
const removeConnection=publishLocalConnection(server.address().port,token,
  join(testHome,'Library','Application Support','VitaPlug','IA2A'));
try {
  const child=spawn(process.argv[2],['--conversation'],{
    stdio:'inherit',
    env:{...process.env,IA2A_TURN_URL:'',IA2A_SERVICE_TOKEN:'',CFFIXED_USER_HOME:testHome}
  });
  const [code]=await once(child,'exit');
  assert.equal(code,0);
  assert.equal(requests.length,5);
  const [first,second,failed,retried,third]=requests;
  assert.equal(failed.prompt,retried.prompt);
  assert.deepEqual(failed.history,retried.history);
  assert.deepEqual(failed.previousResult,retried.previousResult);
  assert.equal(failed.hasPending,true);
  assert.equal(first.sourceOrigin,'unknown');
  assert.equal(first.source,'other');
  assert.equal(first.features.usable,true);
  assert.equal(typeof first.features.attackDb,'number');
  assert.equal(typeof first.features.decayDb,'number');
  assert.equal(second.history[0],first.prompt);
  assert.match(second.history[1],/fuente/);
  assert.match(second.prompt,/voz principal/);
  assert.equal(third.history[2],second.prompt);
  assert.match(third.history[3],/palabras/);
  assert.equal(third.lastAction,'applied');
  assert.equal(third.hasApplied,true);
  assert.equal(third.previousGoal.priorities[0].objective,'stabilise');
  assert.equal(third.previousResult,undefined);
  assert.ok(requests.every(turn=>!('trackName' in turn) && !('audio' in turn)));
} finally {
  removeConnection();
  rmSync(testHome,{recursive:true,force:true});
  server.closeAllConnections();
  server.close();
}
