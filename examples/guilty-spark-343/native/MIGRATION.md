# Gabo net replacement acceptance — 0.12.0

The requested scope includes automatic imports, model-separated chats/personality/memory, phone/LAN access, editing/regeneration, organization and branching, attachments, character cards, lorebook retrieval, macros, extensions, scheduled prompts, search, and backups.

The Windows application hosts the advanced workspace itself. Its native backend and importer are newly authored C; the browser interface is a modified MIT-licensed GobboNet front-end. The original Gabo net server, launchers and installation are not needed for these 343 workflows and are preserved as a recovery option.

## Observed qualification

| Area | Evidence on this Windows machine |
| --- | --- |
| Native core | 688 C assertions, including model-specific notification counts/coalescing, Unicode banner limits, per-model import receipts, timestamp validation, no-change idempotence, forgotten-memory display, bounded history, backup roundtrip, startup shortcuts and workspace identity; warning-free MSVC build |
| Import notifications | 22 Windows checks: two-model delivery through the real Windows notification API, observed SHOW callbacks, hidden operation, click-callback handling, quiet polls/message-only updates, failed-save retry, memory-only alerts, persistent mute/re-enable, no replay, a labeled test banner and model separation |
| Import activity | 19 Windows checks: desktop/tray access, model filters, scheduled refresh while open, failed-save receipts, persistent check/import times, restart after source failure, deleted-memory privacy and draft preservation |
| Windows background mode | 20 checks against the production executable: hidden startup, tray availability, periodic imports and learning for two models, duplicate-launch suppression, reopen, saved drafts, close-to-tray, taskbar recovery, background toggle, restart idempotence, Exit and Windows session-end handling |
| Desktop regression | 16 personality, 31 history, 27 memory and 19 historical-learning checks through owned test windows |
| Automatic import/recovery | 25 checks: periodic acquisition, independent history views and learning in both directions, journal ownership, preserved drafts, failed-save retry, source/local branches, unresolved labels, restart idempotence, LAN authentication, cancellation and durable reply recovery |
| Browser workflows | 50 checks covering authentication, state revisions, model ownership, memories, real embeddings, samplers, reasoning, editing, reroll variants, user edits, branching, text attachments, folder/tag search, semantic lorebook injection, macros, JS/CSS, character hooks and archived notes, PNG V3/V2 JSON/CHARX cards, scheduling, backup, navigation recovery, responsive phone composition, Stop, character deletion and per-model drafts |
| Real inference | Gemma 3 tokenizer, runtime properties, rendered template and a completed reply through the C web host; native Gemma generation, Stop and close-during-generation all pass |
| Actual Gabo database format | The installed app's automatic worker imported provenance from the live configured origin after explicit activation. It recognized the earlier verified Gemma import without duplication. A repeated live read passed with no new chats or memories; the earlier isolated-copy test also passed |

Receipts and screenshots are private under ignored `output/playwright/` and `output/guilty-spark-343/`. Synthetic tests never open the operator's live workspace. Detailed paths are in `EVIDENCE.md` in the source checkout.

## External acceptance and operating limits

- Live polling was subsequently enabled and verified after the operator explicitly requested activation. The installed app checks the configured source every 60 seconds while open. An initial database-copy deferral retried successfully, and the repeat read added no duplicates. The live source contained one attributed Gemma conversation; two-model separation was verified independently with synthetic conversations and memories. The earlier qualification's automatic-approval refusal remains recorded in `EVIDENCE.md`.
- LAN authentication was tested through this machine's private IPv4 address and the browser at a 390-pixel phone viewport. Physical-phone reachability still needs checking on the operator's Wi-Fi/firewall. No firewall rules were changed. The listener uses HTTP; use private Wi-Fi or an encrypted tunnel.
- Web-search request validation and no-key refusal are tested. An authenticated external search requires the operator's own Ollama search key; no existing account secret was read or copied.
- Scheduled prompts run while the relevant model page is open, awake and connected. They are not unattended Windows services.
- Models remain constrained by their runtime, RAM and context. Direct GGUF mode supports the additional samplers/token tools; Ollama mode supports its common options. Images are display-only. The included runtime is CPU x64; GPU/other architectures have not all been qualified.
- The 8 MiB workspace, message/card bounds and recovery retention are documented in `README.md`. Unknown Gabo labels remain pending. Importing does not guess attribution, train model weights, uninstall the old application, or delete source chats.

This is a qualified replacement for the listed local workflows, with the external checks above explicit. It does not claim complete Ollama server compatibility or universal runtime/device compatibility.
