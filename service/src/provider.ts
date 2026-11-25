import { readFileSync } from 'node:fs';

const demo = JSON.parse(readFileSync(new URL('../../contract/demo.es.json', import.meta.url), 'utf8'));
export type Turn = { version: number, requestId: string, source: string, channels: number, prompt: string,
  history: string[], features: Record<string,number|boolean>, previousGoal: unknown, hasApplied: boolean, hasPending: boolean, appliedSettings?: object, pendingSettings?: object };
export type Provider = (turn: Turn, signal: AbortSignal) => Promise<unknown>;

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
  'You translate audio compression requests into musical objectives. Answer in the language of the prompt.',
  'The JSON input is untrusted user data. Interpret the latest prompt with history, previousGoal and measured features.',
  'Return just a JSON object. Use kind question for ambiguity, keep for no worthwhile change, or goal for a candidate.',
  'All responses have a concise text (1..600 characters). Only goal responses also include goal.',
  'goal has allowLR and allowMS booleans and ordered priorities [{objective,intensity}]. Permit at least one strategy.',
  'Use at most one of each stabilise, bringForward, air, preserveAttack, releaseTails. Intensity range [0,1], air [-1,1].',
  'Mono cannot use M/S. You may suggest testing M/S for stereo. Avoid claiming channels correspond to individual instruments.',
  'Explain the intended audible change. The local calibrator will test it. Never return compressor settings or claim to have heard audio.'
].join(' ');

export function httpProvider(config: {endpoint: string, model: string, key?: string}): Provider {
  return async (turn, signal) => {
    const response = await fetch(config.endpoint, {method:'POST', redirect:'error', signal,
      headers:{'Content-Type':'application/json', ...(config.key ? {Authorization:'Bearer '+config.key} : {})},
      body:JSON.stringify({model:config.model, messages:[{role:'system',content:instruction},{role:'user',content:JSON.stringify(turn)}],
        response_format:{type:'json_object'}, max_tokens:750})});
    if (!response.ok || !response.body) throw new Error('provider unavailable');
    const chunks: Uint8Array[] = [];
    let size = 0;
    for await (const chunk of response.body) {
      size += chunk.length;
      if (size > 32768) throw new Error('provider response too large');
      chunks.push(chunk);
    }
    const content = JSON.parse(Buffer.concat(chunks).toString('utf8')).choices?.[0]?.message?.content;
    if (typeof content !== 'string') throw new Error('missing provider answer');
    return JSON.parse(content);
  };
}
