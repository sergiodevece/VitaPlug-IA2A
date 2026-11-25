import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { claudeProvider, demoProvider, httpProvider } from './provider.ts';
import { createTurnServer } from './turn.ts';
import { publishLocalConnection } from './connection.ts';

const env=process.env;
const configured=env.IA2A_PROVIDER || (env.IA2A_MODEL_URL?'http':'local-demo');
if(configured!=='claude' && configured!=='http' && configured!=='local-demo')
  throw new Error('Proveedor IA2A desconocido');

function selectProvider() {
  if(configured==='claude') {
    if(!env.ANTHROPIC_API_KEY) throw new Error('Introduce la clave API de Claude para iniciar el servicio.');
    return claudeProvider({
      key:env.ANTHROPIC_API_KEY, model:env.IA2A_MODEL || 'claude-haiku-5-5',
      workspace:env.ANTHROPIC_WORKSPACE_ID
    });
  }
  if(configured==='http') {
    if(!env.IA2A_MODEL_URL || !env.IA2A_MODEL) throw new Error('El proveedor HTTP necesita IA2A_MODEL_URL e IA2A_MODEL.');
    return httpProvider({endpoint:env.IA2A_MODEL_URL,model:env.IA2A_MODEL,key:env.IA2A_MODEL_KEY});
  }
  return demoProvider;
}

const openLogic=process.argv.includes('--open-logic');
const port=Number(env.IA2A_PORT || (openLogic?0:48220));
const token=openLogic?randomBytes(32).toString('hex'):(env.IA2A_SERVICE_TOKEN||'');
const server=createTurnServer({token,name:configured,provider:selectProvider()});
server.listen(port,'127.0.0.1',()=>{
  const localPort=server.address().port;
  console.log('IA2A en http://127.0.0.1:'+localPort+' · '+configured);
  if(!openLogic) return;

  const removeConnection=publishLocalConnection(localPort, token);
  process.once('exit',removeConnection);
  for (const signal of ['SIGINT','SIGTERM'] as const)
    process.once(signal,()=>process.exit(0));

  // An Audio Unit can find the running service even if Logic was already open.
  const appEnv={...process.env};
  delete appEnv.ANTHROPIC_API_KEY;
  delete appEnv.IA2A_MODEL_KEY;
  const open=spawn('/usr/bin/open',['-b','com.apple.logic10'],{env:appEnv,stdio:'inherit'});
  const report = () => console.error('Abre Logic Pro manualmente. El servicio IA2A está listo.');
  open.once('error',report);
  open.once('exit',code=>{if(code!==0)report();});
});
