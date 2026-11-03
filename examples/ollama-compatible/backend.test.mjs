import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { setTimeout as delay } from 'node:timers/promises';
import { RunnerManager } from './backend.mjs';

const fakeServer = `
import http from 'node:http';
const args = process.argv.slice(2);
const get = name => args[args.indexOf(name) + 1];
const model = get('--model');
if (model === 'exit') process.exit(7);
if (model === 'hang') setInterval(() => {}, 1000);
else http.createServer(async (req, res) => {
  if (req.url === '/health') { res.end(JSON.stringify({status:'ok'})); return; }
  if (req.headers.authorization !== 'Bearer ' + get('--api-key')) { res.writeHead(401).end(); return; }
  if (req.url === '/v1/models') { res.end(JSON.stringify({data:[{id:model === 'foreign' ? 'other-process' : get('--alias')}]})); return; }
  const chunks = []; for await (const chunk of req) chunks.push(chunk);
  const body = JSON.parse(Buffer.concat(chunks).toString());
  if (body.crash) process.exit(8);
  res.setHeader('content-type', 'application/json');
  setTimeout(() => res.end(JSON.stringify({pid:process.pid, model, body})), body.delay || 0);
}).listen(Number(get('--port')), get('--host'));
`;

async function setup(t, options = {}) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'reclaimer-runner-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const script = path.join(dir, 'fake.mjs');
  await writeFile(script, fakeServer);
  const manager = new RunnerManager({ executable: [process.execPath, script], loadTimeoutMs: 3000, idleMs: 1000, ...options });
  t.after(() => manager.close());
  return manager;
}

const model = name => ({ name, modelPath: name, digest: name, size: 8, capabilities: ['completion'] });
const complete = async runner => (await runner.request('/completion', { prompt: 'hello' })).json();

test('a backend is authenticated, reused, switched, and explicitly unloaded', async t => {
  const manager = await setup(t);
  const first = await manager.withModel(model('first'), {}, async runner => {
    assert.ok(runner.loadDuration > 0);
    const unauthorized = await fetch(`${runner.baseUrl}/v1/models`);
    assert.equal(unauthorized.status, 401);
    return complete(runner);
  });
  assert.equal(manager.loaded().model.name, 'first');
  const second = await manager.withModel(model('first'), {}, async runner => {
    assert.equal(runner.loadDuration, 0);
    return complete(runner);
  });
  assert.equal(first.pid, second.pid);
  const switched = await manager.withModel(model('second'), {}, complete);
  assert.notEqual(switched.pid, first.pid);
  await manager.unload('first');
  assert.equal(manager.loaded().model.name, 'second');
  await manager.unload('second');
  assert.equal(manager.loaded(), null);
  await manager.unload();
  assert.equal(manager.loaded(), null);
});

test('requests serialize and unload waits for the active response body', async t => {
  const manager = await setup(t, { maxPendingRequests: 2 });
  let release;
  let entered;
  const barrier = new Promise(resolve => { release = resolve; });
  const started = new Promise(resolve => { entered = resolve; });
  const order = [];
  const first = manager.withModel(model('first'), {}, async runner => {
    order.push('first'); entered();
    await barrier;
    const result = await complete(runner);
    order.push('first-finished');
    return result;
  });
  await started;
  const second = manager.withModel(model('first'), {}, async runner => { order.push('second'); return complete(runner); });
  const unloaded = manager.unload().then(() => order.push('unloaded'));
  await delay(20);
  assert.deepEqual(order, ['first']);
  release();
  await Promise.all([first, second, unloaded]);
  assert.deepEqual(order, ['first', 'first-finished', 'second', 'unloaded']);
});

test('queued cancellation rejects promptly without skipping the active owner', async t => {
  const manager = await setup(t);
  let release;
  let entered;
  const barrier = new Promise(resolve => { release = resolve; });
  const started = new Promise(resolve => { entered = resolve; });
  const active = manager.withModel(model('first'), {}, async () => { entered(); await barrier; });
  await started;
  const controller = new AbortController();
  const queued = manager.withModel(model('second'), { signal: controller.signal }, complete);
  controller.abort();
  await assert.rejects(queued, error => error.name === 'AbortError');
  assert.equal(manager.loaded().model.name, 'first');
  release();
  await active;
  await manager.withModel(model('third'), {}, complete);
  assert.equal(manager.loaded().model.name, 'third');
});

