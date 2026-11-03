#!/usr/bin/env node
import { parseArgs } from 'node:util';
import { readFile, stat } from 'node:fs/promises';
import { homedir } from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { ModelRegistry } from './model-registry.mjs';
import { RunnerManager } from './backend.mjs';
import { createServer } from './server.mjs';

export async function main(argv = process.argv.slice(2), {
  serverFactory = createServer,
  label = 'Reclaimer Ollama-compatible',
  commandPath = 'examples/ollama-compatible/cli.mjs',
} = {}) {
  const { values } = parseArgs({ args: argv, options: {
    help: { type: 'boolean', short: 'h' }, config: { type: 'string' },
    'llama-server': { type: 'string' }, 'models-root': { type: 'string' },
    port: { type: 'string' }, context: { type: 'string' }, threads: { type: 'string' },
    'gpu-layers': { type: 'string' }, list: { type: 'boolean' },
  } });
  if (values.help) {
    console.log(`${label} local server (Node 24+)

node ${commandPath} --llama-server <executable> --models-root <Ollama models directory>
node ${commandPath} --config <local JSON file>

--port <number>       Loopback listener, default 11435 (Ollama normally uses 11434)
--context <number>    Runner context size, default 2048
--threads <number>    CPU threads, default 4
--gpu-layers <n>      Layers offloaded by a suitable llama.cpp build; default 0 (CPU)
--list               Inspect the read-only model registry and exit

Local JSON keys: executable, modelsRoot, models, port, contextSize, threads, gpuLayers.
Optional models: [{"name":"my-model","path":"relative/or/absolute/model.gguf"}].
Model weights and installation are read-only. No services or clients are registered.
See examples/ollama-compatible/README.md for the explicit compatibility limits.`);
    return;
  }
  const configPath = values.config ? path.resolve(values.config) : null;
  const config = configPath ? JSON.parse(await readFile(configPath, 'utf8')) : {};
  const keys = ['executable', 'modelsRoot', 'models', 'port', 'contextSize', 'threads', 'gpuLayers'];
  if (!config || Array.isArray(config) || typeof config !== 'object' || Object.keys(config).some(k => !keys.includes(k))) throw new Error('Invalid local configuration keys.');
  const relativeTo = configPath ? path.dirname(configPath) : process.cwd();
  const modelsRoot = values['models-root'] ? path.resolve(values['models-root']) : config.modelsRoot ? path.resolve(relativeTo, config.modelsRoot) : process.env.OLLAMA_MODELS || path.join(homedir(), '.ollama', 'models');
  if (config.models !== undefined && (!Array.isArray(config.models) || config.models.length > 10000 || config.models.some(model =>
    !model || typeof model !== 'object' || Array.isArray(model) || typeof model.name !== 'string' || typeof model.path !== 'string' || !model.path || Object.keys(model).some(k => !['name', 'path'].includes(k))))) throw new Error('models must be an array of { name, path } entries.');
  const registry = new ModelRegistry(modelsRoot, { models: (config.models ?? []).map(model => ({ name: model.name, path: path.resolve(relativeTo, model.path) })) });
  if (values.list) {
    console.log(JSON.stringify((await registry.list()).map(m => ({ name: m.name, size: m.size, supported: m.supported, reason: m.unsupportedReason ?? null })), null, 2));
    return;
  }
  const executable = values['llama-server'] ? path.resolve(values['llama-server']) : config.executable ? path.resolve(relativeTo, config.executable) : null;
  if (!executable || !(await stat(executable).catch(() => null))?.isFile()) throw new Error('Select an existing llama-server executable with --llama-server or local configuration.');
  const port = Number(values.port ?? config.port ?? 11435);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('port must be between 1 and 65535.');
  const runner = new RunnerManager({ executable,
    contextSize: Number(values.context ?? config.contextSize ?? 2048),
    threads: Number(values.threads ?? config.threads ?? 4),
    gpuLayers: Number(values['gpu-layers'] ?? config.gpuLayers ?? 0),
  });
  const server = serverFactory({ registry, runner });
  server.on('diagnostic', error => console.error(`Local inference diagnostic: ${error.message}`));
  let stopping = false;
  async function stop() {
    if (stopping) return;
    stopping = true;
    server.closeAllConnections();
    await Promise.all([runner.close(), new Promise(resolve => server.close(resolve))]);
  }
  for (const name of ['SIGINT', 'SIGTERM']) process.once(name, () => { void stop().catch(error => { console.error(error.message); process.exitCode = 1; }); });
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
    console.log(`${label}: http://127.0.0.1:${port}`);
    console.log('Native text generation/chat, read-only model registry; Ctrl+C closes the owned inference process.');
    return { server, runner, stop };
  } catch (error) { await runner.close(); throw error; }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
}
