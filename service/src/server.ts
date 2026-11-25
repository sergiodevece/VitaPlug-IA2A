import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { claudeProvider, demoProvider, httpProvider } from './provider.ts';
import { createTurnServer } from './turn.ts';

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

const port=Number(env.IA2A_PORT || 48220);
const openLogic=process.argv.includes('--open-logic');
const token=env.IA2A_SERVICE_TOKEN || (openLogic?randomBytes(32).toString('hex'):'');
const server=createTurnServer({token,name:configured,provider:selectProvider()});
server.listen(port,'127.0.0.1',()=>{
  const endpoint='http://127.0.0.1:'+server.address().port;
  console.log('IA2A en '+endpoint+' · '+configured);
  if(!openLogic) return;

  // Logic receives the local endpoint and its temporary token. The model key stays in this process.
  const appEnv={...process.env};
  delete appEnv.ANTHROPIC_API_KEY;
  delete appEnv.IA2A_MODEL_KEY;
  const open=spawn('/usr/bin/open',[
    '--env','IA2A_TURN_URL='+endpoint+'/v1/turn',
    '--env','IA2A_SERVICE_TOKEN='+token,'-b','com.apple.logic10'
  ],{env:appEnv,stdio:'inherit'});
  const fail=()=>{
    console.error('No se pudo abrir Logic Pro.');
    process.exitCode=1;
    server.close();
  };
  open.once('error',fail);
  open.once('exit',code=>{if(code!==0)fail();});
});
