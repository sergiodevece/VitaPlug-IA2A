import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { demoProvider, httpProvider } from '../src/provider.ts';
import { createTurnServer } from '../src/turn.ts';

// Feed the real HTTP adapter from a fixture provider, then exercise the native client.
const listen=async (server) => {
  server.listen(0,'127.0.0.1');
  await once(server,'listening');
  return 'http://127.0.0.1:'+server.address().port;
};
const model=createServer(async(req,res)=>{
  let body='';for await(const chunk of req)body+=chunk;
  const turn=JSON.parse(JSON.parse(body).messages[1].content);
  const reply=await demoProvider(turn,new AbortController().signal);
  res.setHeader('Content-Type','application/json');
  res.end(JSON.stringify({choices:[{message:{content:JSON.stringify(reply)}}]}));
});
const modelUrl=await listen(model);
const service=createTurnServer({name:'http',token:'test-token',provider:httpProvider({endpoint:modelUrl,model:'fixture'})});
try {
  const url=await listen(service);
  const child=spawn(process.argv[2],[url+'/v1/turn'],{stdio:'inherit'});
  const [code]=await once(child,'exit');
  process.exitCode=code ?? 1;
} finally {
  service.closeAllConnections();service.close();
  model.closeAllConnections();model.close();
}
