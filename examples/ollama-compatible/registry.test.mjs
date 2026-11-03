import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, mkdir, readFile, rm, stat, symlink, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { setTimeout as delay } from 'node:timers/promises';
import { ModelRegistry } from './model-registry.mjs';

const GGUF = Buffer.from([0x47, 0x47, 0x55, 0x46, 3, 0, 0, 0]);
const TYPE = 'application/vnd.ollama.image.';

async function fixture(t) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'reclaimer-registry-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  await mkdir(path.join(root, 'blobs'));
  const manifestPath = path.join(root, 'manifests', 'registry.ollama.ai', 'library', 'tiny', 'latest');
  await mkdir(path.dirname(manifestPath), { recursive: true });
  async function blob(value, mediaType) {
    const bytes = Buffer.isBuffer(value) ? value : Buffer.from(typeof value === 'string' ? value : JSON.stringify(value));
    const digest = `sha256:${createHash('sha256').update(bytes).digest('hex')}`;
    await writeFile(path.join(root, 'blobs', digest.replace(':', '-')), bytes);
    return { mediaType, digest, size: bytes.length };
  }
  const model = await blob(GGUF, `${TYPE}model`);
  const config = await blob({ model_format: 'gguf', model_family: 'llama', model_type: '1M', file_type: 'F32' }, 'application/vnd.docker.container.image.v1+json');
  const manifest = { schemaVersion: 2, config, layers: [model, await blob('{{ .Prompt }}', `${TYPE}template`), await blob({ temperature: 0.2 }, `${TYPE}params`)] };
  const save = () => writeFile(manifestPath, JSON.stringify(manifest));
  await save();
  return { root, manifestPath, model, manifest, save, blob, registry: new ModelRegistry(root) };
}

test('legacy model resolves aliases and metadata without modifying the store', async t => {
  const { registry, manifestPath, model, root } = await fixture(t);
  const before = await stat(manifestPath);
  const resolved = await registry.resolve('registry.ollama.ai/library/tiny');
  assert.equal(resolved.name, 'tiny:latest');
  assert.equal(resolved.modelPath, path.join(root, 'blobs', model.digest.replace(':', '-')));
  assert.deepEqual(resolved.parameters, { temperature: 0.2 });
  assert.equal(resolved.template, '{{ .Prompt }}');
  assert.equal(resolved.details.family, 'llama');
  assert.equal(resolved.digest, createHash('sha256').update(await readFile(manifestPath)).digest('hex'));
  assert.deepEqual(resolved.capabilities, ['completion']);
  assert.equal((await stat(manifestPath)).mtimeMs, before.mtimeMs);
});

test('unsupported adapters remain visible in listing and fail resolution', async t => {
  const f = await fixture(t);
  f.manifest.layers.push(await f.blob('adapter', `${TYPE}adapter`));
  await f.save();
  const [entry] = await f.registry.list();
  assert.equal(entry.supported, false);
  assert.match(entry.unsupportedReason, /adapter/);
  await assert.rejects(f.registry.resolve('tiny'), error => error.code === 'unsupported_model' && error.statusCode === 422);
});

test('MLX and v2 weights produce explicit unsupported errors', async t => {
  const f = await fixture(t);
  f.manifest.config = await f.blob({ model_format: 'mlx' }, 'config');
  await f.save();
  await assert.rejects(f.registry.resolve('tiny'), /Unsupported model format: mlx/);
  f.manifest.config = await f.blob({ model_format: 'gguf' }, 'config');
  f.manifest.layers = [await f.blob('tensor', `${TYPE}tensor`)];
  await f.save();
  await assert.rejects(f.registry.resolve('tiny'), /Unsupported model layer: tensor/);
});

test('digest traversal and invalid GGUF are refused before inference', async t => {
  const f = await fixture(t);
  f.manifest.layers[0].digest = 'sha256:../../outside';
  await f.save();
  await assert.rejects(f.registry.resolve('tiny'), /Invalid model layer digest/);
  f.manifest.layers[0] = await f.blob('not a real model', `${TYPE}model`);
  await f.save();
  await assert.rejects(f.registry.resolve('tiny'), /not a supported GGUF/);
});

test('metadata content must match its declared SHA256 digest', async t => {
  const f = await fixture(t);
  const template = f.manifest.layers[1];
  await writeFile(path.join(f.root, 'blobs', template.digest.replace(':', '-')), 'x'.repeat(template.size));
  await assert.rejects(f.registry.resolve('tiny'), /does not match its SHA256 digest/);
});

test('a directory link outside the selected model store is rejected', async t => {
  const f = await fixture(t);
  const outside = await mkdtemp(path.join(os.tmpdir(), 'reclaimer-outside-'));
  t.after(() => rm(outside, { recursive: true, force: true }));
  await symlink(outside, path.join(f.root, 'manifests', 'linked'), process.platform === 'win32' ? 'junction' : 'dir');
  await assert.rejects(f.registry.list(), error => error.code === 'unsafe_model_path');
});

test('missing models, malformed names, and missing blobs are distinguished', async t => {
  const f = await fixture(t);
  await assert.rejects(f.registry.resolve('missing'), error => error.statusCode === 404);
  await assert.rejects(f.registry.resolve('../tiny'), /Invalid model name/);
  await rm(path.join(f.root, 'blobs', f.model.digest.replace(':', '-')));
  await assert.rejects(f.registry.resolve('tiny'), /referenced model blob is missing/);
});

test('explicit GGUF files are supported only by operator-selected names', async t => {
  const f = await fixture(t);
  const explicit = path.join(f.root, 'selected.gguf');
  await writeFile(explicit, GGUF);
  const registry = new ModelRegistry(f.root, { models: [{ name: 'selected', path: explicit }] });
  assert.equal((await registry.resolve('selected')).modelPath, explicit);
  assert.equal((await registry.list()).length, 2);
});

test('aggregate metadata is bounded across multiple individually valid manifests', async t => {
  const f = await fixture(t);
  f.manifest.layers.push(await f.blob('x'.repeat(600), `${TYPE}system`));
  await f.save();
  const second = path.join(path.dirname(path.dirname(f.manifestPath)), 'other', 'latest');
  await mkdir(path.dirname(second));
  await writeFile(second, JSON.stringify(f.manifest));
  const oneScanBytes = Buffer.byteLength(JSON.stringify(f.manifest)) + f.manifest.config.size + f.manifest.layers.filter(layer => layer.mediaType !== `${TYPE}model`).reduce((sum, layer) => sum + layer.size, 0);
  const registry = new ModelRegistry(f.root, { maxMetadataBytes: oneScanBytes + 32 });
  await assert.rejects(registry.list(), error => error.code === 'model_store_limit' && error.statusCode === 413);
  await rm(second);
  assert.equal((await registry.list()).length, 1, 'failed scan releases its shared promise and budget');
});

test('overlapping listing and resolution share one scan without stale caching', async t => {
  const f = await fixture(t);
  let scans = 0;
  const original = f.registry.manifests.bind(f.registry);
  f.registry.manifests = async () => {
    scans++;
    await delay(20);
    return original();
  };
  const [first, second, resolved] = await Promise.all([f.registry.list(), f.registry.list(), f.registry.resolve('tiny')]);
  assert.equal(scans, 1);
  assert.equal(first, second);
  assert.equal(resolved, first[0]);
  f.manifest.layers.push(await f.blob('projector', `${TYPE}projector`));
  await f.save();
  assert.equal((await f.registry.list())[0].supported, false);
  assert.equal(scans, 2);
});
