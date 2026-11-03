import { createHash } from 'node:crypto';
import { lstat, open, opendir, realpath, stat } from 'node:fs/promises';
import path from 'node:path';

const MAX_MANIFESTS = 10_000;
const MAX_ENTRIES = 50_000;
const MAX_METADATA = 1024 * 1024;
const MAX_SCAN_METADATA = 32 * 1024 * 1024;
const MAX_DEPTH = 8;
const DIGEST = /^sha256:[a-f0-9]{64}$/;
const TYPES = 'application/vnd.ollama.image.';
const SUPPORTED_LAYERS = new Set(['model', 'template', 'system', 'params', 'license', 'messages']);

export class ModelRegistryError extends Error {
  constructor(message, code = 'invalid_model', statusCode = 400) {
    super(message);
    this.name = 'ModelRegistryError';
    this.code = code;
    this.statusCode = statusCode;
    this.status = statusCode;
  }
}

function within(root, candidate) {
  const relative = path.relative(root, candidate);
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative));
}

function normalizeName(input) {
  if (typeof input !== 'string' || !input || input.length > 512 || /[\s\\\x00-\x1f]/.test(input)) {
    throw new ModelRegistryError('Model name must be a nonempty registry name without whitespace or backslashes.');
  }
  const parts = input.split('/');
  if (parts.some(part => !part || part === '.' || part === '..')) throw new ModelRegistryError('Invalid model name.');
  if (parts[0] === 'registry.ollama.ai') parts.shift();
  if (parts[0] === 'library') parts.shift();
  const last = parts.at(-1);
  if (!last || !/^[a-zA-Z0-9_.-]+(?::[a-zA-Z0-9_.-]+)?$/.test(last)) throw new ModelRegistryError('Invalid model name or tag.');
  if (!last.includes(':')) parts[parts.length - 1] += ':latest';
  return parts.join('/');
}

function detailsFrom(config) {
  return {
    parent_model: '',
    format: config.model_format || 'gguf',
    family: config.model_family || '',
    families: Array.isArray(config.model_families) ? config.model_families : (config.model_family ? [config.model_family] : []),
    parameter_size: config.model_type || '',
    quantization_level: config.file_type || '',
  };
}

/** Read-only reader for the legacy Ollama manifests/blobs store.
 * Explicit models are operator-selected files: new ModelRegistry(root, { models: [{ name, path }] }).
 * Listing preserves unsupported stores/layers with a reason; resolving one refuses execution.
 * supported means the local manifest/GGUF container is readable. It is not proof
 * that the selected llama.cpp build implements its architecture or has enough memory.
 */
export class ModelRegistry {
  #scanPromise = null;

  constructor(root, { models = [], maxMetadataBytes = MAX_SCAN_METADATA } = {}) {
    if (typeof root !== 'string' || !root) throw new ModelRegistryError('A model store directory is required.');
    if (!Array.isArray(models) || models.length > MAX_MANIFESTS) throw new ModelRegistryError('Invalid explicit model list.');
    if (!Number.isSafeInteger(maxMetadataBytes) || maxMetadataBytes < 1 || maxMetadataBytes > MAX_SCAN_METADATA) throw new ModelRegistryError('Invalid aggregate metadata limit.');
    this.root = path.resolve(root);
    this.maxMetadataBytes = maxMetadataBytes;
    this.explicitModels = models.map(model => ({ name: normalizeName(model.name), path: path.resolve(model.path) }));
  }

  async contained(candidate) {
    const resolved = await realpath(candidate);
    if (!within(await realpath(this.root), resolved)) {
      throw new ModelRegistryError('A model-store path resolves outside the selected store.', 'unsafe_model_path');
    }
    return resolved;
  }

  async metadata(candidate, budget) {
    const safe = await this.contained(candidate);
    const info = await stat(safe);
    if (!info.isFile() || info.size > MAX_METADATA) throw new ModelRegistryError('Model metadata is not a file or exceeds the 1 MiB limit.');
    if (budget && budget.used + info.size > this.maxMetadataBytes) throw new ModelRegistryError('Model store exceeds the aggregate metadata scan limit (at most 32 MiB).', 'model_store_limit', 413);
    const handle = await open(safe, 'r');
    try {
      const buffer = Buffer.alloc(MAX_METADATA + 1);
      let length = 0;
      while (length < buffer.length) {
        const { bytesRead } = await handle.read(buffer, length, buffer.length - length, length);
        if (!bytesRead) break;
        length += bytesRead;
      }
      if (length > MAX_METADATA) throw new ModelRegistryError('Model metadata exceeds the 1 MiB limit.');
      if (budget) {
        budget.used += length;
        if (budget.used > this.maxMetadataBytes) throw new ModelRegistryError('Model store exceeds the aggregate metadata scan limit (at most 32 MiB).', 'model_store_limit', 413);
      }
      return { bytes: buffer.subarray(0, length), info };
    } finally { await handle.close(); }
  }

