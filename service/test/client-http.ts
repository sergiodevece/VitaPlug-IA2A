import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { claudeProvider, demoProvider, httpProvider } from '../src/provider.ts';
import { createTurnServer } from '../src/turn.ts';

// Feed the real HTTP adapter from a fixture provider, then exercise the native client.
const listen=async (server) => {
  server.listen(0,'127.0.0.1');
  await once(server,'listening');
  return 'http://127.0.0.1:'+server.address().port;
};
const useClaude=process.argv.includes("--claude");
function conversationReply(turn, request, system) {
  assert.equal(turn.sourceOrigin,'unknown');
  assert.equal(turn.features.attackDb,6);
  assert.equal(turn.features.decayDb,10);
  assert.equal(turn.features.sideTailRatioDb,-8);
  assert.equal(turn.features.highSpreadDb,4);
  assert.ok(!Object.hasOwn(turn,'trackName') && !Object.hasOwn(turn.features,'pcm'));
  assert.equal(request.headers['x-api-key'],'fixture');
  assert.equal(request.headers.authorization,undefined);
  assert.ok(system.includes('¿Qué tipo de fuente vamos a procesar?'));
  if(turn.requestId==='native-chat-1') {
    assert.equal(turn.history.length,0);
    return {kind:'question',text:'¿Qué tipo de fuente vamos a procesar?'};
  }
  if(turn.requestId==='native-chat-2') {
    assert.deepEqual(turn.history,['¿Puedes quitarle ese punto que se come el resto?','¿Qué tipo de fuente vamos a procesar?']);
    assert.equal(turn.prompt,'Es un bajo con púa. La primera nota de cada frase pega un latigazo.');
    return {kind:'goal',text:'Probaré a suavizar ese latigazo del bajo.',
      goal:{allowLR:true,allowMS:false,priorities:[{objective:'reduceAttack',intensity:.6}]}};
  }
  assert.equal(turn.requestId,'native-chat-3');
  assert.deepEqual(turn.history.slice(-2),[
    'Es un bajo con púa. La primera nota de cada frase pega un latigazo.',
    'Probaré a suavizar ese latigazo del bajo.'
  ]);
  assert.equal(turn.previousGoal.priorities[0].objective,'reduceAttack');
  assert.equal(turn.previousResult.outcome,'candidate');
  assert.equal(turn.previousResult.before.attackDb,6);
  assert.equal(turn.previousResult.after.attackDb,3.5);
  assert.equal(turn.previousResult.midReductionDb,4);
  assert.equal(turn.lastAction,'applied');
  return {kind:'goal',text:'Aflojaré un poco la compresión del ajuste conservado.',
    goal:{allowLR:true,allowMS:false,priorities:[{objective:'easeCompression',intensity:.4}]}};
}

const model=createServer(async(req,res)=>{
  let body='';for await(const chunk of req)body+=chunk;
  const request=JSON.parse(body);
  const turn=JSON.parse(request.messages[useClaude?0:1].content);
  let reply;
  try {
    if(useClaude && turn.requestId==='native-controls-2') {
      assert.equal(turn.features.lowRatioDb,-6);
      assert.ok(request.system.includes('filters only the detector'));
      reply={kind:'goal',text:'Probaré a preservar el peso del grave.',
        goal:{allowLR:true,allowMS:false,priorities:[{objective:'bassWeight',intensity:.7}]}};
    } else if(useClaude && turn.requestId==='native-controls-1') {
      assert.equal(turn.auditioning,'pending');
      assert.equal(turn.pendingSettings.mid.attackMs,10);
      assert.equal(turn.pendingSettings.mid.hpfOn,false);
      assert.deepEqual(turn.previousGoal,{
        allowLR:true,allowMS:false,priorities:[],
        edits:[{control:'thresholdDb',domain:'both',change:'decrease',value:3}],
        compensateGain:true
      });
      assert.ok(request.system.includes('The user has final say'));
      reply={kind:'goal',text:'Fijaré el umbral y subiré el aire.',
        goal:{allowLR:true,allowMS:false,priorities:[],compensateGain:true,
          edits:[{control:'thresholdDb',domain:'both',change:'set',value:-32.5},
            {control:'airDb',domain:'both',change:'increase',value:null},
            {control:'hpfOn',domain:'main',change:'set',value:1}]}};
    } else {
      reply=useClaude && turn.requestId.startsWith('native-chat-')
        ? conversationReply(turn,req,request.system)
        : await demoProvider(turn,new AbortController().signal);
    }
  } catch(error) {
    console.error(error);
    res.writeHead(422).end();
    return;
  }
  res.setHeader('Content-Type','application/json');
  res.end(JSON.stringify(useClaude
    ? {stop_reason:'end_turn',content:[{type:'text',text:JSON.stringify({goal:null,...reply})}]}
    : {choices:[{message:{content:JSON.stringify(reply)}}]}));
});
const modelUrl=await listen(model);
const name=useClaude?'claude':'http';
const provider=useClaude?claudeProvider({key:'fixture',model:'fixture'},modelUrl):httpProvider({endpoint:modelUrl,model:'fixture'});
const service=createTurnServer({name,token:'test-token',provider});
try {
  const url=await listen(service);
  const child=spawn(process.argv[2],[url+'/v1/turn',name],{stdio:'inherit'});
  const [code]=await once(child,'exit');
  process.exitCode=code ?? 1;
} finally {
  service.closeAllConnections();service.close();
  model.closeAllConnections();model.close();
}
