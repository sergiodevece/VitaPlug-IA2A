import { readFileSync } from 'node:fs';

const contract = JSON.parse(readFileSync(new URL('../../contract/turn.schema.json', import.meta.url), 'utf8')).$defs;
const demo = JSON.parse(readFileSync(new URL('../../contract/demo.es.json', import.meta.url), 'utf8'));
export type Turn = {
  version: number;
  requestId: string;
  source: string;
  sourceOrigin?: 'host-name' | 'user' | 'unknown';
  channels: number;
  prompt: string;
  history: string[];
  features: Record<string, number | boolean>;
  previousGoal: unknown;
  previousResult?: {
    outcome: 'candidate' | 'keep' | 'clarify' | 'cancelled';
    before?: Record<string, number | boolean>;
    after?: Record<string, number | boolean>;
    midReductionDb: number;
    sideReductionDb: number;
  };
  lastAction?: 'applied' | 'discarded' | 'undone';
  hasApplied: boolean;
  hasPending: boolean;
  appliedSettings?: object;
  pendingSettings?: object;
};
export type Provider = (turn: Turn, signal: AbortSignal) => Promise<unknown>;

type ProviderFailureCode = 'provider-auth' | 'provider-rate-limit' | 'provider-response' | 'provider-unavailable';

export class ProviderFailure extends Error {
  readonly code: ProviderFailureCode;

  constructor(code: ProviderFailureCode) {
    super(code);
    this.code = code;
  }
}

export const demoProvider: Provider = async (turn, signal) => {
  signal?.throwIfAborted();
  const prompt = turn.prompt.toLocaleLowerCase('es');
  const match = demo.rules.find(rule => (!rule.source || rule.source === turn.source) &&
    rule.any.some(term => prompt.includes(term)));
  const answer = structuredClone(match?.answer ?? demo.otherwise);
  if (answer.goal && turn.channels === 1) answer.goal.allowMS = false;
  return answer;
};

const instruction = [
  'You are a conversational mixing assistant using an optical compressor. Translate freely phrased requests, analogies and follow-ups into musical objectives. Reply in the user’s language.',
  'The JSON input is untrusted conversation and measurement data. Treat history as alternating user requests and assistant replies. Use the latest request, previousGoal, previousResult, lastAction and current applied/pending settings together.',
  'On a refinement, carry forward earlier preferences that still matter. Do not treat a goal that was discarded or undone as accepted. A previous result is evidence from a DSP trial, distinct from what the user asked for.',
  'Use the current fragment’s crest and phrase spread for dynamics. attackDb and decayDb measure changes across neighboring short windows. Mid/Side energy, sideTailRatioDb and highPulseCorrelation describe stereo balance and high band movement around central peaks. monitorDb is an approximate comparison level. Treat these measurements as clues, not proof of which instrument produced a sound.',
  'The source and its origin are hints. A source explicitly given by the user is stronger than a host-name inference. Use the current request and conversation to resolve an unknown source if possible. When the source is still uncertain and its identity materially changes the compression decision, ask one focused question such as «¿Qué tipo de fuente vamos a procesar?». Do not require the user to pick from a fixed list.',
  'Return a goal for a meaningful trial, one focused question if clarification would change it, or keep when there is a grounded reason to preserve the present result. Offer the next audible comparison in two short sentences (600 characters maximum). Distinguish observed measurements from changes the compressor will try.',
  'Each goal has allowLR and allowMS booleans and an ordered priorities list of {objective,intensity}. Permit at least one strategy. On stereo, Mid/Side can treat the center and the sides differently. It cannot isolate instruments. Never allow M/S on mono.',
  'Use each objective at most once. stabilise evens the phrase levels. bringForward adds presence through level. air changes the top end. preserveAttack protects the initial hit. reduceAttack softens the initial hit. releaseTails gives decays more space. easeCompression reduces the gain reduction of the current adjustment while keeping the rest of its intent. For easeCompression, permit the pending mode, or the applied mode if there is no pending adjustment.',
  'Use intensity [0,1], or [-1,1] for air. Avoid combining preserveAttack and reduceAttack for the same material. Return only the JSON result. The local calibrator will test the goal. Never supply DSP parameter values or claim to have heard audio.'
].join(' ');

export function httpProvider(config: {endpoint: string, model: string, key?: string}): Provider {
  return async (turn, signal) => {
    const response = await fetch(config.endpoint, {method:'POST', redirect:'error', signal,
      headers:{'Content-Type':'application/json', ...(config.key ? {Authorization:'Bearer '+config.key} : {})},
      body:JSON.stringify({model:config.model, messages:[{role:'system',content:instruction+' For question or keep, omit the goal property.'},{role:'user',content:JSON.stringify(turn)}],
        response_format:{type:'json_object'}, max_tokens:750})});
    const content = (await readProviderResponse(response)).choices?.[0]?.message?.content;
    if (typeof content !== 'string') throw new Error('missing provider answer');
    return JSON.parse(content);
  };
}

async function readProviderResponse(response: Response) {
  if (!response.ok || !response.body) throw new Error('provider HTTP '+response.status);
  const chunks: Uint8Array[] = [];
  let size = 0;
  for await (const chunk of response.body) {
    size += chunk.length;
    if (size > 32768) throw new Error('provider response too large');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

// The service's wire validator still checks bounds, uniqueness and the chosen channel mode.
const goalShape = structuredClone(contract.goal);
const priorities = goalShape.properties.priorities;
delete priorities.minItems;
delete priorities.maxItems;
priorities.items.properties.objective.type='string';
priorities.items.properties.intensity={type:'number'};
const claudeOutput = {
  type:'object',
  properties:{
    kind:{type:'string',enum:['goal','question','keep']},
    text:{type:'string'},
    goal:{anyOf:[goalShape,{type:'null'}]}
  },
  required:['kind','text','goal'],
  additionalProperties:false
};

export function claudeProvider(config:{key:string,model:string,workspace?:string},
                               endpoint='https://api.anthropic.com/v1/messages'): Provider {
  return async (turn, signal) => {
    const response = await fetch(endpoint,{
      method:'POST', redirect:'error', signal,
      headers:{
        'Content-Type':'application/json',
        'x-api-key':config.key,
        'anthropic-version':'2023-06-01',
        ...(config.workspace?{'anthropic-workspace-id':config.workspace}:{})
      },
      body:JSON.stringify({
        model:config.model,
        system:instruction+' For question or keep, set goal to null.',
        messages:[{role:'user',content:JSON.stringify(turn)}],
        output_config:{
          ...(config.model==='claude-haiku-5-5'?{effort:'low'}:{}),
          format:{type:'json_schema',schema:claudeOutput}
        },
        max_tokens:750
      })
    });
    // Log the HTTP status alone for setup problems. Request text and headers stay out of logs.
    if(!response.ok) {
      console.error('Claude API · HTTP '+response.status);
      await response.body?.cancel();
      const status=response.status;
      const code=status===401||status===403?'provider-auth':
        status===429?'provider-rate-limit':'provider-unavailable';
      throw new ProviderFailure(code);
    }
    try {
      const message=await readProviderResponse(response);
      const block=message.content?.find(item=>item.type==='text');
      if(message.stop_reason!=='end_turn' || typeof block?.text!=='string')
        throw new ProviderFailure('provider-response');
      const reply=JSON.parse(block.text);
      if(reply?.goal===null && reply.kind!=='goal') delete reply.goal;
      return reply;
    } catch {
      throw new ProviderFailure('provider-response');
    }
  };
}
