# Guilty Spark 343 — native Windows application

Version 0.12.0. A Windows x64 application with a C11/Win32 desktop interface and native HTTP host. The model library, persistence, automatic Gabo net importer, import activity view and notifications, Windows startup/tray integration, inference client and process management are C. **Advanced workspace** opens the included GobboNet-derived browser UI, adapted to 343's model-specific state. Model inference uses a separate llama.cpp C/C++ runtime or a loopback Ollama-compatible server. The MIT browser sources and required notices are included; they are not newly authored C code.

## Run the Windows package

1. Extract the entire `Guilty-Spark-343-Windows-x64.zip` archive to a writable folder. Keep the `runtime`, `web` and `licenses` folders with the executable.
2. Open `GuiltySpark343.exe`.
3. Select a discovered model in the dropdown, use **Add GGUF** for an individual file, or use **Add folder** for a model library. Both ordinary GGUF folders and Ollama folders containing `manifests` and `blobs` are supported. Model files are read in place. No weights are downloaded or bundled.
4. Type a message and click **Send**, or press **Ctrl+Enter**. **Enter** inserts a newline. **Stop** cancels generation and preserves partial output.

The desktop application needs no Node.js, Python, browser, installer or administrator privileges. The included CPU llama.cpp build requires the Microsoft Visual C++ v14 x64 runtime. If it is missing on a new machine, obtain it from [Microsoft's supported redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). No global runtime installation is performed by this application. The C desktop executable itself uses the static MSVC runtime (`/MT`).

Target: Windows 10/11 x64 with enough RAM for the selected GGUF and context. The delivered CPU runtime has been exercised on the development Windows machine; other Windows editions, GPU runtimes and model architectures have not all been qualified. This is an unsigned local build.

For an existing service, open **Settings**, select **Ollama-compatible server**, and enter its loopback HTTP URL, such as `http://127.0.0.1:11434`. **Refresh** lists its models. This desktop client uses `/api/tags` and streamed `/api/chat`. It also works with the earlier Reclaimer implementation of those endpoints.

## Windows startup and background importing

Right-click the **Guilty Spark 343** icon in the Windows notification area (it may be under the **^** arrow). **Start with Windows** adds a shortcut for the current Windows account that starts this executable with `--background` and the same data directory when you sign in. Uncheck it to remove that shortcut. Keep the extracted package in its installed location; after moving/upgrading it, enable startup from the new copy to update the shortcut. Other accounts and system-wide startup entries are not changed.

**Keep running when the window closes** saves your draft and hides the chat window while imports, model-specific memory learning and the web host continue. **Open Guilty Spark 343** or launching the executable normally restores the existing process. Repeated background starts do not launch another importer for the same workspace. **Exit 343** stops it completely until you open it again or next sign in. Windows sign-out/shutdown also stops the process normally. This is an application in your login session, not a system service; a sleeping or powered-off PC does not import, and checks resume after it wakes.

Background mode uses the existing import source, interval and per-model memory preferences. It does not load a conversational model just to extract memories. Closing a browser workspace still stops that page's scheduled prompts. The tray includes **Import Gabo net now** and **Gabo net settings**. Importing automatically requires the existing automatic-import option to be enabled.

For setup or diagnostics, `GuiltySpark343.exe --startup-on` registers the current executable for this account and `--startup-off` removes its owned entry. Both accept `--data-dir` and an optional `--receipt` JSON path. `--background` starts quietly and keeps running when the window closes. The `background` setting controls close-to-tray on ordinary launches. `background-status.json` in the data directory records process ID, window/tray state, completed-check count, last-check time and aggregate result, without conversation text or credentials. The startup shortcut is written atomically and refuses to replace an unrelated same-name shortcut.

## Automatic Gabo net imports

Click **Import activity** in the desktop sidebar or the Windows tray menu to see the last completed check, last successful check, and last import that added content. Times use the computer's local time zone. Choose a model to see its latest recorded import, chat/message counts and the actual new memories it learned, or choose **All models** for grouped results. **Check now** runs a manual check. This window stays open and updates while background importing continues.

**Notify about new Gabo net chats and memories** enables Windows notifications (on by default). Each notification names one model and reports its new chat and/or newly learned memory counts. New facts learned from updates to an already imported chat also notify. Alert text contains no conversation text or memory wording. Click an alert to open the grouped **Import activity** view, then choose a model to review its details. The same preference is available in the tray menu; it is saved as `gaboSync.notifications`. **Test notification** sends a clearly labeled test without creating chats or memories.

Notifications are queued only after a successful workspace save. Empty checks, unchanged polls, message-only updates without new memories, unresolved attribution and failed saves stay quiet. Pending alerts coalesce by model, and Windows completes one banner before 343 submits the next. Muting cancels the current banner and clears pending alerts while importing and learning continue. Re-enabling or restarting does not replay previous imports; receipts remain in Import activity. Pending banners are session-local and are not replayed after Exit. Windows controls display and sound, and its notification/quiet-time settings are respected. Diagnostic status distinguishes submitted notifications from actual Windows SHOW callbacks; submission alone is not proof that a banner appeared.

The workspace retains up to 50 import batches, bounded to 256 KiB. Receipts commit atomically with the chats and memories; failed saves cannot publish an import or a learned fact. Unchanged checks do not create duplicate history. Check status persists separately in `gabo-check.json`, so it survives a restart even if the next source read fails. Receipts contain memory fingerprints, with displayed text resolved only from that model's current saved notes: a deleted/edited memory's old wording is not kept as another readable copy in the log. Older imports have no recorded receipt or trustworthy import time; the view states that explicitly. The complete desktop workspace backup includes the activity history.

Open **Gabo net sync** on the desktop. Configure the Gabo net origin, the Chrome/Edge profile directory (the directory containing `IndexedDB`), and its browser executable. Alternatively specify an exported JSON file containing `threads` and optional model-attribution `telemetry`. Enable automatic checking and choose a 15–3600 second interval (default 60). **Save and sync now** also performs an immediate check. The source remains read-only.

The native reader copies only that origin's IndexedDB files to a new temporary profile. An owned headless browser reads that copy with network requests intercepted and blocked. It never copies cookies, login data, browser extensions or unrelated origins. A changing/locked snapshot fails and retries later. This requires an installed compatible Chrome/Edge browser; desktop chat does not.

Each assistant reply must have an explicit model label or a matching generation record. Names map only to catalog entries or explicit `gaboSync.modelAliases`. There is no fallback to the currently selected model. A chat with any unresolved turn remains pending; the sync status reports it. Mixed-model conversations split into separate model-owned turn sequences. Expired attribution records may require a model-labeled export. The importer does not guess.

Repeated polls and restarts do not duplicate imported turns. A source append extends an unchanged imported chat. If either copy was edited, a new source branch preserves the local continuation. Source deletion never deletes local history; a local deletion suppresses reimport. Import content, provenance and memory changes commit in one atomic workspace save. A failed save leaves the prior state and journal intact for the next poll.

Only newly encountered user text is considered for automatic memory learning, under that model's independent **Use memories** and **Learn automatically** settings. Assistant claims never become user memories. Previously consumed source text is tracked so polling does not restore a deleted memory. New facts do not displace existing saved notes during historical learning. This is saved context, not model-weight training.

## Advanced workspace and phone access

Click **Advanced workspace** to open the browser, authenticated with an ephemeral access code. Select a model there to open its own chats, active character, user personas, memories, schedules, macros, extensions and browser cache. Character definitions remain a shared library; active personality assignments and advanced card settings are per model. The native desktop and browser use the same atomic workspace. An idle browser refreshes native/imported changes; a concurrent edit produces an explicit conflict instead of silently overwriting either copy.

| Workflow | 343 implementation |
| --- | --- |
| Conversation organization | Search, tags, folders, pinning, branches and model-specific histories |
| Editing and regeneration | Editable messages, saved wording/reply variants and forked continuations |
| Formatting | Markdown, code/file download blocks, reasoning displays and generation timing |
| Attachments | Text files in prompt context; image attachments displayed as references |
| Characters | JSON, PNG V2/V3 and CHARX card handling, portraits, persona cards and per-card samplers |
| Retrieval | Weighted lorebook matching and local semantic embeddings; conversation summaries/context budgeting |
| Customization | Custom macros, CSS/JavaScript extensions, character hook code and archived card notes |
| Scheduled prompts | One-time/daily prompts while the model's browser workspace remains open, awake and connected |
| Background replies | Native detached jobs survive browser navigation; durable stream checkpoints recover completed/partial replies after an app restart |
| Backups | Per-model advanced exports plus the desktop's complete workspace export |

For semantic retrieval, set `embeddingModel` to a compatible local embedding GGUF in your settings JSON. The qualified model is [Nomic Embed Text v1.5 GGUF](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF), revision `0188c9bf409793f810680a5a431e7b899c46104c`, file `nomic-embed-text-v1.5.Q4_K_M.gguf`, 84,106,624 bytes, SHA-256 `d4e388894e09cf3816e8b0896d81d265b55e7a9fff9ab03fe8bf4ef5e11295ac`, Apache 2.0. Weights are separate from the app package. Without configured embeddings, tag retrieval remains available and the semantic stage reports unavailable.

Optional web search accepts your own Ollama search key in browser settings and forwards only the requested query over HTTPS to the [documented Ollama search endpoint](https://docs.ollama.com/capabilities/web-search). The key is excluded from native workspace saves. No account credential is discovered automatically. User-installed extensions run as trusted page code and can access the workspace/network; enable only extensions you trust.

For a phone, open **Phone connection**, enable private-network access and choose a fixed port if desired. Save, then use the displayed address/access code on the same private Wi-Fi or encrypted mesh. Windows may require allowing the app on the **Private** network. 343 does not modify firewall rules or install a service. The listener is HTTP, not HTTPS: use a trusted private network or encrypted tunnel, never public port forwarding. Access codes change on restart or connection-setting changes. Actual reachability from another device depends on the host firewall and network.

The browser workspace requires the 343 process to keep running; its desktop window may be hidden in the tray. Schedules are not Windows background tasks and do not run while their page is closed/asleep. One conversational model generates at a time; a separate local embedding process can serve retrieval. XTC, DRY, token bias and model template/tokenization tools require direct GGUF mode; an Ollama connection supports its common sampling options. PNG/CHARX character support does not imply vision inference: image attachments are display-only. Local file downloads, model conversion, GPU driver installation, arbitrary OS tools and the original Gabo net service/mesh installers are not automatic actions of this app.

The host retains the newest 128 recovery spools plus a current reply and keeps up to 64 jobs in memory. Interrupted generation is recovered as partial output, not silently restarted. Attachments are capped at 3 MiB each, with at most 40,000 characters of each text attachment inserted into a prompt; total message/workspace limits still apply. Portrait/image data has a separate 4 MiB storage allowance. Export larger archives before approaching the 8 MiB workspace limit.

## Local model discovery

The app checks the adjacent `models` folder, the Windows user's `.ollama/models` folder, and `OLLAMA_MODELS` when set. It also recognizes an existing explicitly registered Ollama blob and scans that blob's parent model library. **Add folder** remembers additional locations, such as a folder of downloaded GGUF files. **Refresh** rescans without loading model weights into an inference process.

Readable candidates appear first in the dropdown. **Model info** shows the source, file, architecture, prompt choice and availability reason. A usable copy takes precedence over a stale same-name entry with missing weights. Entries marked **unavailable** cannot be sent to the local runner.

The scanner inspects GGUF v2/v3 metadata, preserves model aliases and Ollama system/sampler metadata, verifies hashes for referenced template/parameter metadata, and checks referenced weight sizes. It does not hash entire multi-gigabyte weight files. Standard split GGUF files are supported by selecting the `00001` shard with all parts present in the same folder. Other shards are marked unavailable as independent choices. Directory scans skip reparse points; Ollama blobs must resolve inside the chosen library. No source model file is modified.

"Ready" means that the file passed inspection and can be submitted to the selected runtime. It does not certify a successful load. Actual architecture support, available RAM, model quality and speed still depend on the model and runtime. Audio-only files, vision projectors, adapter/tensor stores and raw Safetensors/PyTorch files are not directly runnable as text chat. Models needing another runtime can be used through a compatible local Ollama server when that server supports them.

Some Ollama Gemma 3 bundles combine text/vision tensors in a layout the bundled llama.cpp runtime cannot load and omit its required RMS normalization metadata. Version 0.4.1 detects the missing or invalid setting before starting inference. Use a llama.cpp-compatible Gemma 3 GGUF for direct text chat; supplying a guessed normalization value alone does not convert the tensor layout. A compatible file registered under the same model name takes precedence over the incompatible library entry. The original source file is preserved.

The verified Gemma 3 text export is [`gemma-3-4b-it-Q4_K_M.gguf` from ggml-org](https://huggingface.co/ggml-org/gemma-3-4b-it-GGUF/blob/d0976223747697cb51e056d85c532013931fe52e/gemma-3-4b-it-Q4_K_M.gguf), revision `d0976223747697cb51e056d85c532013931fe52e`, 2,489,757,856 bytes, SHA-256 `882e8d2db44dc554fb0ea5077cb7e4bc49e7342a1f0da57901c0802ea21a0863`. It is licensed under Gemma's terms and is a separate model download, excluded from the application ZIP. This qualification covers text chat, not image input.

Scanning is bounded to 512 model entries, 32 folders, 20,000 directory entries and 12 directory levels. Each GGUF inspection reads at most the first 32 MiB; combined retained model metadata is capped at 8 MiB. Missing default folders are ignored. Model parsing and discovery support cancellation.

## Conversations and characters

- Create, rename, pin and delete conversations. Select a character for its instructions, description, scenario and greeting. Settings includes a user persona, response token limit and temperature.
- **Each model remembers its own character.** Select a model, then choose a character from the adjacent dropdown or save a new one under **Characters**. That choice is saved immediately and restored when you switch back, refresh the library or reopen 343. Unassigned models use **Default assistant**. Choosing Default assistant clears only the selected model's assignment.
- **Each model also remembers its own chats.** Selecting a model shows only its conversations and restores the last one you selected, including its transcript and unsent draft. A model with no history starts with a fresh conversation. **New chat** starts another chat for that model; older chats stay in its list. Drafts are saved when you leave the chat or close the app. Normal restart, workspace export/import and `--model` preserve these choices. Continuing a chat sends that model's previous conversation turns; messages from other models are excluded.
- **Each model remembers useful details across new chats.** Automatic memory starts enabled. Clear first-person facts in newly sent messages are saved only for the selected model and included in that model's subsequent requests. Open **Memories** to review, add, edit or remove notes. Its count shows how many notes that model has. **Clear notes**, then **Save**, clears that model's memory; Cancel discards editor changes. Each model independently controls whether memories are used and whether clear facts are saved automatically.
- **Learn from existing chats.** Select a model, open **Memories**, and choose **Learn from saved chats**. It scans your messages in all saved conversations assigned to that model and adds eligible facts to the editable preview. Review the notes and choose **Save**; **Cancel** discards the preview. Repeat with another model to learn from its separate conversations. Scanning happens locally and needs no running model.
- **Import** accepts supported GobboNet JSON backups and flat/V2/V3 character JSON, or a Guilty Spark 343 workspace export. Whole-workspace imports ask before replacing the current workspace; single character imports append a card.
- **Export** writes a portable Guilty Spark JSON workspace. It contains supported conversation, character and persona text, drafts, conversation ownership, saved memories and their per-model preferences, and the saved model-to-character and last-conversation choices, not engine configuration or credentials.
- History defaults to `%LOCALAPPDATA%\GuiltySpark343\workspace.json`; engine settings live alongside it as `settings.json`. Saves use a flushed temporary file and atomic replacement, with the previous file at `.bak`. Only one application instance may use a given data directory.
- A malformed saved workspace is preserved and refused. Restore its `.bak` file or choose a new data directory. Export important history separately: `.bak` contains only the immediately preceding save, not a permanent archive.

GobboNet `writingStyle`, `startingLore`, thread lore, greeting, conversation order and the selected enabled persona become text instructions. Message variants, sampler presets, reasoning metadata, scripts, extensions, automation, credentials and remote integrations are omitted. Imports never execute code. Opening greetings become system context when needed by alternating-role model templates; displayed history remains unchanged. Character description and personality are shown together in the character editor.

Personality and history choices use the complete model name, including its tag. The same model name uses the same assignments in local and server modes; a renamed alias is treated as a different model. Importing a single character assigns it to the selected model without changing chats. Deleting a character clears every personality assignment to it while keeping the chats. Editing a shared character updates all models using that character; create separate characters for independent personalities. Switching characters within a model changes instructions for subsequent replies and keeps that chat's history. Start **New chat** for a fresh conversation and its selected character's greeting. Deleting a conversation selects another chat for the same model, or creates an empty one if it was the last. Chats for models temporarily missing from the catalog stay saved and return when the same model name is available again.

Workspace format 4 retains each conversation's model name and draft and the `modelThreads` map, and adds `modelMemories`: each complete model name maps to an `enabled` flag, an `automatic` flag and a `notes` array of strings. Versions 1–3 and supported GobboNet backups are upgraded on load. Version 3 preserves its existing per-model history. Versions 1/2 and GobboNet histories have no reliable model attribution, so their conversations stay with the model selected in the old workspace. Text, ordering, pinned state, context, characters and settings are retained. If no model was selected, those chats and the pending character are assigned to the first selected model. Version 1 character choices migrate as in 0.5. Versions before 0.7 cannot read format 4; keep a separate older workspace backup to return to an older build. A saved mapping to another model's conversation is rejected; missing conversation references fall back to an existing chat for the matching model, never another model's chat.

## Memories across chats

Automatic capture recognizes short statements beginning with **My name is**, **Call me**, **My pronouns are**, **I live in**, **I prefer**, **I like**, **I dislike**, **I work as/in/at**, **I am learning / I'm learning**, **My goal is**, or **I use**. It saves the first declarative sentence of each matching line. Put separate facts on separate lines. A new name, pronoun or home-location statement replaces that category's older note. Other facts append, with identical notes deduplicated; edit obsolete or conflicting preferences in Memories.

For another detail, say **Remember that ...**, **Remember: ...**, or **/remember ...**. These explicit requests also work when automatic capture is off. **/forget followed by the exact note text** removes a matching note; **/forget all** clears notes for the selected model. Turning **Use memories** off omits notes from requests and stops capture for that model, while retaining its notes. Forget commands still work while memory is off.

Capture uses bounded local rules, not a second model or a claim to understand every important fact. It does not infer facts from assistant replies. Quoted lines, fenced code, questions, and messages containing phrases such as “do not save,” “this chat only,” or “pretend” are skipped. Clear English phrasing is recognized; other phrasing or languages can be saved explicitly or entered in the editor. A submitted fact can be saved even if inference subsequently fails or is stopped. Storage/capacity failures are reported; failed editor saves keep the old memory state.

**Learn from saved chats** applies the same rules to user messages already stored in the workspace, including supported imported chats. It never reads assistant replies, unsent drafts, character cards or the global persona as facts about you. The preview reports conversations and user messages scanned, additions, conflicts and skipped forgotten/over-limit facts. Existing notes and unsaved editor changes take priority. Repeated scans deduplicate identical notes; nothing is saved until **Save**. This explicit scan works even with automatic capture or memory use off, and retains both preferences. Loading or importing a workspace alone does not trigger a scan.

Within one conversation, later name/pronoun/location corrections and `/forget` commands take effect in message order. Across different conversations, messages have no reliable shared timestamps: conflicting names, pronouns or locations are skipped rather than choosing by sidebar or creation order. A forget command in another chat suppresses matching candidates (or that fact category); `/forget all` suppresses candidates from other chats, while subsequent facts in its own chat can still be proposed. Existing saved notes are never removed by a scan. Other preferences may need manual reconciliation. Facts removed manually from Memories can be proposed again if you explicitly scan an old chat containing them; review the preview before saving.

Each scan uses the conversation's stored full model name and tag. Old backups without model attribution follow the migration assignment described above; the scanner cannot reconstruct which model originally wrote those unattributed chats. Over-limit notes are skipped without evicting existing ones. A scan with more than 8,192 candidate facts/forget commands fails without changing notes to bound processing work; use manual memory editing for that case.

Notes are stored locally in the workspace and supplied only to the selected model, including when it uses the configured loopback server. They use the complete model name and tag; changing character or creating/deleting a chat does not change memory ownership. The existing **Your persona** setting is still a separately configured global prompt, and is not copied into model memories. Deleting a saved note removes it from future memory context, but does not erase that text from existing chat transcripts, `.bak` recovery files or exported backups. Start a fresh chat after forgetting a fact if an old transcript still contains it. Models may vary in how well they use the supplied context.

Memory limits: 32 notes per model, 512 UTF-8 bytes per note, 4,096 bytes of notes per model, and memory settings for up to 512 model names, subject to the workspace's total size/node limits. Older notes are never silently evicted to make room. Memory uses part of the model's existing context window.

Limits: 8 MiB per JSON file, 100 conversations across all models, 200 characters, 512 model personality assignments, 1,000 messages per conversation, 5,000 messages in a workspace, 50,000 UTF-8 bytes per text field or draft, and 200 bytes per conversation/character name. At the conversation limit, starting a chat for another model requires exporting/deleting an older chat; switching among existing model histories still works. Imports reject duplicate keys, invalid UTF-8, embedded NULs, deep JSON, tool calls and unsupported message roles. Missing character references fall back to Default assistant. Persistent history does not enlarge a model's configured inference context window.

## Configuration

Choose model files in the interface, or copy `guilty-spark.example.json` to `guilty-spark.json` beside the executable. A model entry has this form:

```json
{
  "name": "my-local-model",
  "path": "models/my-model.gguf"
}
```

Put entries in the `models` array. Optional `modelFolders` is an array of folder paths; when supplied it replaces the default scan folders. A registered Ollama blob still contributes its parent model library. Paths are relative to the configuration file. Configuration precedence is `--config`, saved account settings, adjacent `guilty-spark.json`, then defaults. The default runtime is `runtime/llama-server.exe` beside the executable. Settings saves create the account's settings file; they do not rewrite an explicitly supplied configuration file.

```powershell
.\GuiltySpark343.exe --config .\guilty-spark.json --data-dir .\my-workspace
```

The app prefers an embedded GGUF chat template. Otherwise it renders supported legacy Ollama text templates, including System/Prompt/Response placeholders and simple `if`/`else`/`end` conditions. Recognizable Llama 3, Gemma and DeepSeek 2 message templates use the corresponding llama.cpp formatter. Unrecognized complex templates are refused with a suggestion to use the original local Ollama runtime. A model with no chat template uses plain User/Assistant completion, which may need a more appropriate template for good answers. Local model entries may include `template`, `system`, `parameters` and a llama.cpp `chatTemplate` name.

Desktop model metadata parameters are `stop`, `top_k`, `top_p`, `min_p`, `repeat_penalty` and `repeat_last_n`. The UI supplies temperature and the response token limit. Other model parameters are not mapped. The CPU package defaults to 2,048 context tokens and four inference threads; adjust these in Settings for your model and machine. The desktop retains plain-text history. Advanced workspace provides context budgeting, summaries and reasoning displays.

Local mode starts one owned inference process on an ephemeral loopback port with a fresh authentication key. Process creation serializes an argv list for the Windows API; it never invokes a shell. A kill-on-close Windows Job Object owns the runtime. Stop terminates the local runner, and the next request reloads the model. Closing the app also cleans up its owned runner. In server mode the app cancels only its HTTP request, leaving the separately managed server running.

## Build from a Reclaimer source checkout

Install Visual Studio Build Tools with the C++ x64 toolset and a Windows 10/11 SDK, then run this from the repository root:

```powershell
.\examples\guilty-spark-343\native\build.ps1
```

The script discovers MSVC and the SDK, compiles the C11 sources with `/W4`, runs the native tests, and creates `output/guilty-spark-343/windows-x64/GuiltySpark343.exe`. It changes toolchain environment variables only in the build process and restores them afterward. No global installation or service registration is performed.

Source responsibilities:

| File | Responsibility |
| --- | --- |
| `main.c` | Win32 desktop controls, dialogs, worker coordination and smoke mode |
| `state.c` | Workspace schema, GobboNet import, prompt assembly and atomic storage |
| `memory.c` | Per-model notes, bounded live capture and historical learning, editing, import validation and memory preferences |
| `runtime.c` | Asynchronous WinHTTP, NDJSON/SSE parsing and owned llama.cpp process |
| `catalog.c` | Bounded GGUF inspection, model folders and Ollama manifests/blobs |
| `templates.c` | Legacy text prompt rendering and template validation |
| `tests.c` | Native assertions and actual loopback HTTP fixtures |
| `catalog_tests.c` | Synthetic model files, libraries, incomplete shards and template cases |
| `personality_tests.c` | Per-model character selection, migration, prompts, deletion and persistence |
| `history_tests.c` | Conversation ownership, last-chat selection, drafts, migration, deletion, prompt isolation and storage bounds |
| `memory_tests.c` | Memory capture/correction, per-model isolation, new-chat recall, disabling/deletion, migration, persistence and limits |
| `backfill_tests.c` | Historical fact learning, model/tag isolation, existing-note priority, ambiguous corrections, forget commands, repeat scans and capacity limits |
| `vendor/cJSON.*` | Pinned third-party JSON parser, v1.7.19 |

To create a portable package using an existing Windows x64 CPU llama.cpp distribution:

```powershell
.\examples\guilty-spark-343\native\package.ps1 `
  -RuntimeDirectory .\path\to\runtime `
  -RuntimeLicenseFile .\path\to\LICENSE.llama.cpp
```

The packager copies an explicit set of runtime files, documentation and licenses, writes SHA-256 hashes, and creates a ZIP. It refuses to overwrite an existing package. Use `-Destination` for a fresh output folder. Personal configuration, model weights, histories, test receipts, compiler files and unrelated runtime tools are excluded.

The native HTTP host serves the advanced workspace and its scoped APIs. It is not a complete Ollama `/api/*` server. The earlier Reclaimer API server remains separately available for external Ollama clients. Normal 343 use needs neither that JavaScript server nor the original Gabo net server.

## Verification

The build runs deterministic C tests for state round trips, GobboNet mapping, date conversion, sanitized imports, atomic saves, argv quoting, UTF-8 handling, fragmented HTTP streams, missing completion records, HTTP failures, redirects, cancellation and deadlines. Model tests cover GGUF inspection, Unicode paths, missing shards, Ollama metadata hashes, invalid digest paths, missing architectures, unsupported files and legacy prompt turns. It does not download a model.

With a configuration containing a real, compatible model, run the desktop lifecycle tests using PowerShell 7:

```powershell
.\examples\guilty-spark-343\native\smoke.ps1 -Config .\config\local-native.json
```

This launches the actual desktop executable with separate test data directories for generation, Stop and close-during-generation. It writes JSON receipts and screenshots of only the app's window, then exits. Direct switches are `--smoke`, `--smoke-stop` and `--smoke-close`, with `--receipt` required. Test evidence belongs under ignored `output/`. Real model output verifies integration and lifecycle, not correctness of the model's answers.

Use `--model "model-name:tag"` to select a catalog entry on launch. For inspection without inference, use `--list-models --receipt catalog.json` with a separate `--data-dir`. Catalog entries include readiness reasons and machine-specific file paths; keep those receipts private.

Run `./examples/guilty-spark-343/native/personality-smoke.ps1` from the repository root to exercise the actual Windows model/character dropdowns, character editor, saving and restarting. It uses a fresh isolated workspace and synthetic inspection-only model fixtures. It performs no inference and does not touch the user's open app or account workspace. The script uses control messages only on its own test process.

Run `./examples/guilty-spark-343/native/history-smoke.ps1` with PowerShell 7 to exercise model switching, per-model chat lists, multiple conversations, drafts, rename/pin/delete, restart, catalog refresh/failure/empty responses and `--model` through real Windows controls. A deterministic loopback server captures the app's actual chat requests and verifies that continued conversations include their own previous turns and exclude the other model's history and personality. It uses only a fresh test workspace and synthetic text; no model weights or user workspace are accessed. Both UI scripts accept `-Executable` and a fresh `-OutputDirectory`.

`memory-smoke.ps1` exercises automatic and explicit saving, fresh-chat recall, model isolation, memory editing, clearing, Cancel, independent toggles, forget commands, character changes, restart and `--model` through the actual Windows UI. Its loopback fixture inspects the generated requests, including checks that disabled or deleted notes are absent from a new chat. Like the other UI scripts, it accepts `-Executable` and a fresh `-OutputDirectory`, and only accesses its own test process and isolated workspace.

`backfill-smoke.ps1` exercises **Learn from saved chats** through the Windows editor. It checks preview/Cancel/Save, existing and unsaved note preservation, scan counts, conflicts, exclusion of assistant/other-model/opt-out text, independent toggles, repeat scans, restart and new-chat requests for both models. Original transcripts are compared after learning to ensure they remain intact. It uses synthetic chats and a loopback fixture, with the same executable and fresh-directory arguments as the other UI scripts.

`web-smoke.cjs` drives the actual native test host and browser with isolated synthetic data and a local embedding model. `sync-smoke.cjs` verifies timed source acquisition, failed-save retry, branching, memory isolation, LAN authentication, cancellation and job recovery after restart. Their receipts and screenshots go under ignored `output/playwright/`. `GuiltySpark343Test.exe` exposes its ephemeral test connection only through an owned pipe and requires a separate data directory; it is excluded from distribution. See `MIGRATION.md` for qualified workflows and remaining external acceptance checks.

## Dependencies and notices

- cJSON v1.7.19 is MIT-licensed. Its license is in `vendor/LICENSE.cJSON.txt` in the source and `licenses/LICENSE.cJSON.txt` in a package. Upstream: [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON/tree/v1.7.19). Vendored SHA-256: `cJSON.c` = `298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d`; `cJSON.h` = `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee`.
- The tested inference runtime is [llama.cpp release b11669](https://github.com/ggml-org/llama.cpp/releases/tag/b11669), Windows CPU x64. Archive SHA-256: `37b3efcf962847ca869745fd4ccf7f5e7e8dacfeff9fa3523b4ca8cdc66d632b`. Retain its MIT license and LLVM OpenMP notice. The packager copies both.
- Model weights and their licenses are supplied separately. The GobboNet-derived browser UI is bundled under MIT; the original GobboNet server/launchers and Ollama runtime are not bundled. Full browser attribution is in `web/LICENSE` and `web/UPSTREAM.json`, with platform notices in `licenses/GobboNet-and-platform-notices.md`.
- Windows networking follows the documented [WinHTTP API](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp). The client uses asynchronous requests and waits for handle closure before releasing callback storage during cancellation.
