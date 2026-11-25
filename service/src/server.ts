import { demoProvider, httpProvider } from './provider.ts';
import { createTurnServer } from './turn.ts';
const env=process.env;
const real=!!env.IA2A_MODEL_URL;
if(real && !env.IA2A_MODEL)throw new Error('IA2A_MODEL es obligatorio con IA2A_MODEL_URL');
const provider=real?httpProvider({endpoint:env.IA2A_MODEL_URL!,model:env.IA2A_MODEL!,key:env.IA2A_MODEL_KEY}):demoProvider;
const port=Number(env.IA2A_PORT || 48220);
const server=createTurnServer({token:env.IA2A_SERVICE_TOKEN||'',name:real?'http':'local-demo',provider});
server.listen(port,'127.0.0.1',()=>console.log('IA2A en 127.0.0.1:'+port+' · '+(real?'proveedor HTTP':'intérprete local de prueba')));
