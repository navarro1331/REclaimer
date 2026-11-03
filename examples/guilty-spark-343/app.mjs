import { createState, makeThread, makeCharacter, importData, exportData, buildMessages } from './state.mjs';

const STORAGE = 'guilty-spark-343.v1';
const $ = id => document.getElementById(id);
const element = (tag, className, text) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};
let state = createState(), models = [], activeRequest = null, persistTimer, editingCharacter = null;
const mobile = matchMedia('(max-width:700px)');
let restoredError = '';
try {
  const saved = localStorage.getItem(STORAGE);
  if (saved) state = importData(saved).state;
} catch { restoredError = 'Saved history could not be opened. The stored copy has not been changed; export or repair it before saving new work.'; }
const currentThread = () => state.threads.find(item => item.id === state.activeThreadId) ?? state.threads[0];
const currentCharacter = () => state.characters.find(item => item.id === state.activeCharacterId) ?? null;

function notice(text = '', error = false) {
  $('notice').textContent = text;
  $('notice').hidden = !text;
  $('notice').classList.toggle('error', error);
  $('live-status').textContent = text;
}

function persist() {
  clearTimeout(persistTimer);
  try {
    const data = JSON.stringify(exportData(state));
    if (new TextEncoder().encode(data).length > 2 * 1024 * 1024) throw new Error('Workspace exceeds the 2 MiB backup limit. Export and remove older conversations.');
    localStorage.setItem(STORAGE, data);
  } catch (error) { notice(`History could not be saved: ${error.message}. Export a backup before closing this page.`, true); }
}

function sidebar(open) {
  $('sidebar').classList.toggle('open', open);
  $('sidebar').inert = mobile.matches && !open;
  $('sidebar-scrim').hidden = !open;
  $('menu-button').setAttribute('aria-expanded', String(open));
}

function button(label, text, action) {
  const node = element('button', '', text);
  node.type = 'button'; node.title = label; node.setAttribute('aria-label', label);
  node.addEventListener('click', action);
  return node;
}

function renderThreads() {
  const query = $('thread-search').value.trim().toLowerCase();
  const list = $('thread-list'); list.replaceChildren();
  $('thread-count').textContent = String(state.threads.length);
  for (const thread of [...state.threads].sort((a, b) => Number(b.pinned) - Number(a.pinned))) {
    if (query && !thread.name.toLowerCase().includes(query)) continue;
    const row = element('div', `thread-row${thread.id === state.activeThreadId ? ' active' : ''}`);
    const select = button(thread.name, '', () => {
      state.activeThreadId = thread.id; $('prompt').value = ''; render(); persist(); sidebar(false);
    });
    select.className = 'thread-select';
    if (thread.id === state.activeThreadId) select.setAttribute('aria-current', 'page');
    select.append(element('span', 'thread-symbol', thread.pinned ? '◆' : '◌'), element('span', 'thread-name', thread.name));
    const actions = element('div', 'thread-actions');
    actions.append(
      button(`Rename ${thread.name}`, '✎', () => {
        const value = window.prompt('Conversation name', thread.name);
        if (value?.trim()) { thread.name = value.trim().slice(0, 200); render(); persist(); }
      }),
      button(`${thread.pinned ? 'Unpin' : 'Pin'} ${thread.name}`, '◇', () => { thread.pinned = !thread.pinned; renderThreads(); persist(); }),
      button(`Delete ${thread.name}`, '×', () => {
        if (!window.confirm(`Delete “${thread.name}” from this browser?`)) return;
        if (activeRequest?.threadId === thread.id) activeRequest.controller.abort();
        state.threads = state.threads.filter(item => item.id !== thread.id);
        if (!state.threads.length) state.threads.push(makeThread());
        if (state.activeThreadId === thread.id) state.activeThreadId = state.threads[0].id;
        render(); persist();
      }),
    );
    row.append(select, actions); list.append(row);
  }
  if (!list.children.length) list.append(element('p', 'thread-empty', 'No conversations match your search.'));
}

