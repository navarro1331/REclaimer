import http from 'node:http';
import { once } from 'node:events';

const BODY_LIMIT = 1024 * 1024;
const RESPONSE_LIMIT = 8 * 1024 * 1024;
const ns = () => process.hrtime.bigint();
const elapsed = start => Number(ns() - start);
const object = x => x !== null && typeof x === 'object' && !Array.isArray(x);
export const failure = (statusCode, message) => Object.assign(new Error(message), { statusCode });
const unsupported = message => failure(501, message);

function json(res, status, body) {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(body));
}

async function readBody(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req.iterator({ destroyOnReturn: false })) {
    size += chunk.length;
    if (size > BODY_LIMIT) { req.resume(); throw failure(413, 'request body exceeds 1 MiB'); }
    chunks.push(chunk);
  }
  let body;
  try { body = JSON.parse(Buffer.concat(chunks).toString('utf8')); }
  catch { throw failure(400, 'invalid JSON request body'); }
  if (!object(body)) throw failure(400, 'request body must be a JSON object');
  return body;
}

function fields(body, allowed) {
  for (const key of Object.keys(body)) {
    if (!allowed.includes(key)) throw unsupported(`request field '${key}' is not implemented`);
  }
  if (typeof body.model !== 'string' || !body.model.trim()) throw failure(400, 'model is required');
  if (body.stream !== undefined && typeof body.stream !== 'boolean') throw failure(400, 'stream must be a boolean');
  if (body.options !== undefined && !object(body.options)) throw failure(400, 'options must be an object');
}

const OPTION_NAMES = ['temperature', 'seed', 'top_k', 'top_p', 'min_p', 'repeat_penalty', 'num_predict', 'stop'];
function options(model, input = {}) {
  const value = { ...(model.parameters ?? {}), ...input };
  for (const [name, item] of Object.entries(value)) {
    if (!OPTION_NAMES.includes(name)) throw unsupported(`option '${name}' is not implemented`);
    if (name === 'stop') {
      if (!Array.isArray(item) || item.some(x => typeof x !== 'string')) throw failure(400, 'stop must be an array of strings');
    } else if (typeof item !== 'number' || !Number.isFinite(item)) throw failure(400, `option '${name}' must be a finite number`);
  }
  for (const name of ['seed', 'top_k', 'num_predict']) {
    if (value[name] !== undefined && !Number.isSafeInteger(value[name])) throw failure(400, `option '${name}' must be an integer`);
  }
  if (value.num_predict !== undefined && (value.num_predict < 1 || value.num_predict > 32768)) throw failure(400, 'num_predict must be between 1 and 32768');
  for (const name of ['top_p', 'min_p']) if (value[name] !== undefined && (value[name] < 0 || value[name] > 1)) throw failure(400, `${name} must be between 0 and 1`);
  if (value.temperature !== undefined && value.temperature < 0) throw failure(400, 'temperature must be nonnegative');
  const { num_predict = 256, ...rest } = value;
  return { ...rest, n_predict: num_predict };
}

function format(value, native) {
  if (value === undefined || value === '') return {};
  if (value !== 'json' && !object(value)) throw failure(400, 'format must be json or a JSON Schema object');
  return native ? { json_schema: value === 'json' ? {} : value }
    : { response_format: value === 'json' ? { type: 'json_object' } : { type: 'json_schema', json_schema: { name: 'response', schema: value } } };
}

function simpleTemplate(template) {
  return typeof template === 'string' && !template.replace(/{{\s*\.(System|Prompt|Response)\s*}}/g, '').includes('{{');
}

function render(template, values) {
  if (!simpleTemplate(template)) throw unsupported('this model uses an unsupported Go template; use raw:true for generate, or select a model with an embedded chat template');
  return template.replace(/{{\s*\.(System|Prompt|Response)\s*}}/g, (_, name) => values[name] ?? '');
}

