import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { validRequest, validResponse, createTurnServer } from '../src/turn.ts';
import { claudeProvider, demoProvider, httpProvider } from '../src/provider.ts';

const cases = JSON.parse(readFileSync(new URL('../../contract/examples/turns.json', import.meta.url), 'utf8'));
const first = cases[0].request;
test('shared examples produce bounded musical goals, questions or keep decisions', async () => {
  for (const {request, expectedKind} of cases) {
    assert.equal(validRequest(request), true);
    const reply = {version:1, requestId:request.requestId, provider:'local-demo', ...await demoProvider(request)};
    assert.equal(validResponse(reply, request), true);
    assert.equal(reply.kind, expectedKind);
    if (request.channels === 1 && reply.goal) assert.equal(reply.goal.allowMS, false);
    if (request.prompt === 'menos aire') assert.ok(reply.goal.priorities.some(p => p.objective==='air' && p.intensity<0));
  }
});
test('strict shape rejects arbitrary payloads, raw paths as fields, malformed IDs and invalid goals', () => {
  for (const bad of [{...first, pcm:[2]}, {...first,features:{path:'/tmp/voice.wav'}}, {...first,requestId:''}, {...first,channels:9}, {...first,prompt:' '}]) assert.equal(validRequest(bad),false);
  const response = {version:1,requestId:first.requestId,provider:'http',kind:'goal',text:'Una voz estable.',goal:{allowLR:true,allowMS:false,priorities:[{objective:'stabilise',intensity:0.6}]}};
  assert.ok(validResponse(response, first));
  for(const bad of [{...response,ratio:4},{...response,requestId:'wrong'},{...response,goal:{...response.goal,allowMS:true}},{...response,goal:{...response.goal,priorities:[{objective:'stabilise',intensity:-0.2}]}},{...response,goal:{...response.goal,priorities:[...response.goal.priorities,...response.goal.priorities]}}]) assert.equal(validResponse(bad,first),false);
});
test('HTTP service authenticates clients, bounds requests and validates the HTTP provider', async t => {
  const listen = async server => {server.listen(0,'127.0.0.1'); await once(server,'listening'); t.after(()=>{server.closeAllConnections();return new Promise(resolve=>server.close(resolve))}); return 'http://127.0.0.1:'+server.address().port; };
  let modelInput;
  let invalid=false;
  const upstream = await listen(createServer(async (req,res)=>{
    let body=''; for await(const chunk of req) body+=chunk;
    const received=JSON.parse(body); modelInput=JSON.parse(received.messages[1].content);
    assert.equal(req.headers.authorization,'Bearer model-token');
    res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({choices:[{message:{content:JSON.stringify(invalid?{ratio:4}:{kind:'keep',text:'Conserva esta toma.'})}}]}));
  }));
  const api=await listen(createTurnServer({token:'client-token',name:'http',provider:httpProvider({endpoint:upstream,model:'test-model',key:'model-token'})}));
  const send=(body,token='client-token')=>fetch(api+'/v1/turn',{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify(body)});
  assert.equal((await send(first,'wrong')).status,401);
  assert.equal((await send({...first,prompt:'x'.repeat(17000)})).status,413);
  assert.equal((await send({...first,pcm:[]})).status,400);
  const valid=await send(first);assert.equal(valid.status,200);assert.equal((await valid.json()).requestId,first.requestId);
  assert.deepEqual(modelInput,first);
  invalid=true;assert.equal((await send(first)).status,502);
});

test('Claude Messages receives only the turn and returns validated goals and clarifications', async t => {
  const requests = [];
  let answer = {
    kind:'goal', text:'Daré espacio a las colas y mantendré el golpe.',
    goal:{allowLR:true,allowMS:true,priorities:[{objective:'releaseTails',intensity:0.8}]}
  };
  const upstream = createServer(async (req,res) => {
    let body='';
    for await(const chunk of req) body+=chunk;
    requests.push({headers:req.headers, body:JSON.parse(body)});
    res.setHeader('Content-Type','application/json');
    res.end(JSON.stringify({stop_reason:'end_turn',content:[{type:'text',text:JSON.stringify(answer)}]}));
  });
  const bridge = createTurnServer({
    token:'local-only', name:'claude',
    provider:claudeProvider({key:'fixture-key',model:'fixture-claude',workspace:'wrkspc_test'},
      await listen(upstream))
  });
  const url = await listen(bridge);
  const stereo = {...first,source:'drums',channels:2,prompt:'Aprieta la pegada del bombo y la caja y deja los platos abiertos'};
  const send = () => fetch(url+'/v1/turn',{method:'POST',headers:{Authorization:'Bearer local-only','Content-Type':'application/json'},body:JSON.stringify(stereo)});

  const proposed = await send();
  assert.equal(proposed.status,200);
  assert.deepEqual(await proposed.json(),{...answer,provider:'claude',version:1,requestId:stereo.requestId});
  const {headers,body} = requests[0];
  assert.equal(headers['x-api-key'],'fixture-key');
  assert.equal(headers['anthropic-workspace-id'],'wrkspc_test');
  assert.equal(headers['anthropic-version'],'2023-06-01');
  assert.equal(headers.authorization,undefined);
  assert.equal(body.model,'fixture-claude');
  assert.equal(body.output_config.format.type,'json_schema');
  assert.equal(body.output_config.format.schema.additionalProperties,false);
  assert.deepEqual(JSON.parse(body.messages[0].content),stereo);

  answer={kind:'question',text:'¿Te molestan los ataques o las colas?',goal:null};
  const question=await send();
  assert.equal(question.status,200);
  assert.equal((await question.json()).goal,undefined);
  answer={kind:'goal',text:'Una propuesta inválida.',goal:null};
  assert.equal((await send()).status,502);

  async function listen(server) {
    server.listen(0,'127.0.0.1');
    await once(server,'listening');
    t.after(()=>{server.closeAllConnections();return new Promise(resolve=>server.close(resolve))});
    return 'http://127.0.0.1:'+server.address().port;
  }
});