// User and model text is rendered only through textContent. Fenced code is
// presentational; neither markup nor character scripts are ever evaluated.
function renderContent(target, content) {
  target.replaceChildren();
  const parts = content.split(/(```[^\n]*\n[\s\S]*?(?:```|$))/g);
  for (const part of parts) {
    if (!part) continue;
    if (!part.startsWith('```')) { target.append(element('p', 'text-block', part)); continue; }
    const firstLine = part.indexOf('\n');
    const language = part.slice(3, firstLine).slice(0, 40) || 'code';
    const code = part.slice(firstLine + 1).replace(/```$/, '');
    const block = element('div', 'code-block'), header = element('div', 'code-header');
    header.append(element('span', '', language), button('Copy code', 'Copy', async () => {
      try { await navigator.clipboard.writeText(code); notice('Code copied.'); }
      catch { notice('Clipboard access is unavailable. Select the code to copy it.', true); }
    }));
    block.append(header, element('pre', '', code)); target.append(block);
  }
}

function renderMessages(scroll = false) {
  const thread = currentThread(), list = $('message-list');
  $('welcome').hidden = thread.messages.length > 0;
  list.replaceChildren();
  for (const message of thread.messages) {
    const row = element('article', `message ${message.role}`);
    const avatar = element('div', 'message-avatar', message.role === 'user' ? 'YOU' : '✦');
    avatar.setAttribute('aria-hidden', 'true');
    const main = element('div', 'message-main'), heading = element('div', 'message-heading');
    heading.append(element('span', '', message.role === 'user' ? 'You' : 'Guilty Spark 343'));
    if (activeRequest?.assistantId === message.id) heading.append(element('span', 'message-subtitle', 'Generating…'));
    const body = element('div', 'message-body'); body.dataset.messageId = message.id;
    renderContent(body, message.content);
    if (!message.content && activeRequest?.assistantId !== message.id) body.append(element('p', 'text-block', '(Empty message)'));
    main.append(heading, body); row.append(avatar, main); list.append(row);
  }
  if (scroll) $('transcript').scrollTop = $('transcript').scrollHeight;
}

function updateControls() {
  $('send-button').disabled = Boolean(activeRequest) || !state.selectedModel || !$('prompt').value.trim();
  $('stop-button').hidden = !activeRequest;
  $('import-button').disabled = Boolean(activeRequest);
  $('composer-context').textContent = activeRequest
    ? (activeRequest.threadId === state.activeThreadId ? 'Generating locally…' : 'Generating in another conversation…')
    : (state.selectedModel ? `${currentCharacter()?.name ?? 'Default assistant'} · ${state.selectedModel}` : 'Choose an available model to begin');
}

function renderCharacters() {
  const select = $('character-select');
  select.replaceChildren(new Option('Default assistant', ''));
  for (const card of state.characters) select.add(new Option(card.name, card.id));
  select.value = state.activeCharacterId ?? '';
}

function render() {
  $('thread-title').textContent = currentThread().name;
  renderThreads(); renderCharacters(); renderMessages(true); updateControls();
}

function newConversation() {
  if (state.threads.length >= 100) return notice('The workspace holds up to 100 conversations. Export and delete one before creating another.', true);
  const thread = makeThread('New conversation');
  const greeting = currentCharacter()?.greeting;
  if (greeting) thread.messages.push({ id: crypto.randomUUID(), role: 'assistant', content: greeting });
  state.threads.unshift(thread); state.activeThreadId = thread.id;
  $('prompt').value = ''; notice(); render(); persist(); sidebar(false); $('prompt').focus();
}

async function api(route) {
  const response = await fetch(route, { signal: AbortSignal.timeout(10000) });
  const value = await response.json();
  if (!response.ok) throw new Error(value.error ?? `HTTP ${response.status}`);
  return value;
}

function renderModels() {
  const select = $('model-select'); select.replaceChildren();
  for (const model of models) select.add(new Option(model.name, model.name));
  if (!models.length) select.add(new Option('No local models found', ''));
  if (!models.some(item => item.name === state.selectedModel)) state.selectedModel = models.find(item => item.name === 'deepseek-coder:latest')?.name ?? models[0]?.name ?? '';
  select.value = state.selectedModel;
  updateControls();
}

async function connect() {
  try {
    const [inventory, platform] = await Promise.all([api('/api/tags'), api('/api/platform')]);
    models = inventory.models ?? []; renderModels();
    $('connection-text').textContent = 'Local · ready';
    $('connection-pill').classList.remove('offline');
    $('platform-info').textContent = `${platform.name} ${platform.version} · ${platform.integrations.join(' · ')}`;
    if (!models.length) notice('No local models are configured. Add an existing GGUF in the local server configuration and reload.', true);
  } catch (error) {
    $('connection-text').textContent = 'Offline'; $('connection-pill').classList.add('offline');
    notice(`Cannot connect to the local runtime: ${error.message}. Start the server, then reload this page.`, true);
  }
}

async function send(event) {
  event.preventDefault();
  const prompt = $('prompt').value.trim();
  if (!prompt || activeRequest || !state.selectedModel) return;
  const thread = currentThread();
  if (thread.messages.length > 998) return notice('This conversation has reached its message limit. Start a new conversation.', true);
  const user = { id: crypto.randomUUID(), role: 'user', content: prompt };
  let messages;
  try { messages = buildMessages({ ...thread, messages: [...thread.messages, user] }, currentCharacter(), state.persona); }
  catch (error) { return notice(error.message, true); }
  try {
    const pending = exportData({ ...state, threads: state.threads.map(item => item === thread ? { ...item, messages: [...item.messages, user] } : item) });
    // Leave room for the bounded response so a new turn remains exportable.
    if (new TextEncoder().encode(JSON.stringify(pending)).length > 2 * 1024 * 1024 - 400000) throw new Error('Workspace is near its storage limit. Export a backup and remove older conversations before continuing.');
  } catch (error) { return notice(error.message, true); }
  const requestBody = JSON.stringify({ model: state.selectedModel, messages, stream: true, options: { temperature: state.settings.temperature, num_predict: state.settings.numPredict } });
  if (new TextEncoder().encode(requestBody).length > 1024 * 1024) return notice('Conversation exceeds the server request limit. Start a new conversation.', true);
  const assistant = { id: crypto.randomUUID(), role: 'assistant', content: '' };
  const run = { threadId: thread.id, assistantId: assistant.id, controller: new AbortController() };
  activeRequest = run;
  thread.messages.push(user, assistant);
  if (['New chat', 'New conversation'].includes(thread.name)) thread.name = prompt.replace(/\s+/g, ' ').slice(0, 65);
  $('prompt').value = ''; notice(); render(); persist();
  let reader, complete = false, frameBuffer = '', pendingPaint = false;
  const paint = () => {
    if (pendingPaint) return;
    pendingPaint = true;
    requestAnimationFrame(() => {
      pendingPaint = false;
      if (state.activeThreadId !== thread.id || !state.threads.includes(thread)) return;
      const atBottom = $('transcript').scrollHeight - $('transcript').scrollTop - $('transcript').clientHeight < 150;
      renderMessages(atBottom);
    });
  };
  function frame(line) {
    if (!line.trim()) return;
    const item = JSON.parse(line);
    if (item.error) throw new Error(item.error);
    if (typeof item.message?.content === 'string') {
      if (assistant.content.length + item.message.content.length > 50000) throw new Error('Reply exceeds the 50,000 character limit; the partial reply was retained.');
      assistant.content += item.message.content; paint();
      clearTimeout(persistTimer); persistTimer = setTimeout(persist, 300);
    }
    if (item.done === true) complete = true;
  }
  try {
    const response = await fetch('/api/chat', { method: 'POST', headers: { 'content-type': 'application/json' }, body: requestBody, signal: AbortSignal.any([run.controller.signal, AbortSignal.timeout(310000)]) });
    if (!response.ok) {
      const value = await response.json().catch(() => ({}));
      throw new Error(value.error ?? `Runtime returned HTTP ${response.status}`);
    }
    if (!response.body) throw new Error('Runtime returned no stream.');
    reader = response.body.getReader(); const decoder = new TextDecoder();
    while (true) {
      const { value, done } = await reader.read();
      frameBuffer += done ? decoder.decode() : decoder.decode(value, { stream: true });
      if (frameBuffer.length > 1024 * 1024) throw new Error('Runtime stream record exceeded the size limit.');
      let newline;
      while ((newline = frameBuffer.indexOf('\n')) >= 0) { frame(frameBuffer.slice(0, newline)); frameBuffer = frameBuffer.slice(newline + 1); }
      if (done) break;
    }
    if (frameBuffer.trim()) frame(frameBuffer);
    if (!complete) throw new Error('Runtime stream ended before completion. The partial reply was retained.');
    $('live-status').textContent = 'Reply complete.';
  } catch (error) {
    if (run.controller.signal.aborted) notice('Generation stopped. Any partial reply is kept.');
    else notice(`Reply failed: ${error.message}`, true);
  } finally {
    await reader?.cancel().catch(() => {}); reader?.releaseLock();
    if (!assistant.content) thread.messages = thread.messages.filter(item => item !== assistant);
    if (activeRequest === run) activeRequest = null;
    render(); persist();
  }
}

function editCharacter(id = null) {
  editingCharacter = id;
  const card = state.characters.find(item => item.id === id) ?? {};
  const select = $('character-editor-select'); select.replaceChildren(new Option('Create a new character', ''));
  for (const item of state.characters) select.add(new Option(item.name, item.id));
  select.value = id ?? '';
  for (const [input, field] of [['name', 'name'], ['description', 'description'], ['personality', 'personality'], ['scenario', 'scenario'], ['greeting', 'greeting'], ['system', 'systemPrompt']]) $('character-' + input).value = card[field] ?? '';
  $('delete-character').hidden = !id;
}

$('chat-form').addEventListener('submit', send);
$('prompt').addEventListener('input', updateControls);
$('prompt').addEventListener('keydown', event => { if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) { event.preventDefault(); $('chat-form').requestSubmit(); } });
$('stop-button').addEventListener('click', () => activeRequest?.controller.abort());
$('new-chat').addEventListener('click', newConversation);
$('thread-search').addEventListener('input', renderThreads);
$('menu-button').addEventListener('click', () => sidebar(!$('sidebar').classList.contains('open')));
$('sidebar-scrim').addEventListener('click', () => sidebar(false));
$('model-select').addEventListener('change', () => { state.selectedModel = $('model-select').value; persist(); updateControls(); });
$('character-select').addEventListener('change', () => { state.activeCharacterId = $('character-select').value || null; persist(); updateControls(); });
for (const node of document.querySelectorAll('[data-prompt]')) node.addEventListener('click', () => { $('prompt').value = node.dataset.prompt; $('prompt').focus(); updateControls(); });
for (const node of document.querySelectorAll('.close-dialog')) node.addEventListener('click', () => node.closest('dialog').close());
for (const id of ['help-button', 'privacy-button']) $(id).addEventListener('click', () => $('about-dialog').showModal());
$('settings-button').addEventListener('click', () => {
  $('persona-input').value = state.persona; $('temperature-input').value = state.settings.temperature; $('tokens-input').value = state.settings.numPredict;
  $('settings-dialog').showModal();
});
$('settings-form').addEventListener('submit', event => {
  event.preventDefault();
  try {
    const candidate = exportData({ ...state, persona: $('persona-input').value, settings: { temperature: Number($('temperature-input').value), numPredict: Number($('tokens-input').value) } });
    state.persona = candidate.persona; state.settings = candidate.settings; persist(); $('settings-dialog').close(); notice('Settings saved.');
  } catch (error) { notice(error.message, true); }
});
$('characters-button').addEventListener('click', () => { editCharacter(state.activeCharacterId); $('characters-dialog').showModal(); });
$('character-editor-select').addEventListener('change', () => editCharacter($('character-editor-select').value || null));
$('new-character').addEventListener('click', () => editCharacter());
$('character-form').addEventListener('submit', event => {
  event.preventDefault();
  try {
    if (!editingCharacter && state.characters.length >= 200) throw new Error('This workspace holds up to 200 characters.');
    const input = {};
    for (const [control, field] of [['name', 'name'], ['description', 'description'], ['personality', 'personality'], ['scenario', 'scenario'], ['greeting', 'greeting'], ['system', 'systemPrompt']]) input[field] = $('character-' + control).value;
    if (editingCharacter) input.id = editingCharacter;
    const card = makeCharacter(input), index = state.characters.findIndex(item => item.id === editingCharacter);
    if (index >= 0) state.characters[index] = card; else state.characters.push(card);
    state.activeCharacterId = card.id; renderCharacters(); updateControls(); persist(); $('characters-dialog').close(); notice(`${card.name} selected. Start a new conversation to use their greeting.`);
  } catch (error) { notice(error.message, true); }
});
$('delete-character').addEventListener('click', () => {
  if (!window.confirm('Delete this character from this workspace?')) return;
  state.characters = state.characters.filter(item => item.id !== editingCharacter);
  if (state.activeCharacterId === editingCharacter) state.activeCharacterId = null;
  editCharacter(); renderCharacters(); updateControls(); persist();
});
$('import-button').addEventListener('click', () => { if (!activeRequest) $('import-file').click(); });
$('import-file').addEventListener('change', async () => {
  const file = $('import-file').files[0]; $('import-file').value = '';
  if (!file || activeRequest) return;
  try {
    if (file.size > 2 * 1024 * 1024) throw new Error('Import exceeds 2 MiB.');
    const source = await file.text(), result = importData(source, state), raw = JSON.parse(source);
    const isBackup = Object.hasOwn(raw, 'threads') || Object.hasOwn(raw, 'characters') || Object.hasOwn(raw, 'characterCards');
    if (isBackup && !window.confirm('Replace this workspace with the imported backup? Export your current workspace first if you need to retain it.')) return;
    state = result.state; renderModels(); render(); persist();
    notice(['Import complete.', ...result.warnings].join('\n')); sidebar(false);
  } catch (error) { notice(`Import failed: ${error.message}`, true); }
});
$('export-button').addEventListener('click', () => {
  try {
    const data = JSON.stringify(exportData(state));
    const url = URL.createObjectURL(new Blob([data], { type: 'application/json' }));
    const link = element('a'); link.href = url; link.download = `guilty-spark-343-${new Date().toISOString().slice(0, 10)}.json`;
    document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 30000);
    notice('Workspace exported. Keep this file to restore your conversations and characters.');
  } catch (error) { notice(`Export failed: ${error.message}`, true); }
});
document.addEventListener('keydown', event => {
  if (event.key === 'Escape') sidebar(false);
  if (event.key.toLowerCase() === 'n' && !event.ctrlKey && !event.altKey && !event.metaKey && !event.target.closest('input,textarea,select,dialog,[contenteditable]')) { event.preventDefault(); newConversation(); }
});
window.addEventListener('pagehide', () => { activeRequest?.controller.abort(); if (!restoredError) persist(); });
mobile.addEventListener('change', () => sidebar(false));
sidebar(false); render(); if (restoredError) notice(restoredError, true); void connect();