function chatRequest(model, body) {
  if (!Array.isArray(body.messages)) throw failure(400, 'messages must be an array');
  for (const message of body.messages) {
    if (!object(message) || !['system', 'user', 'assistant'].includes(message.role) || typeof message.content !== 'string') throw failure(400, 'messages require system, user or assistant role and string content');
    if (Object.keys(message).some(key => !['role', 'content'].includes(key))) throw unsupported('message images, tools and additional fields are not implemented');
  }
  const opts = options(model, body.options);
  if (model.template) {
    if (!simpleTemplate(model.template)) throw unsupported('chat with this Ollama Go template is not implemented');
    let system = model.system ?? '', prompt = '', user = null, first = true;
    for (const message of body.messages) {
      if (message.role === 'system') {
        if (user !== null || prompt) throw unsupported('system messages after conversation turns are not implemented');
        system = message.content;
      } else if (message.role === 'user') {
        if (user !== null) throw failure(400, 'this model requires alternating user and assistant messages');
        user = message.content;
      } else {
        if (user === null) throw failure(400, 'assistant message must follow a user message');
        const t = render(model.template, { System: first ? system : '', Prompt: user, Response: message.content });
        prompt += t + (model.template.match(/{{\s*\.Response\s*}}/) ? '' : message.content) + '\n';
        user = null; first = false;
      }
    }
    if (user === null) throw failure(400, 'chat must end with a user message');
    prompt += render(model.template, { System: first ? system : '', Prompt: user, Response: '' });
    return { native: true, path: '/completion', payload: { prompt, ...opts, ...format(body.format, true) } };
  }
  const messages = [...body.messages];
  if (model.system && !messages.some(m => m.role === 'system')) messages.unshift({ role: 'system', content: model.system });
  const { n_predict, ...rest } = opts;
  return { native: false, path: '/v1/chat/completions', payload: { messages, max_tokens: n_predict, ...rest, ...format(body.format, false) } };
}

async function backendJson(response) {
  const reader = response.body?.getReader();
  if (!reader) throw failure(502, 'inference backend returned an empty response');
  const chunks = []; let size = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.length;
      if (size > RESPONSE_LIMIT) throw failure(502, 'inference backend response exceeded limit');
      chunks.push(Buffer.from(value));
    }
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
  try { return JSON.parse(Buffer.concat(chunks).toString('utf8')); }
  catch { throw failure(502, 'inference backend returned invalid JSON'); }
}

async function assertBackend(response) {
  if (response.ok) return;
  // Do not return backend filesystem paths or unbounded process logs to clients.
  await response.body?.cancel().catch(() => {});
  throw failure(response.status === 503 ? 503 : 502, `inference backend returned HTTP ${response.status}`);
}

async function requestBackend(handle, route, body, signal) {
  try { return await handle.request(route, body, { signal }); }
  catch (cause) {
    if (signal.aborted || cause.statusCode || cause.status) throw cause;
    throw failure(502, 'inference backend connection failed');
  }
}