  async blob(descriptor, { metadata = false, gguf = false, budget } = {}) {
    if (!descriptor || !DIGEST.test(descriptor.digest) || !Number.isSafeInteger(descriptor.size) || descriptor.size < 0) {
      throw new ModelRegistryError('Invalid blob digest or size in model manifest.');
    }
    const location = await this.contained(path.join(this.root, 'blobs', descriptor.digest.replace(':', '-')));
    const info = await stat(location);
    if (!info.isFile() || info.size !== descriptor.size) throw new ModelRegistryError('A model blob is missing, incomplete, or has the wrong size.');
    if (gguf) await checkGguf(location);
    if (!metadata) return { location, info };
    const { bytes } = await this.metadata(location, budget);
    if (`sha256:${createHash('sha256').update(bytes).digest('hex')}` !== descriptor.digest) {
      throw new ModelRegistryError('Model metadata does not match its SHA256 digest.');
    }
    return { location, info, bytes };
  }

  async manifests() {
    const manifests = path.join(this.root, 'manifests');
    try { await this.contained(manifests); } catch (error) {
      if (error.code === 'ENOENT') return [];
      throw error;
    }
    const files = [];
    let entriesSeen = 0;
    const walk = async (directory, parts = []) => {
      if (parts.length > MAX_DEPTH) throw new ModelRegistryError('Model manifest directory nesting exceeds the supported limit.');
      for await (const entry of await opendir(directory)) {
        entriesSeen++;
        if (entriesSeen > MAX_ENTRIES) throw new ModelRegistryError('Model store exceeds the 50,000-entry scan limit.');
        const location = path.join(directory, entry.name);
        await this.contained(location);
        if (entry.isSymbolicLink()) throw new ModelRegistryError('Symlinks in the manifest tree are unsupported.', 'unsafe_model_path');
        if (entry.isDirectory()) await walk(location, [...parts, entry.name]);
        else if (entry.isFile()) {
          files.push({ location, parts: [...parts, entry.name] });
          if (files.length > MAX_MANIFESTS) throw new ModelRegistryError('Model store exceeds the 10,000-manifest scan limit.');
        }
      }
    };
    await walk(manifests);
    return files;
  }

