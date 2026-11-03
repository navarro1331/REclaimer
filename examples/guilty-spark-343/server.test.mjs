import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { once } from 'node:events';
import { createPlatformServer } from './server.mjs';

const model = { name: 'fixture:latest', digest: 'sha256:fixture', size: 10, modified_at: '2026-01-01T00:00:00Z', details: { family: 'test' }, capabilities: ['completion'], parameters: {} };

async function fixture(t) {
  const requests = [];
  const registry = { list: async () => [model], resolve: async () => model };
  const runner = {
    loaded: () => null,
    async withModel(selected, options, work) {
      return work({ loadDuration: 0, async request(route, payload) {
        requests.push({ route, payload });
        return new Response('data: {"choices":[{"delta":{"content":"Hello, monitor."},"finish_reason":null}]}\n\ndata: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n');
      } });
    },
  };
  const server = createPlatformServer({ registry, runner });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(async () => { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); });
  return { base: `http://127.0.0.1:${server.address().port}`, requests };
}

test('platform serves browser modules and native streaming chat on the same listener', async t => {
  const { base, requests } = await fixture(t);
  const page = await fetch(base);
  assert.equal(page.status, 200);
  assert.match(page.headers.get('content-type'), /text\/html/);
  assert.match(await page.text(), /Guilty Spark 343/);
  for (const route of ['/app.mjs', '/state.mjs', '/style.css']) {
    const asset = await fetch(base + route);
    assert.equal(asset.status, 200, route);
    assert.ok((await asset.text()).length > 30);
  }
  assert.equal((await (await fetch(base + '/api/platform')).json()).name, 'Guilty Spark 343');
  assert.match((await (await fetch(base + '/api/version')).json()).version, /guilty-spark-343/);
  assert.equal((await (await fetch(base + '/api/tags')).json()).models[0].name, model.name);
  const response = await fetch(base + '/api/chat', { method: 'POST', headers: { 'content-type': 'application/json', origin: base }, body: JSON.stringify({ model: model.name, messages: [{ role: 'system', content: 'Be a helpful monitor.' }, { role: 'user', content: 'Hello' }], stream: true }) });
  assert.equal(response.status, 200);
  const frames = (await response.text()).trim().split('\n').map(JSON.parse);
  assert.equal(frames[0].message.content, 'Hello, monitor.');
  assert.equal(frames.at(-1).done, true);
  assert.equal(requests[0].route, '/v1/chat/completions');
  assert.equal(requests[0].payload.messages[0].content, 'Be a helpful monitor.');
});

test('only allowlisted files are public and responses prevent active content injection', async t => {
  const { base } = await fixture(t);
  const page = await fetch(base);
  assert.match(page.headers.get('content-security-policy'), /script-src 'self'/);
  assert.ok(!page.headers.get('content-security-policy').includes('unsafe-inline'));
  assert.equal(page.headers.get('x-content-type-options'), 'nosniff');
  assert.equal(page.headers.get('cache-control'), 'no-store');
  for (const route of ['/cli.mjs', '/server.mjs', '/README.md', '/%2e%2e/config/local-ollama-compatible.json', '/.env']) {
    assert.equal((await fetch(base + route)).status, 404, route);
  }
  assert.equal((await fetch(base + '/api/platform', { method: 'POST', body: '{}' })).status, 405);
  assert.equal(await (await fetch(base, { method: 'HEAD' })).text(), '');
});

test('platform assets retain loopback Host and same-origin protections', async t => {
  const { base } = await fixture(t);
  assert.equal((await fetch(base, { headers: { origin: 'https://foreign.example' } })).status, 403);
  const status = await new Promise((resolve, reject) => {
    const req = http.get(base, { headers: { host: 'foreign.example', origin: 'http://foreign.example' } }, res => { res.resume(); resolve(res.statusCode); });
    req.on('error', reject);
  });
  assert.equal(status, 403);
});