/** Decode SSE independently of UTF-8, HTTP chunk and event boundaries. */
async function* events(response, signal) {
  const reader = response.body?.getReader();
  if (!reader) throw failure(502, 'inference backend returned an empty stream');
  const decoder = new TextDecoder();
  let buffer = '', data = [], eventBytes = 0;
  function parse(line) {
    if (line.endsWith('\r')) line = line.slice(0, -1);
    if (line.startsWith('data:')) {
      eventBytes += Buffer.byteLength(line);
      if (eventBytes > RESPONSE_LIMIT) throw failure(502, 'inference backend stream event exceeded limit');
      data.push(line.slice(5).trimStart());
    }
    if (line === '' && data.length) { const text = data.join('\n'); data = []; eventBytes = 0; return text; }
    return null;
  }
  try {
    while (true) {
      signal?.throwIfAborted();
      const { done, value } = await reader.read();
      buffer += done ? decoder.decode() : decoder.decode(value, { stream: true });
      if (buffer.length > RESPONSE_LIMIT) throw failure(502, 'inference backend stream event exceeded limit');
      let index;
      while ((index = buffer.indexOf('\n')) >= 0) {
        const value = parse(buffer.slice(0, index)); buffer = buffer.slice(index + 1);
        if (value !== null) {
          if (value === '[DONE]') return;
          try { yield JSON.parse(value); } catch (cause) { if (cause instanceof SyntaxError) throw failure(502, 'inference backend returned invalid stream JSON'); throw cause; }
        }
      }
      if (done) {
        if (buffer.trim() || data.length) throw failure(502, 'inference backend ended with an incomplete stream event');
        return;
      }
    }
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}

async function line(res, item, signal) {
  if (signal.aborted || res.destroyed) throw signal.reason ?? new Error('client disconnected');
  if (!res.write(JSON.stringify(item) + '\n')) await once(res, 'drain', { signal });
}

function metrics(value) {
  const timing = value.timings ?? {};
  const usage = value.usage ?? {};
  const result = {};
  const prompt = Number.isFinite(timing.prompt_n) ? timing.prompt_n + (Number.isFinite(timing.cache_n) ? timing.cache_n : 0) : value.tokens_evaluated ?? usage.prompt_tokens;
  const predicted = timing.predicted_n ?? value.tokens_predicted ?? usage.completion_tokens;
  if (Number.isFinite(prompt)) result.prompt_eval_count = prompt;
  if (Number.isFinite(timing.cache_n)) result.prompt_eval_cached_count = timing.cache_n;
  if (Number.isFinite(predicted)) result.eval_count = predicted;
  if (Number.isFinite(timing.prompt_ms)) result.prompt_eval_duration = Math.round(timing.prompt_ms * 1e6);
  if (Number.isFinite(timing.predicted_ms)) result.eval_duration = Math.round(timing.predicted_ms * 1e6);
  return result;
}

function reason(value, native) {
  if (!native) return value.choices?.[0]?.finish_reason === 'length' ? 'length' : 'stop';
  return value.stopped_limit || value.stop_type === 'limit' || value.stop_type === 'length' ? 'length' : 'stop';
}

function tag(model) {
  return { name: model.name, model: model.name, modified_at: model.modified_at, size: model.size, digest: model.digest, details: model.details };
}

async function inference(res, registry, runner, body, kind, signal) {
  const start = ns();
  fields(body, kind === 'chat' ? ['model', 'messages', 'stream', 'options', 'format', 'keep_alive']
    : ['model', 'prompt', 'system', 'raw', 'stream', 'options', 'format', 'keep_alive']);
  if (kind === 'generate') {
    if (body.prompt !== undefined && typeof body.prompt !== 'string') throw failure(400, 'prompt must be a string');
    if (body.system !== undefined && typeof body.system !== 'string') throw failure(400, 'system must be a string');
    if (body.raw !== undefined && typeof body.raw !== 'boolean') throw failure(400, 'raw must be a boolean');
  }
  const model = await registry.resolve(body.model);
  if (model.capabilities && !model.capabilities.includes('completion')) throw unsupported('model does not support text completion');
  const empty = kind === 'chat' ? Array.isArray(body.messages) && body.messages.length === 0 : !body.prompt;
  const base = () => ({ model: model.name, created_at: new Date().toISOString() });
  const content = (text, thinking) => kind === 'chat' ? { message: { role: 'assistant', content: text, ...(thinking ? { thinking } : {}) } } : { response: text };
  const stream = body.stream !== false;
  let spec;
  if (!empty) {
    if (kind === 'chat') spec = chatRequest(model, body);
    else {
      const system = body.system ?? model.system ?? '';
      if (body.raw && body.system !== undefined) throw failure(400, 'raw mode cannot include a system override');
      if (!body.raw && !model.template) {
        const { n_predict, ...rest } = options(model, body.options);
        const messages = [...(system ? [{ role: 'system', content: system }] : []), { role: 'user', content: body.prompt }];
        spec = { native: false, path: '/v1/chat/completions', payload: { messages, max_tokens: n_predict, ...rest, ...format(body.format, false) } };
      } else {
        const prompt = body.raw ? body.prompt : render(model.template, { System: system, Prompt: body.prompt, Response: '' });
        spec = { native: true, path: '/completion', payload: { prompt, ...options(model, body.options), ...format(body.format, true) } };
      }
    }
  } else {
    options(model, body.options); format(body.format, true);
    if (kind === 'chat' && !Array.isArray(body.messages)) throw failure(400, 'messages must be an array');
  }
  if (empty && (body.keep_alive === 0 || body.keep_alive === '0' || body.keep_alive === '0s')) {
    await runner.unload(model.name);
    const result = { ...base(), ...content(''), done: true, done_reason: 'unload', total_duration: elapsed(start) };
    if (stream) { res.writeHead(200, { 'content-type': 'application/x-ndjson' }); await line(res, result, signal); res.end(); }
    else json(res, 200, result);
    return;
  }
  await runner.withModel(model, { signal, keepAlive: body.keep_alive }, async handle => {
    if (empty) {
      const result = { ...base(), ...content(''), done: true, done_reason: 'load', total_duration: elapsed(start), load_duration: handle.loadDuration };
      if (stream) { res.writeHead(200, { 'content-type': 'application/x-ndjson' }); await line(res, result, signal); res.end(); }
      else json(res, 200, result);
      return;
    }
    const payload = { ...spec.payload, stream, ...(!spec.native && stream ? { stream_options: { include_usage: true } } : {}) };
    const response = await requestBackend(handle, spec.path, payload, signal);
    await assertBackend(response);
    if (!stream) {
      const value = await backendJson(response);
      if (value.error) throw failure(502, 'inference backend returned an error');
      const message = spec.native ? { content: value.content } : value.choices?.[0]?.message;
      if (typeof message?.content !== 'string') throw failure(502, 'inference backend omitted generated text');
      json(res, 200, { ...base(), ...content(message.content, message.reasoning_content), done: true, done_reason: reason(value, spec.native), total_duration: elapsed(start), load_duration: handle.loadDuration, ...metrics(value) });
      return;
    }
    res.writeHead(200, { 'content-type': 'application/x-ndjson', 'cache-control': 'no-store' });
    let terminal = null, stats = {};
    for await (const value of events(response, signal)) {
      if (value.error) throw failure(502, 'inference backend returned a stream error');
      stats = { ...stats, ...metrics(value) };
      const delta = spec.native ? { content: value.content } : value.choices?.[0]?.delta;
      if (delta?.content || delta?.reasoning_content) await line(res, { ...base(), ...content(delta.content ?? '', delta.reasoning_content), done: false }, signal);
      if (spec.native ? value.stop === true : value.choices?.[0]?.finish_reason != null) terminal = value;
    }
    if (!terminal) throw failure(502, 'inference backend stream ended without a completion marker');
    await line(res, { ...base(), ...content(''), done: true, done_reason: reason(terminal, spec.native), total_duration: elapsed(start), load_duration: handle.loadDuration, ...stats }, signal);
    res.end();
  });
}

async function embedding(res, registry, runner, body, signal, legacy = false) {
  const start = ns();
  fields(body, legacy ? ['model', 'prompt', 'options', 'keep_alive'] : ['model', 'input', 'truncate', 'options', 'keep_alive']);
  const input = legacy ? body.prompt : body.input;
  if (typeof input !== 'string' && !(Array.isArray(input) && input.every(x => typeof x === 'string'))) throw failure(400, 'input must be a string or array of strings');
  if (body.truncate !== undefined && typeof body.truncate !== 'boolean') throw failure(400, 'truncate must be a boolean');
  if (body.options && Object.keys(body.options).length) throw unsupported('embedding options are not implemented');
  const model = await registry.resolve(body.model);
  if (!model.capabilities?.includes('embedding')) throw unsupported('this model is not configured as an embedding model');
  if (body.truncate !== false) throw unsupported('automatic embedding truncation is not implemented; specify truncate:false');
  await runner.withModel(model, { signal, keepAlive: body.keep_alive }, async handle => {
    const response = await requestBackend(handle, '/v1/embeddings', { input, encoding_format: 'float' }, signal);
    await assertBackend(response);
    const result = await backendJson(response);
    const vectors = result.data?.slice().sort((a, b) => a.index - b.index).map(x => x.embedding);
    const count = Array.isArray(input) ? input.length : 1;
    if (!Array.isArray(vectors) || vectors.length !== count || vectors.some(v => !Array.isArray(v) || !v.length || v.some(x => !Number.isFinite(x)))) throw failure(502, 'inference backend returned invalid embeddings');
    if (legacy) json(res, 200, { embedding: vectors[0] });
    else json(res, 200, { model: model.name, embeddings: vectors, total_duration: elapsed(start), load_duration: handle.loadDuration, ...(Number.isFinite(result.usage?.prompt_tokens) ? { prompt_eval_count: result.usage.prompt_tokens } : {}) });
  });
}

/** Native Ollama subset; metadata is truthful and unsupported features fail explicitly. */
export function createServer({ registry, runner, version = '0.1.0-reclaimer', requestTimeoutMs = 300000, handleRequest }) {
  const server = http.createServer(async (req, res) => {
    const disconnected = new AbortController();
    req.once('aborted', () => disconnected.abort(new Error('client aborted request')));
    res.once('close', () => { if (!res.writableEnded) disconnected.abort(new Error('client disconnected')); });
    const signal = AbortSignal.any([disconnected.signal, AbortSignal.timeout(requestTimeoutMs)]);
    try {
      // CLI clients omit Origin. Browser origins must be this exact loopback listener.
      const host = req.headers.host ?? '';
      if (!/^(?:127\.0\.0\.1|localhost|\[::1\])(?::\d+)?$/i.test(host)) throw failure(403, 'only loopback Host names are accepted');
      if (req.headers.origin && req.headers.origin !== `http://${host}`) throw failure(403, 'cross-origin requests are disabled');
      const route = new URL(req.url, 'http://127.0.0.1').pathname;
      // Optional local platform routes share the API's Host/Origin boundary.
      if (handleRequest && await handleRequest({ req, res, route, signal })) return;
      if (req.method === 'GET') {
        if (route === '/') { res.writeHead(200, { 'content-type': 'text/plain' }); res.end('Ollama-compatible Reclaimer server is running'); return; }
        if (route === '/api/version') return json(res, 200, { version });
        if (route === '/api/tags') return json(res, 200, { models: (await registry.list()).map(tag) });
        if (route === '/api/ps') {
          const loaded = await runner.loaded();
          return json(res, 200, { models: loaded ? [{ ...tag(loaded.model), expires_at: loaded.expiresAt, ...(runner.gpuLayers === 0 ? { size_vram: 0 } : {}), ...(loaded.contextSize ? { context_length: loaded.contextSize } : {}) }] : [] });
        }
        if (route === '/v1/models') return json(res, 200, { object: 'list', data: (await registry.list()).map(m => ({ id: m.name, object: 'model', created: Math.floor(Date.parse(m.modified_at) / 1000), owned_by: 'local' })) });
      }
      if (req.method === 'POST') {
        const body = await readBody(req);
        if (route === '/api/show') {
          fields(body, ['model', 'verbose']);
          const model = await registry.resolve(body.model);
          return json(res, 200, { license: (model.licenses ?? []).join('\n\n'), template: model.template ?? '', system: model.system ?? '', parameters: Object.entries(model.parameters ?? {}).map(([k, v]) => `${k} ${JSON.stringify(v)}`).join('\n'), details: model.details, capabilities: model.capabilities ?? ['completion'], model_info: { 'general.architecture': model.details?.family } });
        }
        if (route === '/api/chat' || route === '/api/generate') return await inference(res, registry, runner, body, route.endsWith('chat') ? 'chat' : 'generate', signal);
        if (route === '/api/embed') return await embedding(res, registry, runner, body, signal);
        if (['/api/pull', '/api/push', '/api/create', '/api/copy', '/api/delete', '/api/embeddings'].includes(route) || route.startsWith('/v1/')) throw unsupported('this operation is not implemented; see the compatibility matrix');
      }
      throw failure(404, 'route not found');
    } catch (cause) {
      if (res.destroyed || disconnected.signal.aborted) return;
      const status = signal.aborted ? 504 : cause.statusCode ?? cause.status ?? 500;
      const message = signal.aborted ? 'inference request timed out' : status >= 500 && status !== 501 ? (status === 502 || status === 503 ? cause.message : 'local inference failed; inspect server diagnostics') : cause.message;
      if (res.headersSent) res.end(JSON.stringify({ error: message }) + '\n');
      else json(res, status >= 400 && status <= 599 ? status : 500, { error: message });
      if (status === 500) server.emit('diagnostic', cause);
    }
  });
  server.requestTimeout = 30000;
  server.headersTimeout = 10000;
  return server;
}
