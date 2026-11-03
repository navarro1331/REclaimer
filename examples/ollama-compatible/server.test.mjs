import assert from 'node:assert/strict';
import { once } from 'node:events';
import { request } from 'node:http';
import test from 'node:test';
import { createServer } from './server.mjs';

// These synthetic backend fixtures test HTTP translation, not model quality or
// inference parity. Public contracts: https://docs.ollama.com/api/generate,
// https://docs.ollama.com/api/chat, and https://docs.ollama.com/api/embed.
const MODEL = Object.freeze({
  name: 'fixture:latest', modelPath: 'C:/synthetic/private/model.gguf',
  digest: 'sha256:0123456789abcdef', size: 1234,
  modified_at: '2026-01-01T00:00:00.000Z',
  details: { format: 'gguf', family: 'llama', families: ['llama'], parameter_size: '1B', quantization_level: 'Q4_K_M' },
  template: '', system: '', parameters: {}, licenses: ['Synthetic fixture'],
  capabilities: ['completion', 'embedding'],
});
const TIMINGS = { prompt_n: 7, prompt_ms: 1.25, predicted_n: 3, predicted_ms: 2.5 };

function jsonResponse(value, status = 200) {
  return new Response(JSON.stringify(value), { status, headers: { 'content-type': 'application/json' } });
}

function sseResponse(events, { finalMarker = false, crlf = false } = {}) {
  const newline = crlf ? '\r\n' : '\n';
  const serialized = events.map(value => `data: ${JSON.stringify(value)}${newline}${newline}`).join('')
    + (finalMarker ? `data: [DONE]${newline}${newline}` : '');
  const bytes = new TextEncoder().encode(serialized);
  // A network read can split the UTF-8 encoding of one character, an SSE field,
  // or the blank line separating events. All three happen in this fixture.
  return new Response(new ReadableStream({
    start(controller) {
      for (let offset = 0; offset < bytes.length; offset += 2) controller.enqueue(bytes.slice(offset, offset + 2));
      controller.close();
    },
  }), { headers: { 'content-type': 'text/event-stream' } });
}

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}

async function fixture(t, respond = () => jsonResponse({ content: 'Hello', stop: true, timings: TIMINGS }), serverOptions = {}) {
  const calls = [];
  const leases = [];
  const registry = {
    async list() { return [MODEL]; },
    async resolve(name) {
      if (name === MODEL.name || name === 'fixture') return MODEL;
      const error = new Error(`model '${name}' not found`);
      error.statusCode = 404;
      throw error;
    },
  };
  const runner = {
    async withModel(model, options, operation) {
      leases.push({ model, options });
      return operation({
        loadDuration: 3456000,
        async request(path, body, requestOptions = {}) {
          calls.push({ path, body, options: requestOptions });
          return respond(path, body, requestOptions);
        },
      });
    },
    loaded() { return null; },
    async unload() {},
    async close() {},
  };
  const server = createServer({ registry, runner, version: '0.1.0-contract-test', ...serverOptions });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(async () => {
    server.closeAllConnections();
    await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  });
  const origin = `http://127.0.0.1:${server.address().port}`;
  const post = (path, body, options = {}) => fetch(origin + path, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body), ...options,
  });
  return { origin, post, calls, leases, runner, registry };
}

function assertTimestamp(value) {
  assert.equal(typeof value, 'string');
  assert.ok(Number.isFinite(Date.parse(value)), `invalid timestamp: ${value}`);
}

function assertStatistics(body) {
  assert.equal(body.load_duration, 3456000);
  assert.equal(body.prompt_eval_count, 7);
  assert.equal(body.eval_count, 3);
  assert.equal(body.prompt_eval_duration, 1250000);
  assert.equal(body.eval_duration, 2500000);
  assert.ok(Number.isSafeInteger(body.total_duration) && body.total_duration >= 0);
}

test('discovery exposes Ollama model metadata without leaking the local model path', async t => {
  const f = await fixture(t);
  const health = await fetch(f.origin + '/');
  assert.equal(health.status, 200);
  assert.match(await health.text(), /running/i);
  assert.deepEqual(await (await fetch(f.origin + '/api/version')).json(), { version: '0.1.0-contract-test' });
  const tags = await (await fetch(f.origin + '/api/tags')).json();
  assert.equal(tags.models.length, 1);
  assert.equal(tags.models[0].name, MODEL.name);
  assert.equal(tags.models[0].size, MODEL.size);
  assert.equal(tags.models[0].digest, MODEL.digest);
  assert.deepEqual(tags.models[0].details, MODEL.details);
  assert.ok(!JSON.stringify(tags).includes(MODEL.modelPath));
  assert.deepEqual(await (await fetch(f.origin + '/api/ps')).json(), { models: [] });
  assert.equal(f.calls.length, 0);
});

