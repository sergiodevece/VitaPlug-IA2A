import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { timingSafeEqual } from 'node:crypto';
import { ProviderFailure, type Provider, type Turn } from './provider.ts';

const defs = JSON.parse(readFileSync(new URL('../../contract/turn.schema.json', import.meta.url), 'utf8')).$defs;

// Interpret the vocabulary used by the shared versioned contract.
function matches(rule, value: unknown): boolean {
  if (rule.$ref) return matches(defs[rule.$ref.split('/').at(-1)],value);
  if (rule.oneOf) return rule.oneOf.filter(option => matches(option,value)).length===1;
  if (rule.anyOf) return rule.anyOf.some(option => matches(option,value));
  if ('const' in rule) return value === rule.const;
  if (rule.enum) return rule.enum.includes(value);
  switch(rule.type) {
    case 'null': return value === null;
    case 'boolean': return typeof value === 'boolean';
    case 'number': return typeof value === 'number' && Number.isFinite(value) && value>=rule.minimum && value<=rule.maximum;
    case 'string': return typeof value==='string' && !!value.trim() && (rule.minLength===undefined||[...value].length>=rule.minLength) &&
      (rule.maxLength===undefined||[...value].length<=rule.maxLength) && (!rule.pattern||new RegExp(rule.pattern).test(value));
    case 'array': return Array.isArray(value) && value.length>=rule.minItems && value.length<=rule.maxItems && value.every(v=>matches(rule.items,v));
    case 'object': return typeof value==='object' && value!==null && !Array.isArray(value) &&
      rule.required.every(key => Object.hasOwn(value,key)) &&
      Object.entries(value).every(([key,v])=>Object.hasOwn(rule.properties,key) && matches(rule.properties[key],v));
    default: return false;
  }
}
function admissible(goal, channels:number) {
  const edits=goal.edits ?? [];
  return (goal.priorities.length>0 || edits.length>0) &&
    edits.every(edit => {
      const global=['inputTrimDb','outputGainDb','enabled','midSide'].includes(edit.control);
      if(global && edit.domain!=='both')return false;
      const toggle=['hpfOn','enabled','midSide'].includes(edit.control);
      if(toggle)return edit.change==='set' && (edit.value===0 || edit.value===1);
      return edit.change==='set' ? edit.value!==undefined : edit.value===undefined || edit.value>0;
    }) &&
    (goal.allowLR || goal.allowMS) && !(goal.allowMS && channels===1) &&
    new Set(goal.priorities.map(p=>p.objective)).size === goal.priorities.length &&
    goal.priorities.every(p=>p.objective==='air' || p.intensity>=0);
}
export function validRequest(input): input is Turn {
  return matches(defs.request,input) && (!input.previousGoal || admissible(input.previousGoal,2)) &&
    (input.auditioning!=='pending' || input.hasPending);
}
export function validResponse(reply, turn:Turn) {
  return matches(defs.response,reply) && reply.requestId===turn.requestId &&
    (reply.kind!=='goal' || admissible(reply.goal,turn.channels));
}
function authorised(value:string, token:string) {
  const actual=Buffer.from(value), expected=Buffer.from('Bearer '+token);
  return !token || (actual.length===expected.length && timingSafeEqual(actual,expected));
}

export function createTurnServer(config:{token:string, name:'local-demo'|'http'|'claude', provider:Provider}) {
  let active=0;
  const server=createServer(async (req,res)=>{
    const finish=(code:number, data:unknown={})=> {
      if (!res.destroyed && !res.writableEnded) {res.writeHead(code,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(data));}
    };
    const fail=(status:number, code:string) => {
      if (status>=500 || code==='invalid-turn') console.warn('IA2A · '+status+' · '+code);
      return finish(status,{code});
    };
    if(req.url!=='/v1/turn'||req.method!=='POST')return finish(404);
    if(!authorised(req.headers.authorization||'',config.token))return finish(401);
    if(req.headers['content-type']?.split(';')[0].trim()!=='application/json')return finish(415);
    if(active>=4)return finish(429);
    if(Number(req.headers['content-length'])>16384)return finish(413);
    ++active;
    const abort=new AbortController();
    res.once('close',()=>abort.abort());
    let expired=false;
    const timer=setTimeout(()=>{expired=true;abort.abort();fail(504,'provider-timeout');},25000);
    try {
      const chunks:Buffer[]=[];let size=0;
      for await(const chunk of req) {
        size+=chunk.length;
        if(size>16384)return finish(413);
        chunks.push(chunk);
      }
      if (expired || abort.signal.aborted) return;
      let turn;
      try {turn=JSON.parse(Buffer.concat(chunks).toString('utf8'));} catch {return fail(400,'invalid-turn');}
      if(!validRequest(turn))return fail(400,'invalid-turn');
      const result=await config.provider(turn,abort.signal);
      if(expired || abort.signal.aborted)return;
      if(!result || typeof result!=='object' || ['version','requestId','provider'].some(key=>Object.hasOwn(result,key)))return fail(502,'provider-response');
      const reply={...result,version:1,requestId:turn.requestId,provider:config.name};
      return validResponse(reply,turn)?finish(200,reply):fail(502,'provider-response');
    } catch (error) {
      if(!expired && !abort.signal.aborted)
        fail(502,error instanceof ProviderFailure?error.code:'provider-unavailable');
    }
    finally {clearTimeout(timer);--active;}
  });
  server.requestTimeout=12000;
  server.headersTimeout=10000;
  return server;
}
