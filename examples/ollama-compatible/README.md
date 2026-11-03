# Ollama-compatible local inference with Reclaimer

A working local server implementing a defined subset of the Ollama HTTP API.
It reads existing Ollama GGUF models and runs them through an independently
managed llama.cpp process. No running Ollama server is required.

This was built using Reclaimer analysis of the official Ollama JavaScript client,
Ollama source/documentation, independent protocol tests, and real local inference.
The source identities, analysis receipts, and remaining reconstruction obligations
are recorded in [EVIDENCE.md](EVIDENCE.md).

For the combined chat interface and GobboNet JSON imports, launch
[Guilty Spark 343](../guilty-spark-343/README.md), which shares this API and runner.

## Run

Requirements: Node.js 24.11 or newer, an explicit llama.cpp `llama-server`
executable, and an existing legacy Ollama models directory containing `manifests/`
and `blobs/`. The implementation uses Node built-ins only.

From the Reclaimer repository:

```powershell
node examples/ollama-compatible/cli.mjs --llama-server <path-to-llama-server.exe> --models-root <path-to-Ollama-models>
```

The server listens on **http://127.0.0.1:11435**. Use `--port 11434` when you
deliberately want Ollama's conventional port and it is available. Startup fails
on an occupied port. `Ctrl+C` closes the API and its owned inference process.

For this machine, an ignored local configuration has already been prepared:

```powershell
node examples/ollama-compatible/cli.mjs --config config/local-ollama-compatible.json
```

That configuration selects the existing model store and an isolated CPU backend
download. It is host-specific evidence and is not part of the public source.
Nothing was installed globally or registered as a Windows service.

In another terminal:

```powershell
$request = @{
  model = 'deepseek-coder:latest'
  prompt = 'Write a Python function that adds two numbers.'
  stream = $false
  options = @{ temperature = 0; num_predict = 80 }
} | ConvertTo-Json -Depth 5
Invoke-RestMethod 'http://127.0.0.1:11435/api/generate' -Method Post -ContentType 'application/json' -Body $request
```

Point an Ollama API client at `http://127.0.0.1:11435`. This is an HTTP API server,
not a replacement for the Ollama command-line executable or desktop application.
Clients that require unsupported operations receive an explicit error.

`--list` prints the inventory without starting an inference process. `--help`
describes context size, threads and GPU-layer selection. Default inference uses
four CPU threads and a 2,048-token context. A suitable GPU backend can be selected
with `--gpu-layers`; GPU inference was not validated in this delivery.

Local JSON configuration also accepts an optional `models` array of `{ "name":
"my-model", "path": "../weights/model.gguf" }` entries. Paths resolve relative to
the configuration file, allowing an explicitly selected standalone GGUF without
copying or downloading weights. This does not make unsupported model architectures
or templates compatible with the selected runtime.

## Implemented compatibility

| Surface | Behavior |
| --- | --- |
| `GET /api/version` | Honest version `0.1.0-reclaimer` |
| `GET /api/tags` | Read-only inventory of legacy local Ollama manifests |
| `POST /api/show` | Declared metadata, system text, template, parameters and licenses |
| `GET /api/ps` | Model resident in this implementation's owned runner |
| `POST /api/generate` | Real generation; JSON or NDJSON; raw mode; supported templates; JSON Schema |
| `POST /api/chat` | Text conversations and history; JSON or NDJSON; JSON Schema |
| Empty prompt/messages | Load the named model; `keep_alive:0` unloads that model |
| `GET /v1/models` | OpenAI-shaped discovery only |
| Errors | JSON `error` strings; midstream errors remain NDJSON without false completion |

Streaming defaults to true. Each native stream terminates with `done:true` after
the backend reports completion. Generated token counts, prompt/cache counts and
evaluation durations come from backend observations; wall-clock and load durations
are measured locally. Missing backend statistics stay absent. Durations use
nanoseconds.

Supported request options: `temperature`, `seed`, `top_k`, `top_p`, `min_p`,
`repeat_penalty`, `num_predict` (1–32,768), and string-array `stop`. The default
generation cap is **256 tokens**, which is a deliberate difference from Ollama.
Unsupported options fail visibly, including unsupported defaults in a manifest.

Legacy templates containing only `.System`, `.Prompt`, and `.Response` substitutions
are supported. Text chat using these templates requires alternating user/assistant
turns, ending with a user turn; system overrides precede the conversation.
Without a legacy template, non-raw requests delegate formatting to the GGUF chat
template through llama.cpp. `raw:true` bypasses template/system formatting for
generation. Arbitrary Go-template logic is not implemented.