test('matching an untrusted Host and Origin cannot bypass the loopback restriction', async t => {
  const f = await fixture(t);
  // Use node:http to transmit the exact Host header: fetch implementations may
  // normalize or ignore this header, which would weaken this regression test.
  const response = await new Promise((resolve, reject) => {
    const req = request(f.origin + '/api/version', {
      headers: { host: 'evil.test', origin: 'http://evil.test' },
    }, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', chunk => { body += chunk; });
      res.on('end', () => resolve({ status: res.statusCode, body: JSON.parse(body) }));
      res.on('error', reject);
    });
    req.on('error', reject);
    req.end();
  });
  assert.equal(response.status, 403);
  assert.match(response.body.error, /host/i);
  assert.equal(f.calls.length, 0);
});

test('an unrelated browser Origin is rejected even with a valid loopback Host', async t => {
  const f = await fixture(t);
  const response = await fetch(f.origin + '/api/version', { headers: { origin: 'http://evil.test' } });
  assert.equal(response.status, 403);
  assert.match((await response.json()).error, /origin/i);
  assert.equal(f.calls.length, 0);
});

test('show resolves a model alias and unknown models return a structured 404', async t => {
  const f = await fixture(t);
  const shown = await f.post('/api/show', { model: 'fixture' });
  assert.equal(shown.status, 200);
  const body = await shown.json();
  assert.deepEqual(body.details, MODEL.details);
  assert.deepEqual(body.capabilities, MODEL.capabilities);
  assert.ok(!JSON.stringify(body).includes(MODEL.modelPath));
  const missing = await f.post('/api/generate', { model: 'missing', prompt: 'Hello', stream: false });
  assert.equal(missing.status, 404);
  assert.equal(typeof (await missing.json()).error, 'string');
  assert.equal(f.calls.length, 0);
});

test('nonstream generation maps text, token counts, millisecond timings and runtime options', async t => {
  const f = await fixture(t, () => jsonResponse({
    content: 'Hello café 🌍', stop: true, stopped_eos: true,
    tokens_evaluated: 7, tokens_predicted: 3, timings: TIMINGS,
  }));
  const response = await f.post('/api/generate', {
    model: 'fixture', prompt: 'Say hello', raw: true, stream: false, keep_alive: '2m',
    options: { num_predict: 4, temperature: 0, top_k: 10, top_p: 0.8, min_p: 0.05, seed: 42, repeat_penalty: 1.1, stop: ['END'] },
  });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.response, 'Hello café 🌍');
  assert.equal(body.done, true);
  assert.equal(body.done_reason, 'stop');
  assertTimestamp(body.created_at);
  assertStatistics(body);
  assert.equal(f.calls[0].path, '/completion');
  assert.equal(f.calls[0].body.prompt, 'Say hello');
  assert.equal(f.calls[0].body.n_predict, 4);
  assert.equal(f.calls[0].body.temperature, 0);
  assert.equal(f.calls[0].body.seed, 42);
  assert.deepEqual(f.calls[0].body.stop, ['END']);
  assert.equal(f.leases[0].options.keepAlive, '2m');
});

test('generation streams by default, preserves split UTF-8 and emits one final record', async t => {
  const f = await fixture(t, () => sseResponse([
    { content: 'café ', stop: false },
    { content: '🌍', stop: false },
    { content: '', stop: true, stopped_eos: true, timings: TIMINGS },
  ], { crlf: true }));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true });
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type'), /application\/x-ndjson/);
  const records = (await response.text()).trim().split('\n').map(line => JSON.parse(line));
  assert.equal(records.map(item => item.response ?? '').join(''), 'café 🌍');
  assert.equal(records.filter(item => item.done === true).length, 1);
  assert.equal(records.at(-1).done, true);
  assertStatistics(records.at(-1));
  for (const record of records) assertTimestamp(record.created_at);
});

