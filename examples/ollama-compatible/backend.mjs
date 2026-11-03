import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import net from 'node:net';
import { setTimeout as delay } from 'node:timers/promises';

export class BackendError extends Error {
  constructor(message, code = 'backend_error', statusCode = 503) {
    super(message);
    this.name = 'BackendError';
    this.code = code;
    this.statusCode = statusCode;
    this.status = statusCode;
  }
}

function combine(...signals) {
  return AbortSignal.any(signals.filter(Boolean));
}

function keepAliveMs(value, fallback) {
  if (value === undefined || value === null) return fallback;
  if (typeof value === 'number' && Number.isFinite(value)) return value < 0 ? Infinity : Math.min(value * 1000, 2_147_483_647);
  if (typeof value !== 'string' || !/^-?(?:\d+(?:\.\d+)?(?:ms|s|m|h)?)+$/.test(value)) {
    throw new BackendError('keep_alive must be seconds or a duration such as 5m.', 'invalid_keep_alive', 400);
  }
  if (value.startsWith('-')) return Infinity;
  let total = 0;
  for (const match of value.matchAll(/(\d+(?:\.\d+)?)(ms|s|m|h)?/g)) {
    total += Number(match[1]) * ({ ms: 1, s: 1000, m: 60_000, h: 3_600_000 }[match[2] || 's']);
  }
  return Math.min(total, 2_147_483_647);
}

async function freePort() {
  const reservation = net.createServer();
  await new Promise((resolve, reject) => {
    reservation.once('error', reject);
    reservation.listen(0, '127.0.0.1', resolve);
  });
  const port = reservation.address().port;
  await new Promise((resolve, reject) => reservation.close(error => error ? reject(error) : resolve()));
  return port;
}

/** Owns one isolated llama-server process. Callers consume the Response body
 * inside withModel: ownership lasts until the callback settles, including streaming.
 */
export class RunnerManager {
  #queue = [];
  #busy = false;
  #pending = 0;
  #unloads = new Map();
  #closePromise = null;
  #state = null;
  #timer = null;
  #closed = false;
  #shutdown = new AbortController();

  constructor({ executable, contextSize = 2048, threads = 4, gpuLayers = 0, loadTimeoutMs = 120_000, idleMs = 300_000, maxPendingRequests = 32 } = {}) {
    // An argv prefix also permits a synthetic Node runner in lifecycle tests.
    this.command = Array.isArray(executable) ? [...executable] : [executable];
    if (!this.command.length || this.command.some(arg => typeof arg !== 'string' || !arg)) throw new BackendError('Provide a llama-server executable path.', 'backend_not_configured');
    for (const [name, value, min, max] of [['contextSize', contextSize, 128, 1_048_576], ['threads', threads, 1, 1024], ['gpuLayers', gpuLayers, -1, 1_048_576], ['loadTimeoutMs', loadTimeoutMs, 1, 2_147_483_647], ['idleMs', idleMs, 0, 2_147_483_647], ['maxPendingRequests', maxPendingRequests, 1, 4096]]) {
      if (!Number.isInteger(value) || value < min || value > max) throw new BackendError(`Invalid ${name}.`, 'invalid_backend_config', 400);
    }
    Object.assign(this, { contextSize, threads, gpuLayers, loadTimeoutMs, idleMs, maxPendingRequests });
  }

