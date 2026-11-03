import test from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const run = promisify(execFile);
const cli = fileURLToPath(new URL('./cli.mjs', import.meta.url));

test('Guilty Spark CLI help identifies the platform and launcher', async () => {
  const { stdout } = await run(process.execPath, [cli, '--help']);
  assert.match(stdout, /Guilty Spark 343/);
  assert.match(stdout, /examples\/guilty-spark-343\/cli\.mjs/);
});

test('CLI explicitly maps a standalone GGUF relative to config and never launches it during listing', async t => {
  const dir = await mkdtemp(path.join(tmpdir(), 'spark-cli-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  await writeFile(path.join(dir, 'tiny.gguf'), Buffer.from([0x47, 0x47, 0x55, 0x46, 3, 0, 0, 0]));
  const config = path.join(dir, 'config.json');
  await writeFile(config, JSON.stringify({ modelsRoot: 'empty', models: [{ name: 'gobbo-local', path: 'tiny.gguf' }] }));
  const { stdout } = await run(process.execPath, [cli, '--config', config, '--list']);
  const inventory = JSON.parse(stdout);
  assert.equal(inventory.length, 1);
  assert.equal(inventory[0].name, 'gobbo-local:latest');
  assert.equal(inventory[0].supported, true);
  await writeFile(config, JSON.stringify({ models: [{ name: 'bad', path: 'tiny.gguf', command: 'unexpected' }] }));
  await assert.rejects(run(process.execPath, [cli, '--config', config, '--list']), error => error.code === 1 && /models must be an array/.test(error.stderr));
});