test('default generation delegates blank-template prompt formatting to the GGUF chat template', async t => {
  const f = await fixture(t, () => jsonResponse({
    choices: [{ index: 0, message: { role: 'assistant', content: 'Four.' }, finish_reason: 'stop' }],
    usage: { prompt_tokens: 7, completion_tokens: 3, total_tokens: 10 }, timings: TIMINGS,
  }));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Two plus two?', system: 'Be concise.', stream: false });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.response, 'Four.');
  assert.equal(body.message, undefined);
  assert.equal(body.done, true);
  assertStatistics(body);
  assert.equal(f.calls[0].path, '/v1/chat/completions');
  assert.deepEqual(f.calls[0].body.messages, [
    { role: 'system', content: 'Be concise.' },
    { role: 'user', content: 'Two plus two?' },
  ]);
  assert.equal(f.calls[0].body.prompt, undefined);
});

test('simple Ollama Go templates preserve formatting through the native completion endpoint', async t => {
  const f = await fixture(t);
  f.registry.resolve = async () => ({
    ...MODEL, system: 'Be concise.', template: 'SYSTEM: {{ .System }}\nUSER: {{ .Prompt }}\nASSISTANT: {{ .Response }}',
  });
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Two plus two?', stream: false });
  assert.equal(response.status, 200);
  assert.equal((await response.json()).response, 'Hello');
  assert.equal(f.calls[0].path, '/completion');
  assert.equal(f.calls[0].body.prompt, 'SYSTEM: Be concise.\nUSER: Two plus two?\nASSISTANT: ');
  assert.equal(f.calls[0].body.messages, undefined);
  const rawResponse = await f.post('/api/generate', { model: 'fixture', prompt: 'Already formatted', raw: true, stream: false });
  assert.equal(rawResponse.status, 200);
  assert.equal(f.calls[1].path, '/completion');
  assert.equal(f.calls[1].body.prompt, 'Already formatted');
});

test('cached prompt counts include reused tokens without fabricating evaluation duration', async t => {
  const f = await fixture(t, () => jsonResponse({
    content: 'Hello', stop: true, stopped_eos: true,
    timings: { ...TIMINGS, cache_n: 11 },
  }));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.prompt_eval_count, 18);
  assert.equal(body.prompt_eval_cached_count, 11);
  assert.equal(body.prompt_eval_duration, 1250000);
  assert.equal(body.eval_count, 3);
});

test('nonstream chat preserves the conversation and translates OpenAI completion output', async t => {
  const f = await fixture(t, () => jsonResponse({
    choices: [{ index: 0, message: { role: 'assistant', content: 'Four.' }, finish_reason: 'length' }],
    usage: { prompt_tokens: 7, completion_tokens: 3, total_tokens: 10 }, timings: TIMINGS,
  }));
  const messages = [{ role: 'system', content: 'Be concise.' }, { role: 'user', content: 'Two plus two?' }];
  const response = await f.post('/api/chat', { model: 'fixture', messages, stream: false, options: { num_predict: 3 } });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.deepEqual(body.message, { role: 'assistant', content: 'Four.' });
  assert.equal(body.done_reason, 'length');
  assertStatistics(body);
  assert.equal(f.calls[0].path, '/v1/chat/completions');
  assert.deepEqual(f.calls[0].body.messages, messages);
  assert.equal(f.calls[0].body.max_tokens, 3);
});

test('chat streams translate deltas and carry usage only into the final native record', async t => {
  const f = await fixture(t, () => sseResponse([
    { choices: [{ index: 0, delta: { role: 'assistant', content: 'hé' }, finish_reason: null }] },
    { choices: [{ index: 0, delta: { content: 'llo 🌍' }, finish_reason: null }] },
    { choices: [{ index: 0, delta: {}, finish_reason: 'stop' }] },
    { choices: [], usage: { prompt_tokens: 7, completion_tokens: 3, total_tokens: 10 }, timings: TIMINGS },
  ], { finalMarker: true }));
  const response = await f.post('/api/chat', { model: 'fixture', messages: [{ role: 'user', content: 'Hello' }] });
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type'), /application\/x-ndjson/);
  const records = (await response.text()).trim().split('\n').map(line => JSON.parse(line));
  assert.equal(records.map(item => item.message?.content ?? '').join(''), 'héllo 🌍');
  assert.equal(records.filter(item => item.done === true).length, 1);
  assert.equal(records.at(-1).done, true);
  assertStatistics(records.at(-1));
});

