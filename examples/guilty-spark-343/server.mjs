import { readFile } from 'node:fs/promises';
import { createServer } from '../ollama-compatible/server.mjs';

export const PLATFORM = Object.freeze({
  name: 'Guilty Spark 343',
  version: '0.2.0',
  integrations: ['Ollama-compatible text API', 'GobboNet conversation and character import'],
  history: 'Browser-local storage; export JSON for backup.',
  capabilities: ['streaming-chat', 'model-selection', 'characters', 'persona', 'conversation-history', 'json-import-export'],
  limits: ['Text only', 'One loaded model at a time', 'No model downloads', 'No GobboNet scripts, schedules, RAG, or server-side state sync'],
});

const ASSETS = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/index.html', ['index.html', 'text/html; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/state.mjs', ['state.mjs', 'text/javascript; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']],
]);

const SECURITY = {
  'content-security-policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data: blob:; font-src 'self'; base-uri 'none'; object-src 'none'; frame-ancestors 'none'; form-action 'self'",
  'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer',
  'x-frame-options': 'DENY',
  'cache-control': 'no-store',
};

/** Same loopback listener and model runner serve both the UI and native API. */
export function createPlatformServer(options) {
  return createServer({ ...options, version: `${PLATFORM.version}-guilty-spark-343`,
    async handleRequest({ req, res, route }) {
      if (route === '/favicon.ico' && ['GET', 'HEAD'].includes(req.method)) {
        res.writeHead(204, SECURITY); res.end(); return true;
      }
      const asset = ASSETS.get(route);
      if (route !== '/api/platform' && !asset) return false;
      if (!['GET', 'HEAD'].includes(req.method)) {
        res.writeHead(405, { ...SECURITY, allow: 'GET, HEAD', 'content-type': 'application/json' });
        res.end(JSON.stringify({ error: 'method not allowed' }));
        return true;
      }
      const body = asset ? await readFile(new URL(asset[0], import.meta.url)) : Buffer.from(JSON.stringify(PLATFORM));
      res.writeHead(200, { ...SECURITY, 'content-type': asset?.[1] ?? 'application/json; charset=utf-8', 'content-length': body.length });
      res.end(req.method === 'HEAD' ? undefined : body);
      return true;
    },
  });
}