  async manifestModel({ location, parts }, budget) {
    const rawName = parts.length >= 4 ? `${parts.slice(0, -1).join('/')}:${parts.at(-1)}` : parts.join('/');
    const name = normalizeName(rawName);
    const base = { name, model: name, modelPath: null, digest: '', size: 0, modified_at: '', details: detailsFrom({}), template: '', system: '', parameters: {}, licenses: [], capabilities: [], supported: false };
    try {
      if (parts.length < 4) throw new ModelRegistryError('Unsupported model-store layout; expected registry/namespace/model/tag.');
      const { bytes, info } = await this.metadata(location, budget);
      base.digest = createHash('sha256').update(bytes).digest('hex');
      base.modified_at = info.mtime.toISOString();
      const manifest = JSON.parse(bytes.toString('utf8'));
      if (manifest.schemaVersion !== 2 || !Array.isArray(manifest.layers) || manifest.layers.length > 256) {
        throw new ModelRegistryError('Unsupported model manifest schema. Only legacy Ollama GGUF manifests are supported.', 'unsupported_model', 422);
      }
      const configBlob = await this.blob(manifest.config, { metadata: true, budget });
      const config = JSON.parse(configBlob.bytes.toString('utf8'));
      base.details = detailsFrom(config);
      if (config.model_format && config.model_format.toLowerCase() !== 'gguf') {
        throw new ModelRegistryError(`Unsupported model format: ${config.model_format}. Only GGUF is supported.`, 'unsupported_model', 422);
      }
      const models = manifest.layers.filter(layer => layer.mediaType === `${TYPES}model`);
      for (const layer of manifest.layers) {
        if (!DIGEST.test(layer.digest) || !Number.isSafeInteger(layer.size) || layer.size < 0) throw new ModelRegistryError('Invalid model layer digest or size.');
        base.size += layer.size;
        if (!Number.isSafeInteger(base.size)) throw new ModelRegistryError('Model size exceeds the supported range.');
        const type = layer.mediaType?.startsWith(TYPES) ? layer.mediaType.slice(TYPES.length) : layer.mediaType;
        if (!SUPPORTED_LAYERS.has(type)) throw new ModelRegistryError(`Unsupported model layer: ${type || 'unknown'} (including v2 weights, adapters, and projectors).`, 'unsupported_model', 422);
      }
      if (models.length !== 1) throw new ModelRegistryError('Expected one GGUF model layer; split or v2 models are unsupported.', 'unsupported_model', 422);
      const modelBlob = await this.blob(models[0], { gguf: true });
      base.modelPath = modelBlob.location;
      base.modelDigest = models[0].digest;
      for (const layer of manifest.layers) {
        const type = layer.mediaType.slice(TYPES.length);
        if (type === 'model') continue;
        const content = (await this.blob(layer, { metadata: true, budget })).bytes.toString('utf8');
        if (type === 'license') base.licenses.push(content);
        else if (type === 'params') {
          const params = JSON.parse(content);
          if (!params || typeof params !== 'object' || Array.isArray(params)) throw new ModelRegistryError('Model parameters must be a JSON object.');
          base.parameters = params;
        } else if (type === 'messages') {
          throw new ModelRegistryError('Models with stored message-history layers are unsupported.', 'unsupported_model', 422);
        } else base[type] = content;
      }
      base.capabilities = ['completion'];
      base.supported = true;
    } catch (error) {
      if (error.code === 'model_store_limit') throw error;
      base.unsupportedReason = error instanceof ModelRegistryError ? error.message : (error.code === 'ENOENT' ? 'A referenced model blob is missing.' : 'Invalid or unreadable model metadata.');
      base.errorCode = error instanceof ModelRegistryError ? error.code : 'invalid_model';
    }
    return base;
  }

  list() {
    if (!this.#scanPromise) {
      this.#scanPromise = this.#scan().finally(() => { this.#scanPromise = null; });
    }
    return this.#scanPromise;
  }

  async #scan() {
    const result = [];
    const budget = { used: 0 };
    for (const manifest of await this.manifests()) result.push(await this.manifestModel(manifest, budget));
    for (const explicit of this.explicitModels) {
      const location = await realpath(explicit.path);
      await checkGguf(location);
      const info = await stat(location);
      result.push({ name: explicit.name, model: explicit.name, modelPath: location, digest: '', size: info.size, modified_at: info.mtime.toISOString(), details: detailsFrom({}), template: '', system: '', parameters: {}, licenses: [], capabilities: ['completion'], supported: true });
    }
    const names = new Set();
    for (const model of result) {
      if (names.has(model.name)) throw new ModelRegistryError(`Duplicate model name: ${model.name}.`);
      names.add(model.name);
    }
    return result.sort((a, b) => a.name.localeCompare(b.name));
  }

  async resolve(name) {
    const canonical = normalizeName(name);
    const model = (await this.list()).find(item => item.name === canonical);
    if (!model) throw new ModelRegistryError(`Model '${name}' was not found in the selected local store.`, 'model_not_found', 404);
    if (!model.supported) throw new ModelRegistryError(model.unsupportedReason, model.errorCode || 'unsupported_model', 422);
    return model;
  }
}

async function checkGguf(location) {
  const info = await lstat(location);
  if (!info.isFile()) throw new ModelRegistryError('Model is not a regular GGUF file.', 'unsupported_model', 422);
  const handle = await open(location, 'r');
  try {
    const header = Buffer.alloc(8);
    const { bytesRead } = await handle.read(header, 0, 8, 0);
    if (bytesRead !== 8 || header.toString('ascii', 0, 4) !== 'GGUF' || ![2, 3].includes(header.readUInt32LE(4))) {
      throw new ModelRegistryError('Model is not a supported GGUF v2/v3 file; MLX and Ollama v2 tensor stores are unsupported.', 'unsupported_model', 422);
    }
  } finally { await handle.close(); }
}
