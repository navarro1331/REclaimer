#!/usr/bin/env node
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { main as start } from '../ollama-compatible/cli.mjs';
import { createPlatformServer } from './server.mjs';

export function main(argv = process.argv.slice(2)) {
  return start(argv, { serverFactory: createPlatformServer, label: 'Guilty Spark 343', commandPath: 'examples/guilty-spark-343/cli.mjs' });
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
}