test('embedding batches preserve input order and normalize OpenAI data into native vectors', async t => {
  const f = await fixture(t, () => jsonResponse({
    data: [{ index: 1, embedding: [0, 1] }, { index: 0, embedding: [1, 0] }],
    usage: { prompt_tokens: 7, total_tokens: 7 },
  }));
  const response = await f.post('/api/embed', { model: 'fixture', input: ['first', 'second'], truncate: false });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.deepEqual(body.embeddings, [[1, 0], [0, 1]]);
  assert.equal(body.prompt_eval_count, 7);
  assert.equal(body.load_duration, 3456000);
  assert.equal(f.calls[0].path, '/v1/embeddings');
  assert.deepEqual(f.calls[0].body.input, ['first', 'second']);
  assert.equal(f.calls[0].body.encoding_format, 'float');
});

test('JSON output format is explicitly forwarded to the inference backend', async t => {
  const f = await fixture(t, () => jsonResponse({ content: '{"answer":4}', stop: true, timings: TIMINGS }));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Return JSON', raw: true, stream: false, format: 'json' });
  assert.equal(response.status, 200);
  assert.deepEqual(JSON.parse((await response.json()).response), { answer: 4 });
  assert.ok(f.calls[0].body.json_schema || f.calls[0].body.response_format);
});

test('malformed JSON and invalid native request shapes fail before starting inference', async t => {
  const f = await fixture(t);
  const malformed = await fetch(f.origin + '/api/generate', { method: 'POST', headers: { 'content-type': 'application/json' }, body: '{' });
  assert.equal(malformed.status, 400);
  assert.equal(typeof (await malformed.json()).error, 'string');
  for (const [path, body] of [
    ['/api/generate', { prompt: 'Hello' }],
    ['/api/generate', { model: 'fixture', prompt: 5 }],
    ['/api/generate', { model: 'fixture', stream: 'false' }],
    ['/api/generate', { model: 'fixture', options: [] }],
    ['/api/chat', { model: 'fixture', messages: 'Hello' }],
    ['/api/chat', { model: 'fixture', messages: [{ role: 'user', content: 2 }] }],
    ['/api/embed', { model: 'fixture', input: ['hello', 42] }],
  ]) {
    const response = await f.post(path, body);
    assert.equal(response.status, 400, `${path} accepted ${JSON.stringify(body)}`);
    assert.equal(typeof (await response.json()).error, 'string');
  }
  assert.equal(f.calls.length, 0);
});

test('unsupported features and runtime options are refused rather than silently dropped', async t => {
  const f = await fixture(t);
  for (const body of [
    { images: ['aGVsbG8='] }, { suffix: 'suffix' }, { context: [1, 2] },
    { template: '{{ .Prompt }}' }, { think: false }, { options: { numa: true } },
  ]) {
    const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', stream: false, ...body });
    assert.equal(response.status, 501, `silently accepted ${JSON.stringify(body)}`);
    assert.equal(typeof (await response.json()).error, 'string');
  }
  const tools = await f.post('/api/chat', { model: 'fixture', messages: [{ role: 'user', content: 'Hello' }], tools: [{ type: 'function', function: { name: 'lookup', parameters: {} } }] });
  assert.equal(tools.status, 501);
  const imageMessage = await f.post('/api/chat', { model: 'fixture', messages: [{ role: 'user', content: 'Describe this', images: ['aGVsbG8='] }] });
  assert.equal(imageMessage.status, 501);
  const dimensions = await f.post('/api/embed', { model: 'fixture', input: 'Hello', dimensions: 12 });
  assert.equal(dimensions.status, 501);
  assert.equal(f.calls.length, 0);
});

test('model download and mutation routes are explicit unsupported operations', async t => {
  const f = await fixture(t);
  for (const path of ['/api/pull', '/api/push', '/api/create', '/api/copy']) {
    const response = await f.post(path, { model: 'fixture', name: 'fixture' });
    assert.equal(response.status, 501, path);
    assert.equal(typeof (await response.json()).error, 'string');
  }
  assert.equal((await fetch(f.origin + '/not-a-route')).status, 404);
  assert.equal(f.calls.length, 0);
});

test('requests exceeding the 1 MiB body limit receive a structured 413', { timeout: 5000 }, async t => {
  const f = await fixture(t);
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'x'.repeat(1024 * 1024 + 1), stream: false });
  assert.equal(response.status, 413);
  assert.equal(typeof (await response.json()).error, 'string');
  assert.equal(f.calls.length, 0);
});

test('a backend HTTP failure becomes a structured gateway failure', async t => {
  const f = await fixture(t, () => jsonResponse({ error: { message: 'synthetic inference failure' } }, 500));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false });
  assert.equal(response.status, 502);
  const body = await response.json();
  assert.equal(typeof body.error, 'string');
  assert.equal(body.done, undefined);
});

