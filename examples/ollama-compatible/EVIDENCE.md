# Ollama compatibility reconstruction evidence

This example reconstructs a selected local Ollama HTTP surface around an
independently written Node.js server and an explicitly configured llama.cpp
`llama-server` process. Its inference path does not require an Ollama process.
The scope is a useful local implementation, not a claim of full Ollama parity.

Evidence reviewed on 2026-10-22 is separated below into public source facts,
Reclaimer analysis, implementation choices, and verification limits. No REA
implementation, fixtures, authored prompts, or prose were used.

## Source identities

| Input | Pinned identity | Purpose |
| --- | --- | --- |
| Ollama server | [v0.40.7, `2cd63f7a15039ccd790e97b31edc091fa1707c56`](https://github.com/ollama/ollama/releases/tag/v0.40.7) | Server architecture and endpoint relationships |
| Official Ollama JavaScript client | [`c276d078eee37a0df003b9ee2120ec70327f32e9`](https://github.com/ollama/ollama-js/tree/c276d078eee37a0df003b9ee2120ec70327f32e9) | Selected client request, stream, completion, cancellation, and error contracts |
| llama.cpp | [v0.7.0, `620d2225cc436ab0b001e3200e590e3b286ef9ab`](https://github.com/ggml-org/llama.cpp/commit/620d2225cc436ab0b001e3200e590e3b286ef9ab) | External inference backend protocol |
| Ollama API documentation | Current pages linked below | Published HTTP contract; these pages can change independently of the source pins |

The Ollama release index also exposed a v0.40.8 prerelease during this review.
The current documentation already describes audio features introduced in 0.40.8;
they are not inferred to exist in the pinned 0.40.7 server.

## Reclaimer was used on actual artifacts

The `skills/real/SKILL.md` workflow was applied through Reclaimer's discovery
manifest and `analyze-javascript-application` operation. Six selected official
SDK files were fetched from the pinned commit into an ignored evidence directory:
`src/browser.ts`, `src/utils.ts`, `src/interfaces.ts`, `src/constant.ts`,
`package.json`, and `LICENSE`. Each file has a URL and SHA-256 receipt.
The SDK source was analyzed as data and was not executed or incorporated into
the implementation.

Local evidence lives under
`output/ollama-reconstruction/source-map-evidence/`:

| Receipt | Observed result |
| --- | --- |
| `source-receipt.json` | Original URLs and six file digests |
| `sdk-analysis.json` | Four TypeScript modules parsed, 4,152 AST nodes visited, 74 findings, zero parse failures |
| `client-obligations.json` | Six reviewed required client obligations plus 157 generated candidates; six required obligations open, zero verified |
| `build-ledger.mjs` | Reproducible ledger construction using the real Reclaimer engine and retained analysis |

The analysis evidence identifier is
`ev_16c626c57579b0e5f1cd7210a4e8baaade658c9d875f87539ef8f4457fc9fa74`.
The ledger identifier is
`ev_1e4c5c8c8247a0e104ded11f71a62ed6185b1ea4fed50f4b053ebb57a7914e38`.
Its deliberately open state is evidence of a remaining validation obligation,
not a failing runtime test. Static syntax does not prove runtime equivalence.
Only selected SDK files were retained; this is not a complete SDK package or
analysis of Ollama's Go/C++ implementation.

The graph identified `processStreamableRequest`, `generate`, `chat`, `embed`,
and `embeddings` in the browser client. Inspection of their retained source and
the utilities established useful client requirements: the SDK explicitly defaults
`stream` to false, parses newline-delimited objects, throws for an error object,
and requires a terminal completion marker before stream EOF. That SDK default
differs from the server's documented default. References:
[browser client](https://github.com/ollama/ollama-js/blob/c276d078eee37a0df003b9ee2120ec70327f32e9/src/browser.ts),
[stream utilities](https://github.com/ollama/ollama-js/blob/c276d078eee37a0df003b9ee2120ec70327f32e9/src/utils.ts).

## Selected HTTP contract

| Surface | Reconstruction obligation | Official reference |
| --- | --- | --- |
| `GET /api/version` | Identify this implementation honestly; do not impersonate an installed Ollama build | [Server routes](https://github.com/ollama/ollama/blob/2cd63f7a15039ccd790e97b31edc091fa1707c56/server/routes.go) |
| `GET /api/tags` | Return the configured local model inventory | [List models](https://docs.ollama.com/api/tags) |
| `POST /api/show` | Return known model details and declared capabilities without invented metadata | [Show model](https://docs.ollama.com/api-reference/show-model-details) |
| `GET /api/ps` | Report resident models owned by this implementation | [Running models](https://docs.ollama.com/api/ps) |
| `POST /api/generate` | Accept a model and prompt; text appears in `response`; streaming defaults to true | [Generate](https://docs.ollama.com/api/generate) |
| `POST /api/chat` | Accept message history; return `message.role` and `message.content`; streaming defaults to true | [Chat](https://docs.ollama.com/api/chat) |
| `POST /api/embed` | Contract translation is defined for text/vector arrays, but the current completion-only registry cannot run embeddings; requests fail explicitly | [Embed](https://docs.ollama.com/api/embed) |

For chat and generation, partial native responses use
`application/x-ndjson`, one JSON object per line, with `done:false`; completion
must include `done:true`. `stream:false` returns a single JSON response.
The native protocol does not use an SSE `[DONE]` terminator.
[Streaming contract](https://docs.ollama.com/api/streaming)

Invalid bodies/parameters use HTTP 400 and missing models use HTTP 404. Error
responses have a string `error` field. After response bytes have been sent, a
failure is another NDJSON error object; the HTTP status cannot then be changed.
An interrupted or failed stream must not be reported as successful completion.
[Error contract](https://docs.ollama.com/api/errors)

Reported durations are nanoseconds. Token counts and evaluation timings must
come from backend measurements; elapsed wall time is not an evaluation-token
count. Embedding truncation defaults to true in Ollama; requests that explicitly
disable it must fail if the context limit is exceeded. Dimensionality and
normalization need model-appropriate verification.
[Generation metrics](https://docs.ollama.com/api/generate),
[embedding options](https://docs.ollama.com/api/embed)

## Why llama.cpp is the backend

At the pinned Ollama version, `llm/server.go` delegates GGUF inference to an
upstream llama-server subprocess. The wrapper offers two chat paths: native
Ollama prompt rendering followed by `/completion`, or the runner's chat-template
path through `/v1/chat/completions`. Scheduler ownership, model resolution and
response conversion are separate layers. This supports the reconstruction choice
of writing a small API/registry/process layer while using an existing inference
engine. It does not establish equivalent scheduling, templates, or outputs.
[Inference interface](https://github.com/ollama/ollama/blob/2cd63f7a15039ccd790e97b31edc091fa1707c56/llm/server.go),
[runner wrapper](https://github.com/ollama/ollama/blob/2cd63f7a15039ccd790e97b31edc091fa1707c56/llm/llama_server.go),
[scheduler](https://github.com/ollama/ollama/blob/2cd63f7a15039ccd790e97b31edc091fa1707c56/server/sched.go)

The pinned llama.cpp server documents `-m`, `--host`, `--port`, and `--ctx-size`
for explicit local startup. Its default port changed to 9931 in v0.7.0, so the
adapter must provide a port. `/health` returns 503 during loading and 200 when
ready. `/completion` uses llama.cpp's own response shape;
`/v1/chat/completions` and `/v1/embeddings` offer OpenAI-shaped responses.
Embeddings require an appropriate pooling mode. Backend `prompt_ms` and
`predicted_ms` are milliseconds, and cached prompt tokens are counted separately
from newly evaluated prompt tokens. Those units and fields require explicit
translation. Backend syntax is a documented dependency rather than an assertion
that every historic llama-server build is supported.
[Pinned server documentation](https://github.com/ggml-org/llama.cpp/blob/620d2225cc436ab0b001e3200e590e3b286ef9ab/tools/server/README.md)

## Deliberate reconstruction boundaries

- The read-only registry accepts a bounded subset of existing legacy Ollama
  `manifests` and `blobs` stores, resolving one GGUF model layer with supported
  metadata. Newer `manifests-v2` layouts, split weights, adapters, projectors,
  stored message history, and the pull/push distribution workflow are not covered.
  This is limited legacy storage compatibility rather than a full registry clone.
- Backend startup uses an explicit executable and argument array. This example
  does not install an inference engine, download weights, or register a global
  service.
- Cloud routing, pull/push, model conversion, GPU scheduling parity, multimodal
  input, tool calls, and complete template/thinking behavior are outside the
  initial contract unless the implementation README explicitly supports them.
- The current registry declares completion-mode models only. Embedding response
  translation has synthetic coverage, but the shipped CLI cannot select and
  launch an embedding-mode model. `/api/embed` is therefore explicitly unavailable
  in this runtime. Unsupported stored model options fail visibly; GGUF-embedded
  generation defaults are not reconstructed, so default-setting parity is not
  claimed.
- Synthetic backend tests establish adapter behavior only. Real token-generation
  evidence requires an actual compatible llama-server executable and GGUF model.
  A source graph, a healthy mock endpoint, or passing schema validation cannot
  substitute for that inference run.
- The client-obligation ledger remains a historical source-analysis receipt.
  Later implementation tests do not silently convert it into an equivalence claim.

Ollama and its official JavaScript client use MIT licenses; the SDK notice is
retained with its downloaded evidence. llama.cpp is an external dependency with
its own license notice, and model weights retain their own license terms.
No third-party implementation source is copied into this example.
[Ollama license](https://github.com/ollama/ollama/blob/2cd63f7a15039ccd790e97b31edc091fa1707c56/LICENSE),
[SDK license](https://github.com/ollama/ollama-js/blob/c276d078eee37a0df003b9ee2120ec70327f32e9/LICENSE),
[llama.cpp license](https://github.com/ggml-org/llama.cpp/blob/620d2225cc436ab0b001e3200e590e3b286ef9ab/LICENSE)
