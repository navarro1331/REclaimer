#!/usr/bin/env node
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { readFile, mkdir, mkdtemp, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { parseArgs } from 'node:util';
import { ModelRegistry } from './model-registry.mjs';
import { RunnerManager } from './backend.mjs';
import { createServer } from './server.mjs';

const { values } = parseArgs({ options: { config: { type: 'string' }, model: { type: 'string', default: 'deepseek-coder:latest' }, output: { type: 'string', default: 'output/ollama-reconstruction' } } });
assert.ok(values.config, '--config is required (local JSON with executable and modelsRoot)');
const configFile = path.resolve(values.config);
const config = JSON.parse(await readFile(configFile, 'utf8'));
const root = path.resolve(path.dirname(configFile), config.modelsRoot);
const executable = path.resolve(path.dirname(configFile), config.executable);
const registry = new ModelRegistry(root);
const model = await registry.resolve(values.model);
async function hash(file) {
  const h = createHash('sha256');
  for await (const bytes of createReadStream(file)) h.update(bytes);
  return h.digest('hex');
}
const sourceHash = await hash(model.modelPath);
const runner = new RunnerManager({ executable, contextSize: config.contextSize ?? 2048, threads: config.threads ?? 4, gpuLayers: config.gpuLayers ?? 0 });
const server = createServer({ registry, runner });
const receipt = { created_at: new Date().toISOString(), model: model.name, model_sha256: sourceHash, backend_sha256: await hash(executable), cases: [], limitations: ['Real HTTP/inference smoke on one model, not complete Ollama equivalence', 'No embeddings or multimodal inference tested'] };
server.listen(0, '127.0.0.1');
await once(server, 'listening');
const url = `http://127.0.0.1:${server.address().port}`;
async function request(route, body) {
  const response = await fetch(url + route, body === undefined ? {} : { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body), signal: AbortSignal.timeout(120000) });
  const text = await response.text();
  return { status: response.status, content_type: response.headers.get('content-type'), value: response.headers.get('content-type')?.includes('ndjson') ? text.trim().split('\n').map(x => JSON.parse(x)) : JSON.parse(text) };
}
async function check(name, route, body, verify) {
  const start = performance.now();
  const result = await request(route, body);
  verify(result);
  receipt.cases.push({ name, duration_ms: Math.round(performance.now() - start), ...result });
  console.log(`PASS ${name}`);
  return result;
}
const generated = result => { assert.equal(result.status, 200, JSON.stringify(result.value)); assert.equal(result.value.done, true); assert.ok(result.value.eval_count > 0); assert.ok(result.value.response.length > 0); };
const streamed = result => { assert.equal(result.status, 200, JSON.stringify(result.value)); assert.ok(result.value.length > 1); assert.equal(result.value.at(-1).done, true); assert.ok(result.value.at(-1).eval_count > 0); assert.equal(result.value.filter(x => x.done).length, 1); assert.ok(!result.value.some(x => x.error)); };
try {
  await check('version', '/api/version', undefined, r => assert.equal(r.value.version, '0.1.0-reclaimer'));
  await check('existing model discovery', '/api/tags', undefined, r => assert.ok(r.value.models.some(m => m.name === model.name)));
  await check('model metadata', '/api/show', { model: model.name }, r => { assert.equal(r.status, 200); assert.equal(r.value.details.format, 'gguf'); });
  await check('real templated generation', '/api/generate', { model: model.name, prompt: 'What is 2 + 2? Answer with one sentence.', stream: false, options: { temperature: 0, num_predict: 24 } }, generated);
  await check('real raw generation NDJSON', '/api/generate', { model: model.name, prompt: 'The sum of two and two is', raw: true, options: { temperature: 0, num_predict: 12 } }, streamed);
  await check('real chat', '/api/chat', { model: model.name, messages: [{ role: 'user', content: 'Write a Python function that adds two numbers.' }], stream: false, options: { temperature: 0, num_predict: 40 } }, r => { assert.equal(r.status, 200, JSON.stringify(r.value)); assert.equal(r.value.done, true); assert.ok(r.value.message.content.length > 0); assert.ok(r.value.eval_count > 0); });
  await check('real chat NDJSON with history', '/api/chat', { model: model.name, messages: [{ role: 'user', content: 'What is 2 + 2?' }, { role: 'assistant', content: '4.' }, { role: 'user', content: 'And 3 + 3?' }], options: { temperature: 0, num_predict: 16 } }, streamed);
  await check('JSON schema generation', '/api/generate', { model: model.name, prompt: 'Return an object with answer 4.', stream: false, format: { type: 'object', properties: { answer: { type: 'integer', const: 4 } }, required: ['answer'], additionalProperties: false }, options: { temperature: 0, num_predict: 24 } }, r => { generated(r); assert.deepEqual(JSON.parse(r.value.response), { answer: 4 }); });
  await check('resident model', '/api/ps', undefined, r => assert.equal(r.value.models[0]?.name, model.name));
  await check('missing model error', '/api/generate', { model: 'missing-reclaimer-test', prompt: 'test' }, r => assert.equal(r.status, 404));
  await check('unsupported tools refused', '/api/chat', { model: model.name, messages: [], tools: [] }, r => assert.equal(r.status, 501));
  await check('explicit model unload', '/api/generate', { model: model.name, prompt: '', stream: false, keep_alive: 0 }, r => { assert.equal(r.status, 200); assert.equal(r.value.done_reason, 'unload'); });
  await check('no resident model after unload', '/api/ps', undefined, r => assert.deepEqual(r.value.models, []));
  assert.equal(await hash(model.modelPath), sourceHash, 'source weights changed');
  receipt.source_weights_preserved = true;
  receipt.passed = true;
} catch (error) {
  receipt.passed = false;
  receipt.failure = error.message;
  process.exitCode = 1;
} finally {
  server.closeAllConnections();
  await Promise.all([runner.close(), new Promise(resolve => server.close(resolve))]);
  receipt.owned_runner_stopped = runner.loaded() === null;
  const output = path.resolve(values.output);
  await mkdir(output, { recursive: true });
  const run = await mkdtemp(path.join(output, 'live-'));
  await writeFile(path.join(run, 'receipt.json'), JSON.stringify(receipt, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ passed: receipt.passed, cases: receipt.cases.length, receipt: path.join(run, 'receipt.json'), failure: receipt.failure }));
}
