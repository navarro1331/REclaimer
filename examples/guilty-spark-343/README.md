# Guilty Spark 343

A local platform built with Reclaimer: an Ollama-compatible text API and a GobboNet-inspired chat workspace use the same model runner. This is an independent implementation of the supported behaviors, not the original Ollama or GobboNet server.

A [native Windows desktop application written in C](native/README.md) is also available. It provides local GGUF inference, an Ollama-compatible API client, conversations, characters and GobboNet JSON import without requiring Node.js or a browser. The instructions below describe the web/API edition.

## Start

Requires Node 24+, an existing llama.cpp `llama-server` executable, and existing compatible GGUF weights. From the Reclaimer repository:

```powershell
node examples/guilty-spark-343/cli.mjs --config config/local-ollama-compatible.json --port 11436
```

Open **http://127.0.0.1:11436**. The earlier local configuration works unchanged; this command selects 11436 because an existing service currently occupies 11435 on the development machine. The generic default remains 11435. The server binds only to loopback. Ctrl+C closes the server and its owned inference process. No service is installed and no client registration is changed.

For a fresh machine, create an ignored `config/local-guilty-spark-343.json` using your own paths:

```json
{
  "executable": "../.cache/runtime/llama-server.exe",
  "modelsRoot": "../.cache/models",
  "models": [{ "name": "my-model", "path": "../.cache/my-model.gguf" }],
  "port": 11435,
  "contextSize": 2048,
  "threads": 4,
  "gpuLayers": 0
}
```

Then pass that file with `--config`. Paths in configuration are relative to the configuration file. `models` is optional and lets you explicitly reuse GGUF files from GobboNet or another model directory without copying them. `modelsRoot` reads the legacy Ollama manifests/blobs store; it may point to a nonexistent empty store when using only explicit models. Use `--list` to inspect model metadata without starting inference. A listed model is not a guarantee that the selected backend supports its architecture or fits in memory.

## Shared workspace

- Stream a conversation, stop generation, and choose a model for the next request.
- Create, rename, pin, and delete conversations. History lives in this browser and origin, under `guilty-spark-343.v1`; export a backup before clearing browser data or changing ports.
- Create characters and set a user persona. Character instructions are sent as text to the selected model.
- Import GobboNet JSON conversation backups and flat, V2, or V3 character JSON. Export a sanitized Guilty Spark 343 JSON backup.
- Use the same listener from Ollama-compatible clients: `/api/tags`, `/api/show`, `/api/ps`, `/api/chat`, and `/api/generate`. `/api/platform` describes the platform. See the [API compatibility matrix](../ollama-compatible/README.md).

Import is explicit: the application does not read an existing GobboNet browser profile or private state file. Import allowlists supported fields and warns when unsupported data is omitted. Passwords, API keys, custom JavaScript, extensions, macros, schedules, and remote integrations are not imported or executed. Review character text before using it, since it becomes model instructions.

The implementation is text-only and loads one model at a time. PNG character cards, lorebook retrieval/RAG, web search, GobboNet background jobs, server-side state synchronization, model downloads, and the full OpenAI API are not implemented. Model parameters and template support follow the existing compatibility matrix. Long conversations can exceed the configured context window; start a new conversation or increase `contextSize` within the model and machine limits.

Native GobboNet `writingStyle`, authored lore, running thread lore, greetings, conversation order, and the selected enabled persona are converted to text. Original persona injection frequency, character sampler settings, message variants, reasoning metadata, folders, and automation are omitted with warnings. No persona is activated when the source has none selected. Limits are 2 MiB per imported backup, 100 conversations, 200 characters, and 50,000 characters per message; requests reserve space for a new reply before accepting more history.

## Verification

```powershell
node --test --test-timeout=15000 examples/ollama-compatible/*.test.mjs examples/guilty-spark-343/*.test.mjs
node scripts/check.mjs
```

The suite exercises protocol conversion, process lifecycle, registry limits, HTTP boundaries, state migration and import sanitization. The [evidence record](EVIDENCE.md) distinguishes source analysis from live behavior. The runtime and model licenses remain their owners' terms; see [notices](THIRD_PARTY_NOTICES.md).