test('active cancellation stops its owned runner and later requests recover', async t => {
  const manager = await setup(t);
  const controller = new AbortController();
  await assert.rejects(manager.withModel(model('first'), { signal: controller.signal }, async runner => {
    setTimeout(() => controller.abort(), 30);
    return (await runner.request('/completion', { delay: 500 })).json();
  }), error => error.name === 'AbortError');
  assert.equal(manager.loaded(), null);
  assert.equal((await manager.withModel(model('first'), {}, complete)).model, 'first');
});

test('zero keep_alive unloads, finite idle expires, and negative stays loaded', async t => {
  const manager = await setup(t);
  await manager.withModel(model('first'), { keepAlive: 0 }, complete);
  assert.equal(manager.loaded(), null);
  await manager.withModel(model('first'), { keepAlive: '25ms' }, complete);
  await delay(100);
  assert.equal(manager.loaded(), null);
  await manager.withModel(model('first'), { keepAlive: -1 }, complete);
  assert.equal(manager.loaded().expiresAt, null);
  await assert.rejects(manager.withModel(model('first'), { keepAlive: 'forever' }, complete), /keep_alive/);
});

test('spawn failure, early exit, stalled load, and wrong listener identity are explicit errors', async t => {
  const absent = new RunnerManager({ executable: path.join(os.tmpdir(), 'reclaimer-nonexistent-executable') });
  t.after(() => absent.close());
  await assert.rejects(absent.withModel(model('first'), {}, complete), error => error.code === 'backend_start_failed');
  const manager = await setup(t, { loadTimeoutMs: 1200 });
  await assert.rejects(manager.withModel(model('exit'), {}, complete), error => error.code === 'backend_start_failed');
  await assert.rejects(manager.withModel(model('hang'), {}, complete), error => error.code === 'backend_load_timeout');
  await assert.rejects(manager.withModel(model('foreign'), {}, complete), error => error.code === 'backend_load_timeout');
  assert.equal(manager.loaded(), null);
});

test('runner crashes invalidate loaded state and do not poison subsequent requests', async t => {
  const manager = await setup(t);
  await assert.rejects(manager.withModel(model('first'), {}, async runner => (await runner.request('/completion', { crash: true })).json()));
  await delay(30);
  assert.equal(manager.loaded(), null);
  assert.equal((await manager.withModel(model('second'), {}, complete)).model, 'second');
});

test('close cancels active inference and rejects subsequent use', async t => {
  const manager = await setup(t, { maxPendingRequests: 1 });
  let entered;
  const started = new Promise(resolve => { entered = resolve; });
  const active = manager.withModel(model('first'), {}, async runner => {
    entered();
    return (await runner.request('/completion', { delay: 1000 })).json();
  });
  await started;
  const rejection = assert.rejects(active);
  await manager.close();
  await rejection;
  assert.equal(manager.loaded(), null);
  await assert.rejects(manager.withModel(model('first'), {}, complete), error => error.code === 'backend_closed');
});

test('admission is bounded and canceled queued requests immediately release capacity', async t => {
  const manager = await setup(t, { maxPendingRequests: 2 });
  let release;
  let entered;
  const barrier = new Promise(resolve => { release = resolve; });
  const started = new Promise(resolve => { entered = resolve; });
  const active = manager.withModel(model('first'), {}, async () => { entered(); await barrier; });
  await started;
  assert.equal(manager.pending(), 1);
  const canceled = new AbortController();
  const queued = manager.withModel(model('second'), { signal: canceled.signal }, complete);
  assert.equal(manager.pending(), 2);
  await assert.rejects(manager.withModel(model('third'), {}, complete), error => error.code === 'backend_queue_full' && error.statusCode === 503);
  assert.equal(manager.pending(), 2);
  canceled.abort();
  await assert.rejects(queued, error => error.name === 'AbortError');
  assert.equal(manager.pending(), 1);
  // Repeated cancellations must remove queue references, not append released
  // Promise gates behind the still-running callback.
  for (let index = 0; index < 40; index++) {
    const controller = new AbortController();
    const waiting = manager.withModel(model('canceled'), { signal: controller.signal }, complete);
    controller.abort();
    await assert.rejects(waiting, error => error.name === 'AbortError');
    assert.equal(manager.pending(), 1);
  }
  const accepted = manager.withModel(model('third'), {}, complete);
  assert.equal(manager.pending(), 2);
  const unloading = manager.unload();
  assert.equal(manager.unload(), unloading);
  release();
  await Promise.all([active, accepted, unloading]);
  assert.equal(manager.pending(), 0);
  assert.equal(manager.loaded(), null);
});