test('backend connection failures return a structured gateway error without private diagnostics', async t => {
  const privateDiagnostic = `connect ECONNREFUSED 127.0.0.1:45678 while loading ${MODEL.modelPath}`;
  const f = await fixture(t, () => { throw new TypeError(privateDiagnostic); });
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false });
  assert.equal(response.status, 502);
  const body = await response.json();
  assert.equal(typeof body.error, 'string');
  assert.match(body.error, /backend/i);
  for (const secretDetail of ['ECONNREFUSED', '127.0.0.1:45678', MODEL.modelPath]) {
    assert.equal(JSON.stringify(body).includes(secretDetail), false);
  }
  assert.equal(body.done, undefined);
});

test('empty prompts load or unload a model without fabricating generated output', async t => {
  const f = await fixture(t);
  const unloaded = [];
  f.runner.unload = async name => { unloaded.push(name); };
  const loaded = await f.post('/api/generate', { model: 'fixture', stream: false });
  assert.equal(loaded.status, 200);
  const loadBody = await loaded.json();
  assert.equal(loadBody.done, true);
  assert.equal(loadBody.done_reason, 'load');
  assert.equal(loadBody.response, '');
  assert.equal(f.leases.length, 1);
  const unloadedResponse = await f.post('/api/generate', { model: 'fixture', keep_alive: 0, stream: false });
  assert.equal(unloadedResponse.status, 200);
  const unloadBody = await unloadedResponse.json();
  assert.equal(unloadBody.done_reason, 'unload');
  assert.equal(unloadBody.response, '');
  assert.deepEqual(unloaded, [MODEL.name]);
  assert.equal(f.leases.length, 1);
  assert.equal(f.calls.length, 0);
});

test('the request deadline cancels stalled inference and returns a structured timeout', { timeout: 5000 }, async t => {
  const f = await fixture(t, (_path, _body, { signal }) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(signal.reason), { once: true });
  }), { requestTimeoutMs: 50 });
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false });
  assert.equal(response.status, 504);
  assert.match((await response.json()).error, /timed out/i);
  assert.equal(f.calls[0].options.signal.aborted, true);
});

test('a backend error inside an established stream is not reported as successful completion', async t => {
  const f = await fixture(t, () => sseResponse([
    { content: 'partial', stop: false },
    { error: { message: 'synthetic stream failure' } },
  ]));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true });
  const records = (await response.text()).trim().split('\n').map(line => JSON.parse(line));
  assert.equal(records[0].response, 'partial');
  assert.equal(typeof records.at(-1).error, 'string');
  assert.equal(records.some(item => item.done === true), false);
});

test('an incomplete backend stream cannot manufacture a successful final record', async t => {
  const f = await fixture(t, () => sseResponse([{ content: 'partial', stop: false }]));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true });
  const records = (await response.text()).trim().split('\n').map(line => JSON.parse(line));
  assert.equal(records[0].response, 'partial');
  assert.equal(typeof records.at(-1).error, 'string');
  assert.equal(records.some(item => item.done === true), false);
});

test('generation does not invent token counts or evaluation timings absent from backend output', async t => {
  const f = await fixture(t, () => jsonResponse({ content: 'Hello', stop: true, stopped_eos: true }));
  const response = await f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.response, 'Hello');
  assert.equal(body.done, true);
  for (const field of ['prompt_eval_count', 'eval_count', 'prompt_eval_duration', 'eval_duration']) {
    assert.equal(Object.hasOwn(body, field), false, `invented ${field}`);
  }
});

test('disconnecting a client aborts its in-flight backend operation', { timeout: 5000 }, async t => {
  const started = deferred();
  const cancelled = deferred();
  const f = await fixture(t, (_path, _body, { signal }) => new Promise((resolve, reject) => {
    assert.ok(signal instanceof AbortSignal);
    started.resolve();
    signal.addEventListener('abort', () => {
      cancelled.resolve();
      reject(new DOMException('Client disconnected', 'AbortError'));
    }, { once: true });
  }));
  const controller = new AbortController();
  t.after(() => controller.abort());
  const pending = f.post('/api/generate', { model: 'fixture', prompt: 'Hello', raw: true, stream: false }, { signal: controller.signal });
  await started.promise;
  controller.abort();
  await assert.rejects(pending, { name: 'AbortError' });
  await cancelled.promise;
});