  async #exclusive(callback, signal, admission = false) {
    signal?.throwIfAborted();
    if (admission && this.#pending >= this.maxPendingRequests) {
      throw new BackendError('The inference queue is full. Retry after current requests finish.', 'backend_queue_full', 503);
    }
    if (admission) this.#pending++;
    return new Promise((resolve, reject) => {
      const entry = { callback, signal, admission, resolve, reject, started: false, abort: null };
      if (signal) {
        entry.abort = () => {
          if (entry.started) return;
          const index = this.#queue.indexOf(entry);
          if (index < 0) return;
          this.#queue.splice(index, 1);
          if (admission) this.#pending--;
          signal.removeEventListener('abort', entry.abort);
          reject(signal.reason || new DOMException('Aborted', 'AbortError'));
        };
        signal.addEventListener('abort', entry.abort, { once: true });
      }
      this.#queue.push(entry);
      if (signal?.aborted) entry.abort();
      this.#drain();
    });
  }

  #drain() {
    if (this.#busy) return;
    const entry = this.#queue.shift();
    if (!entry) return;
    this.#busy = true;
    entry.started = true;
    if (entry.abort) entry.signal.removeEventListener('abort', entry.abort);
    const finish = (succeeded, result) => {
      this.#busy = false;
      if (entry.admission) this.#pending--;
      if (succeeded) entry.resolve(result);
      else entry.reject(result);
      queueMicrotask(() => this.#drain());
    };
    Promise.resolve().then(() => {
      entry.signal?.throwIfAborted();
      return entry.callback();
    }).then(result => finish(true, result), error => finish(false, error));
  }

  /** Number of accepted inference calls, including the active call. */
  pending() { return this.#pending; }

  loaded() {
    const state = this.#state;
    if (!state?.ready || state.exited) return null;
    return { model: state.model, loadedAt: state.loadedAt, expiresAt: state.expiresAt, loadDuration: state.loadDuration, contextSize: this.contextSize, pid: state.child.pid };
  }

  async withModel(model, { signal, keepAlive } = {}, callback) {
    if (this.#closed) throw new BackendError('The inference manager is closed.', 'backend_closed');
    if (!model?.modelPath || !model?.name) throw new BackendError('A resolved GGUF model is required.', 'invalid_model', 400);
    if (typeof callback !== 'function') throw new TypeError('withModel requires a callback.');
    const idle = keepAliveMs(keepAlive, this.idleMs);
    const combined = combine(signal, this.#shutdown.signal);
    return this.#exclusive(async () => {
      combined.throwIfAborted();
      clearTimeout(this.#timer);
      this.#timer = null;
      let newlyLoaded = false;
      if (!this.#state?.ready || this.#state.exited || this.#state.model.modelPath !== model.modelPath || this.#state.model.digest !== model.digest) {
        await this.#stop();
        await this.#start(model, combined);
        newlyLoaded = true;
      }
      const state = this.#state;
      state.expiresAt = null;
      let active = true;
      const runner = {
        baseUrl: state.baseUrl,
        loadedAt: state.loadedAt,
        loadDuration: newlyLoaded ? state.loadDuration : 0,
        request: async (route, body, { signal: requestSignal } = {}) => {
          if (!active) throw new BackendError('The inference request owner has finished.', 'backend_request_finished');
          if (!['/completion', '/v1/chat/completions', '/embedding', '/v1/embeddings', '/tokenize', '/detokenize'].includes(route)) throw new BackendError('Unsupported inference backend route.', 'unsupported_backend_route', 400);
          if (state.exited || this.#state !== state) throw new BackendError('The inference process exited.', 'backend_exited');
          return fetch(`${state.baseUrl}${route}`, {
            method: 'POST',
            headers: { 'content-type': 'application/json', authorization: `Bearer ${state.key}` },
            body: JSON.stringify(body),
            signal: combine(combined, requestSignal, state.controller.signal),
          });
        },
      };
      try { return await callback(runner); }
      finally {
        active = false;
        if (this.#state === state && !this.#closed) {
          if (idle === 0 || combined.aborted) await this.#stop();
          else if (Number.isFinite(idle)) {
            state.expiresAt = new Date(Date.now() + idle).toISOString();
            this.#timer = setTimeout(() => {
              void this.#exclusive(async () => {
                if (this.#state === state && state.expiresAt && Date.parse(state.expiresAt) <= Date.now()) await this.#stop();
              }).catch(() => {});
            }, idle);
            this.#timer.unref();
          }
        }
      }
    }, combined, true);
  }

  async #start(model, signal) {
    const port = await freePort();
    signal.throwIfAborted();
    const key = randomUUID();
    const alias = `reclaimer-${randomUUID()}`;
    const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => !key.startsWith('LLAMA_ARG_') && key !== 'LLAMA_API_KEY'));
    const child = spawn(this.command[0], [...this.command.slice(1),
      '--model', model.modelPath, '--host', '127.0.0.1', '--port', String(port),
      '--ctx-size', String(this.contextSize), '--threads', String(this.threads),
      '--n-gpu-layers', String(this.gpuLayers), '--parallel', '1', '--alias', alias,
      '--api-key', key, '--no-webui', '--offline', '--no-mmproj',
    ], { shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
    const started = process.hrtime.bigint();
    const state = { model, child, key, alias, baseUrl: `http://127.0.0.1:${port}`, ready: false, exited: false, controller: new AbortController(), logTail: '', expiresAt: null };
    this.#state = state;
    state.exitPromise = new Promise(resolve => {
      child.once('error', error => {
        state.startError = error;
        state.exited = true;
        state.controller.abort(new BackendError('The inference process could not start.', 'backend_start_failed'));
        resolve();
      });
      child.once('exit', (code, signal) => {
        state.exited = true;
        state.exitCode = code;
        state.exitSignal = signal;
        state.controller.abort(new BackendError('The inference process exited.', 'backend_exited'));
        resolve();
      });
    });
    const logs = chunk => { state.logTail = (state.logTail + chunk.toString('utf8')).slice(-16_384); };
    child.stdout.on('data', logs);
    child.stderr.on('data', logs);
    const deadline = Date.now() + this.loadTimeoutMs;
    try {
      while (Date.now() < deadline) {
        signal.throwIfAborted();
        if (state.exited) throw new BackendError(state.startError?.code === 'ENOENT' ? 'The configured llama-server executable was not found.' : 'The inference process exited before becoming ready. Verify the executable and GGUF compatibility.', 'backend_start_failed');
        try {
          const probeSignal = combine(signal, state.controller.signal, AbortSignal.timeout(Math.min(1000, Math.max(1, deadline - Date.now()))));
          const health = await fetch(`${state.baseUrl}/health`, { signal: probeSignal });
          const healthy = health.ok && (await health.json()).status === 'ok';
          if (!health.ok) await health.body?.cancel();
          if (!healthy) {
            await delay(100, undefined, { signal });
            continue;
          }
          const response = await fetch(`${state.baseUrl}/v1/models`, { headers: { authorization: `Bearer ${key}` }, signal: probeSignal });
          if (response.ok) {
            const json = await response.json();
            if (Array.isArray(json.data) && json.data.some(model => model.id === alias) && !state.exited) {
              state.ready = true;
              state.loadedAt = new Date().toISOString();
              state.loadDuration = Number(process.hrtime.bigint() - started);
              return;
            }
          } else await response.body?.cancel();
        } catch (error) {
          if (signal.aborted) throw signal.reason;
          if (state.exited) throw new BackendError('The inference process exited before becoming ready.', 'backend_start_failed');
        }
        await delay(Math.min(100, Math.max(1, deadline - Date.now())), undefined, { signal });
      }
      throw new BackendError('Timed out loading the GGUF model.', 'backend_load_timeout', 504);
    } catch (error) {
      await this.#stop();
      throw error;
    }
  }

  async #stop() {
    clearTimeout(this.#timer);
    this.#timer = null;
    const state = this.#state;
    if (!state) return;
    state.ready = false;
    state.controller.abort(new BackendError('The inference process is stopping.', 'backend_stopping'));
    if (!state.exited) {
      state.child.kill('SIGTERM');
      const stopped = await Promise.race([state.exitPromise.then(() => true), delay(1500, undefined, { ref: false }).then(() => false)]);
      if (!stopped && !state.exited) {
        state.child.kill('SIGKILL');
        await Promise.race([state.exitPromise, delay(1500, undefined, { ref: false })]);
      }
      if (!state.exited) throw new BackendError('The owned inference process did not stop; refusing to start another.', 'backend_stop_failed');
    }
    if (this.#state === state) this.#state = null;
  }

  unload(name) {
    if (name !== undefined && this.#state?.model.name !== name) return Promise.resolve();
    if (this.#unloads.has(name)) return this.#unloads.get(name);
    // Cleanup bypasses inference admission, but repeated cleanup requests share
    // a single queue entry per currently loaded name (or an unconditional entry).
    const pending = this.#exclusive(async () => {
      if (name === undefined || this.#state?.model.name === name) await this.#stop();
    }).finally(() => this.#unloads.delete(name));
    this.#unloads.set(name, pending);
    return pending;
  }

  close() {
    if (this.#closePromise) return this.#closePromise;
    this.#closed = true;
    this.#shutdown.abort(new BackendError('The inference manager is closing.', 'backend_closed'));
    this.#closePromise = this.#exclusive(() => this.#stop());
    return this.#closePromise;
  }
}