## Limits

- This is a local text-inference subset, not full Ollama equivalence. No bitwise
  output parity, complete default-parameter parity, or performance parity is claimed.
- The registry reads legacy manifests and blobs. It does not implement v2 manifest
  lists, cloud models, split model layers, projectors, adapters, stored message
  layers, pull/push, model creation, deletion, or registry distribution. Unsupported
  manifests stay visible in inventory and are refused for inference.
- Container readability is not proof that llama.cpp supports an architecture or
  that the host has enough memory. Only `deepseek-coder:latest` was live-qualified.
- Images, audio, tools, thinking controls, custom templates, suffix/infill,
  deprecated context arrays, and OpenAI generation routes return HTTP 501.
- **Embeddings are unavailable in the shipped completion-mode runner.** The
  HTTP translation has synthetic tests and an injection point for an embedding
  backend, but the CLI cannot currently configure one. `/api/embed` therefore
  returns 501 for this registry; it does not manufacture vectors.
- One inference request owns the runner at a time. Up to 32 calls, including the
  active call, are admitted. Overflow returns 503; queued cancellation releases
  admission immediately. Model switches are serialized.
- Requests have a 1 MiB body limit and a five-minute deadline. Model loading has a
  two-minute deadline. Metadata scans have per-file, entry, depth and 32 MiB aggregate
  limits. Concurrent scans share in-progress work; no persistent scan cache hides
  changed manifests.
- The public API is loopback-only and refuses non-loopback Host headers and
  cross-origin browser requests. The inference child uses a separate ephemeral
  loopback port and temporary authentication, with no web UI and offline mode.
  This is a local process boundary, not an OS sandbox or multi-user access control.
- Source model blobs are opened read-only. Metadata hashes and sizes are checked;
  complete weight hashes are recorded by the live smoke rather than recomputed on
  every request. Concurrent external changes to model weights are not prevented.

## Verification

Run the protocol, model-store and lifecycle tests:

The verified delivery passed **44 tests, with zero failures or skips**. Reclaimer's
repository build check also passed: 233 JavaScript modules checked, all 139 MCP
operations present, and all six prompts retained.

```powershell
node --test --test-timeout=15000 examples/ollama-compatible/server.test.mjs examples/ollama-compatible/backend.test.mjs examples/ollama-compatible/registry.test.mjs
```

Run the real inference smoke with your explicitly selected local configuration:

```powershell
node examples/ollama-compatible/smoke.mjs --config config/local-ollama-compatible.json --model deepseek-coder:latest
```

The smoke creates a temporary loopback API, performs 13 checks, writes a fresh
receipt under ignored `output/ollama-reconstruction/live-*/receipt.json`, hashes
the weights before and after, and shuts down its owned processes. It verifies
generation, chat/history, NDJSON, JSON-schema output, discovery, errors and unload.
Assertions check actual output and backend token counts, not a canned response.

The first live run returned **"The answer to 2 + 2 is 4."**, generated a Python
addition function, and produced the constrained JSON object `{"answer":4}`.
All 13 checks passed and the original model hash was unchanged. Synthetic tests
exercise paths that the live run does not, including timeouts, queue pressure,
broken streams, malformed metadata and cancellation.

The downloaded CPU backend was the official
[llama.cpp b11669 release](https://github.com/ggml-org/llama.cpp/releases/tag/b11669),
selected by v0.7.0's nightly tag. The Windows CPU x64 archive SHA-256 was checked
against GitHub's release digest:
`37b3efcf962847ca869745fd4ccf7f5e7e8dacfeff9fa3523b4ca8cdc66d632b`.
Binary files, local configuration, model weights and raw receipts are excluded
from source publication. Existing upstream and model licenses still apply.

## Implementation map

```text
Ollama HTTP client
  -> server.mjs: validation, native JSON/NDJSON, template and response mapping
  -> model-registry.mjs: read-only legacy manifest/blob resolution
  -> backend.mjs: bounded queue, owned child process, keep-alive and cancellation
  -> external llama-server: actual GGUF inference
```

`cli.mjs` starts a foreground local server. `smoke.mjs` retains end-to-end evidence.
No existing Reclaimer CLI/MCP contracts or Mega-Crossover runtime were changed.
