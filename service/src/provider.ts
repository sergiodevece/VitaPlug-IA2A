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
  auditioning?: 'applied' | 'pending';
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
  'On a refinement, use auditioning to identify which settings the user is hearing. Carry forward earlier preferences that still matter. Do not treat a goal that was discarded or undone as accepted. A previous result is evidence from a DSP trial, distinct from what the user asked for.',
  'Use the current fragment’s crest and phrase spread for dynamics. attackDb and decayDb measure changes across neighboring short windows. Mid/Side energy, sideTailRatioDb and highPulseCorrelation describe stereo balance and high band movement around central peaks. monitorDb is an approximate comparison level. Treat these measurements as clues, not proof of which instrument produced a sound.',
  'The source and its origin are hints. A source explicitly given by the user is stronger than a host-name inference. Use the current request and conversation to resolve an unknown source if possible. When the source is still uncertain and its identity materially changes the compression decision, ask one focused question such as «¿Qué tipo de fuente vamos a procesar?». Do not require the user to pick from a fixed list.',
  'The user has final say. Plan an audition for every clear request to change the sound. Use keep only when the user asks to keep it. Never use keep merely because you think the present result is close. Ask one focused question only when an essential part of the request is unclear. In text, describe what will be auditioned in two short sentences (600 characters maximum). Never say you applied anything.',
  'A request to change a named compressor control is a direct instruction. Represent it with goal.edits, even if an earlier audition was already close. Each edit has control, domain both/main/side, change set/increase/decrease, and an optional value. Set uses the signed absolute value in the control’s units. Increase and decrease use a positive magnitude. If the user gives no amount, omit the value so the local plugin chooses a useful step.',
  'IA2A signal path: global input trim feeds the optical detector and the program path. Threshold, ratio and knee control the amount and onset of gain reduction. Attack and fast/slow release set the envelope timing; memory blends the slower recovery. Makeup gain follows dynamics. AIR is a fixed 10 kHz high shelf with adjustable boost. Global output gain is the final level. The optional mix reference is for analysis, separate from the program and detector.',
  'The sidechain HPF (hpfOn + hpfHz) filters only the detector. Raising it can reduce gain reduction triggered by low-end peaks and retain perceived bass. It does not boost or filter the output bass as an EQ. For a qualitative request for more low-end weight, use bassWeight and use lowRatioDb (low-band energy relative to broadband) and transient measurements as evidence. For an explicitly requested detector switch or cutoff, edit hpfOn or hpfHz directly.',
  'Direct controls and usable ranges: thresholdDb -60..0 dB; ratio 1..20; attackMs 0.1..200 ms; fastReleaseMs 10..3000 ms; slowReleaseMs 100..15000 ms; memory 0..1; kneeDb 0..24 dB; hpfHz 20..500 Hz; airDb 0..10 dB; makeupDb, inputTrimDb and outputGainDb -24..24 dB. Read the actual values from appliedSettings/pendingSettings and choose their relevant bank. The plugin clamps explicit amounts to its control limits.',
  'Use set with value 0 or 1 for hpfOn and enabled: 0 is off, 1 is on. Use midSide set 0 for linked L/R and set 1 for stereo M/S. In mono and linked L/R the main bank governs processing. Stereo M/S has independent Mid and Side detector, dynamics, makeup, and AIR values. Use domain both for enabled, midSide, inputTrimDb and outputGainDb, which are global. Explicit requests for Mid or Side use main or side; Side edits in stereo can select M/S when needed. For a user explicitly asking for M/S on mono, pass the edit so the local plugin can explain that hardware limit. The allowMS parameter still governs the optimizer and must be false on mono.',
  'When asked for gain compensation or level balancing after compression, set compensateGain to true unless the user explicitly specifies an output gain. The local plugin measures the compensation and leaves the Apply decision to the user.',
  'Common relationships: a lower threshold or higher ratio can make compression more audible. Preserve attack or use a longer attack time for more punch when the transient is getting flattened, rather than assuming a level increase is needed. Fast/slow release, memory and detector filtering affect whether gain reduction follows a rhythmic pulse. Use the measured context and prior audition to decide which approach to try.',
  'Do not invent exact settings for a sonic description. Use musical priorities, or use a relative edit to carry out a clear directional request such as more compression. A goal requires at least one edit or priority. For a purely direct instruction use an empty priorities array.',
  'Each goal has allowLR and allowMS booleans and an ordered priorities list of {objective,intensity}. Permit at least one strategy. On stereo, Mid/Side can treat the center and the sides differently. It cannot isolate instruments. Never allow M/S on mono.',
  'Use each objective at most once. stabilise evens the phrase levels. bringForward adds presence through level. air changes the top end. preserveAttack protects the initial hit. reduceAttack softens the initial hit. releaseTails gives decays more space. bassWeight aims to preserve the weight of the low end by changing how low frequencies trigger compression. easeCompression reduces the gain reduction of the current adjustment while keeping the rest of its intent. For easeCompression, permit the pending mode, or the applied mode if there is no pending adjustment.',
  'Use intensity [0,1], or [-1,1] for air. Avoid combining preserveAttack and reduceAttack for the same material. Return only the JSON result. The local calibrator will test the goal. Never claim to have heard audio. Leave the Apply decision to the user.'
].join(' ');

export function httpProvider(config: {endpoint: string, model: string, key?: string}): Provider {
  return async (turn, signal) => {
    const response = await fetch(config.endpoint, {method:'POST', redirect:'error', signal,
      headers:{'Content-Type':'application/json', ...(config.key ? {Authorization:'Bearer '+config.key} : {})},
      body:JSON.stringify({model:config.model, messages:[{role:'system',content:instruction+' For question or keep, omit the goal property.'},{role:'user',content:JSON.stringify(turn)}],
        response_format:{type:'json_object'}, max_tokens:1100})});
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
const edits = goalShape.properties.edits;
delete edits.minItems;
delete edits.maxItems;
for (const key of ['control','domain','change']) edits.items.properties[key].type='string';
edits.items.properties.value={anyOf:[{type:'number'},{type:'null'}]};
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
        max_tokens:1100
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
      if(Array.isArray(reply?.goal?.edits)) {
        for(const edit of reply.goal.edits)
          if(edit?.value===null) delete edit.value;
      }
      return reply;
    } catch {
      throw new ProviderFailure('provider-response');
    }
  };
}
