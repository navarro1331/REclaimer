# Guilty Spark 343: GobboNet reconstruction evidence

This platform combines the existing independently implemented
[Ollama-compatible inference server](../ollama-compatible/EVIDENCE.md) with a new
local chat interface and selected GobboNet data compatibility. The reference
application is [ElodineOfficial/GobboNet](https://github.com/ElodineOfficial/GobboNet),
interpreting the operator's spelling “gabonet” against the available project.
This document records selected source evidence, not a claim of complete GobboNet
behavioral parity.

## Reference boundary

The reference identity is commit
[`f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab`](https://github.com/ElodineOfficial/GobboNet/tree/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab),
inspected on 2026-10-22. The selected JavaScript files matched that checkout's
Git HEAD without local differences. Its public license is MIT, copyright
2026 Elodine. The reference application was not launched. Private configuration,
credentials, server state, model weights, job spools, and stored conversations
were not inputs to this investigation.

Only nine explicitly selected public JavaScript files were copied into an
ignored investigation directory for Reclaimer analysis. No reference JavaScript
is loaded by the delivered interface, and no original GobboNet server or
launcher is required by the new application. Local investigation artifacts are
excluded from public publication by the existing `output/` ignore rule.
Selected public lines in `08-rag.js` and `17-personas.js` were additionally
inspected to confirm the native prompt and persona mappings; their source
identities are recorded separately from the Reclaimer analysis inputs.

## Actual Reclaimer analysis

The `skills/real/SKILL.md` workflow was applied using the CLI discovery manifest
and `analyze-javascript-application`. Investigation output is retained locally
under `output/guilty-spark-343/gobbonet-static/`:

| Receipt | Observed result |
| --- | --- |
| `discovery-manifest.json` | Actual CLI operation discovery |
| `source-receipt.json` | Pinned reference URLs, SHA-256 values, file sizes, analysis identities and limitations |
| `analysis-01-config.json` | One JavaScript module, 10,619 bytes, 527 AST nodes, zero parse failures; 211 semantic nodes, 21 relations, five unresolved items |
| `analysis-15-cards.json` | One JavaScript module, 19,067 bytes, 2,590 AST nodes, zero parse failures; 1,761 semantic nodes, 1,328 relations, 442 unresolved items |
| `REFERENCE-LICENSE.txt` | Public reference license accompanying the retained investigation source |

The two evidence identifiers are
`ev_66d860d811363aef65a06e2b83a8dbf34008220fdacf0141be65bc422fc9f460`
and `ev_16c8801c4b5d524e27536533d95fef818dfbf4c77fe7d5dcff7720e75fcda71d`.
Both isolated inventories completed without truncation. Semantic coverage
remains partial: free application globals, DOM operations, and dynamic call
targets are not proven by these isolated source modules.

An initial combined analysis of all nine retained files remained CPU-active
without returning a result after about five minutes and was stopped. The two
isolated analyses then completed successfully. The retained receipt records
that cancelled attempt; this document claims two analyzed modules, not a
completed nine-module graph. Other contract findings below come from selected
public source inspection rather than an unobserved cross-module execution.

## Mechanically observed interfaces

All source links below refer to the pinned identity.

| Area | Observed source contract | Integration implication |
| --- | --- | --- |
| Inference transport | [`01-config.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/01-config.js) selects a same-origin `/llm` prefix when served; [`03-generation.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/03-generation.js) streams `/v1/chat/completions`. | The new interface can call the existing Ollama `/api/chat` route directly. Serving unchanged GobboNet files would additionally require its other backend contracts. |
| Stream records | [`02-model.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/02-model.js#L412) parses OpenAI `choices[0].delta.content`, reasoning fields, and Ollama `message.content` / `message.thinking`. | Both products carry user-visible chat text; wire format and request bodies remain distinct. |
| Detached generation | [`03-generation.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/03-generation.js#L332) first probes `/llm/jobs?thread=…`; 404/405 select direct streaming. | Persistent jobs, reattachment, and spool recovery are separate behaviors and cannot be inferred from successful text streaming. |
| Model selection | [`02-model.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/02-model.js) uses `/active-model.json`, `/models-list.json`, `/swap-model`, and `/swap-status`. | Guilty Spark can use existing Ollama `/api/tags` discovery and request-scoped model selection; these are not GobboNet's original administrative routes. |
| Saved conversations | [`05-persistence.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/05-persistence.js#L470) serializes `threads`, `characterCards`, active IDs, and other state. | JSON transfer can preserve the selected conversation and character data without reading the reference application's live state. |
| Thread relationships | [`09-threads.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/09-threads.js) defines `forkSource: {threadId, at}`; branches copy preceding history. | Any reconstructed branch operation must copy messages independently of its source conversation. |
| Character settings | [`15-cards.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/15-cards.js#L117) defines prompt, personality, greeting, lore, and sampler fields. | The selected character can supply a system instruction and supported generation options. |
| Prompt assembly | [`08-rag.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/08-rag.js#L903) inserts `writingStyle` as a system message; authored `startingLore` and running thread `lore` are strings. | Main instruction text must reach the system prompt; a summary string is not a list of retrieval entries. |
| User persona | [`17-personas.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/17-personas.js#L56) stores `id`, `name`, `description`, `avatar`, `injectionFrequency`, and display colors. | `activePersonaId` selects a persona; the reference's periodic injection and its zero-frequency disable behavior are separate from retaining identity text. |
| Character interchange | [`16-card-io.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/16-card-io.js) recognizes flat V1 and wrapped V2/V3 cards, plus image/archive containers. | Selected JSON compatibility does not establish PNG or CHARX container support. |
| Persistence and sync | [`05-persistence.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/05-persistence.js) uses IndexedDB/localStorage; [`06-state-sync.js`](https://github.com/ElodineOfficial/GobboNet/blob/f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab/js/06-state-sync.js) uses `/state` GET/PUT. | Browser persistence and explicit JSON export are separate from GobboNet's cross-device server synchronization. |

The native backup's full top-level shape is `threads`, `threadOrder`,
`activeThreadId`, `settings`, `characterCards`, `activeCardId`, `personaCards`,
`activePersonaId`, `schedules`, `sidebarOpen`, `searchEnabled`, `folders`,
`extensions`, `macros`, and `seededDefaultMacros`. A thread carries `id`, `name`,
`messages`, `lore`, `createdAt`, `pinned`, `folderId`, `tags`, and `forkSource`.
Messages carry a role and content, and can additionally contain reasoning,
variants, continuations, and temporary generation metadata. Recognizing this
shape does not imply that all its fields have executable behavior in Guilty Spark.

## Selected character field mapping

| Reference field | Meaning / mapping |
| --- | --- |
| Native `writingStyle` | Main character instruction text |
| Native `personality`, `startingLore`, `greeting` | Character traits, authored lore, and initial assistant message |
| V1/V2/V3 `description`, `system_prompt`, `personality`, `scenario`, `mes_example`, `post_history_instructions` | Textual character instruction material |
| V1/V2/V3 `first_mes` | Initial assistant greeting |
| V2/V3 `character_book.entries` | Selected enabled content can be flattened into instruction text; keyword retrieval and ordering semantics require separate implementation |
| `temperature`, `minP`, `topK`, `topP`, `repeatPenalty`, `repeatLastN` | Ollama options `temperature`, `min_p`, `top_k`, `top_p`, `repeat_penalty`, `repeat_last_n` |
| Native `customCode`, `customCodeEnabled`; `extensions.gobbonet.customCode` | Reference executable extension fields, not executable input to this interface |
| Native `settings.apiKey` | Credential configuration; not portable conversation data |

GobboNet's own V3 exporter folds several imported prompt fields into
`description`; its own import/export is not lossless. JSON interoperability should
therefore identify the fields retained or omitted instead of promising exact
round trips. Imported text is data; it must never become application JavaScript.

## Coverage and verification limits

This investigation did not execute the reference application, compare its live
model outputs, or inspect its private state. Static syntax and relationships
establish candidate contracts only. Functional tests of the new implementation
and actual inference checks establish the tested reconstruction's behavior;
they do not establish comprehensive equivalence with either GobboNet or Ollama.

Detached background jobs, multimodal inference, web search, RAG/storybook
retrieval, extensions/custom JavaScript, schedules, macros, cross-device sync,
PNG/CHARX containers, and the reference application's model administration
routes are independent obligations. Consult [README.md](README.md) for the
delivered subset and the exact startup instructions.

## Implementation verification on 2026-10-22

The combined automated suite passed **68 tests, with zero failures or skips**:

```text
node --test --test-timeout=15000 examples/ollama-compatible/*.test.mjs examples/guilty-spark-343/*.test.mjs
```

This includes the original 44 inference/registry/lifecycle tests and 24 platform,
CLI, import, state, persona, and conversation tests. `node scripts/check.mjs`
passed with 240 syntax files, 139 MCP operations, six prompts, and no missing
handlers. Reclaimer's existing public CLI/MCP identities remain intact.

A separate Guilty Spark 343 listener ran on port 11436 while the existing service
on 11435 remained untouched. Actual CPU inference used the previously qualified
`deepseek-coder:latest` weights and the existing isolated llama.cpp executable.
The native API returned a completed real reply; the receipt is
`output/guilty-spark-343/live-api-response.json`.

Playwright CLI with Microsoft Edge verified the following browser behavior:

- Model discovery and a streamed Python addition function from the real runtime.
- Conversation persistence across page reload.
- Import of a synthetic GobboNet-shaped backup with numeric timestamps, history,
  character instructions, authored/thread lore, and the selected persona.
- A new real reply continuing the imported conversation.
- Portable JSON export retaining supported data and excluding synthetic API-key
  and executable-code marker fields.
- Editing generation settings, stopping an active stream, retaining partial
  output, and successful generation after cancellation.
- Character creation/selection and its greeting in a new conversation.
- Desktop and 390-pixel mobile layouts, including the mobile drawer and character
  editor. The final inspected browser console had zero errors and warnings.

Browser screenshots, snapshots, synthetic import/export files and a concise
verification receipt are retained under ignored `output/playwright/`. Source
investigation artifacts remain in their separate ignored evidence directory.
The first attempt to click Stop occurred after generation had already completed;
a second, longer stream was successfully cancelled. Early browser loading while
the module was still being authored was not counted as a passing result.

These checks establish working transport, persistence, migration and UI behavior
on one model. They do not qualify every discovered model or establish response
accuracy: an imported-character arithmetic continuation produced `9` for `3 + 3`.
That incorrect generated output is retained in the browser evidence. The tested
small model's output should not be treated as an accuracy benchmark or a claim
of generation parity with the reference applications.
