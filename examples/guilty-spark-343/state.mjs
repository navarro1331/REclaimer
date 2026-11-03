// Portable browser state. Imports are data, never executable configuration.
const LIMITS = Object.freeze({ bytes: 2 * 1024 * 1024, depth: 24, nodes: 20000, threads: 100, characters: 200, messages: 5000, perThread: 1000, text: 50000, field: 20000 });
const own = (value, key) => Object.prototype.hasOwnProperty.call(value, key);
const plain = value => value !== null && typeof value === 'object' && !Array.isArray(value)
  && [Object.prototype, null].includes(Object.getPrototypeOf(value));
const uuid = () => globalThis.crypto.randomUUID();
const defaults = () => ({ temperature: 0.7, numPredict: 256 });

function object(value, name) {
  if (!plain(value)) throw new TypeError(`${name} must be a plain object`);
  return value;
}

function checkInput(input) {
  let value = input;
  if (typeof input === 'string') {
    if (input.length > LIMITS.bytes || new TextEncoder().encode(input).length > LIMITS.bytes) throw new RangeError('Import exceeds 2 MiB');
    try { value = JSON.parse(input); } catch { throw new TypeError('Import must contain valid JSON'); }
  }
  let nodes = 0, bytes = 0;
  const seen = new Set();
  function visit(item, depth) {
    if (++nodes > LIMITS.nodes || depth > LIMITS.depth) throw new RangeError('Import is too large or deeply nested');
    if (typeof item === 'string') {
      bytes += item.length;
      if (bytes > LIMITS.bytes) throw new RangeError('Import exceeds the text limit');
      return;
    }
    if (item === null || typeof item === 'boolean' || (typeof item === 'number' && Number.isFinite(item))) return;
    if (typeof item !== 'object' || (!Array.isArray(item) && !plain(item))) throw new TypeError('Import must contain plain JSON data');
    if (seen.has(item)) throw new TypeError('Import cannot contain circular or shared object references');
    seen.add(item);
    for (const key of Reflect.ownKeys(item)) {
      if (Array.isArray(item) && key === 'length') continue;
      if (typeof key !== 'string' || key.length > 200) throw new TypeError('Import contains an invalid property name');
      const descriptor = Object.getOwnPropertyDescriptor(item, key);
      if (!descriptor || !own(descriptor, 'value')) throw new TypeError('Import cannot contain property accessors');
      bytes += key.length;
      visit(descriptor.value, depth + 1);
    }
  }
  visit(value, 0);
  return object(value, 'Import');
}

function text(value, name, maximum = LIMITS.field, fallback = '') {
  if (value === undefined || value === null) return fallback;
  if (typeof value !== 'string') throw new TypeError(`${name} must be text`);
  if (value.length > maximum) throw new RangeError(`${name} exceeds ${maximum} characters`);
  return value;
}

function name(value, fallback) {
  return text(value, 'Name', 200, fallback).trim() || fallback;
}

function narrative(value, label) {
  if (Array.isArray(value)) {
    if (value.length > 200) throw new RangeError(`${label} exceeds 200 entries`);
    return text(value.map(item => text(item, label)).join('\n\n'), label);
  }
  return text(value, label);
}

function combineText(first, second, label) {
  const parts = [first, second].filter(item => item !== '');
  return text([...new Set(parts)].join('\n\n'), label);
}

function identity(value) {
  return typeof value === 'string' && /^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$/.test(value) ? value : uuid();
}

function omitted(value, allowed, warnings, context) {
  const unsupported = Object.keys(value).filter(key => !allowed.includes(key));
  if (unsupported.length) {
    const labels = unsupported.slice(0, 12).map(key => /^[A-Za-z_][A-Za-z0-9_]{0,39}$/.test(key) ? key : '[unrecognized field]');
    warnings.add(`Unsupported fields were omitted from ${context}: ${labels.join(', ')}${unsupported.length > 12 ? ', ...' : ''}. Credentials, executable code, extensions, and automation are never imported.`);
  }
}

function collection(value, maximum, label) {
  const entries = Array.isArray(value) ? value.map(item => [undefined, item]) : plain(value) ? Object.entries(value) : null;
  if (!entries) throw new TypeError(`${label} must be an array or keyed object`);
  if (entries.length > maximum) throw new RangeError(`${label} exceeds ${maximum} entries`);
  return entries;
}

export function makeThread(threadName = 'New chat') {
  return { id: uuid(), name: name(threadName, 'New chat'), messages: [], createdAt: new Date().toISOString(), pinned: false };
}

export function createState() {
  const thread = makeThread();
  return { version: 1, threads: [thread], activeThreadId: thread.id, characters: [], activeCharacterId: null, persona: '', selectedModel: '', settings: defaults() };
}

function character(value, warnings, keyedId) {
  object(value, 'Character');
  let card = value;
  if (own(value, 'spec')) {
    if (!['chara_card_v2', 'chara_card_v3'].includes(value.spec)) throw new TypeError('Unsupported character card specification');
    omitted(value, ['spec', 'spec_version', 'data'], warnings, 'character card wrapper');
    card = object(value.data, 'Character card data');
  }
  omitted(card, ['id', 'name', 'description', 'writingStyle', 'personality', 'scenario', 'startingLore', 'greeting', 'first_mes', 'systemPrompt', 'system_prompt'], warnings, 'character card');
  return {
    id: identity(card.id ?? keyedId), name: name(card.name, 'New character'),
    description: text(card.description, 'Description'), personality: text(card.personality, 'Personality'),
    scenario: combineText(text(card.scenario, 'Scenario'), narrative(card.startingLore, 'Starting lore'), 'Scenario'), greeting: text(card.greeting ?? card.first_mes, 'Greeting', LIMITS.text),
    systemPrompt: combineText(text(card.systemPrompt ?? card.system_prompt, 'Character system prompt'), text(card.writingStyle, 'Writing style'), 'Character system prompt'),
  };
}

export function makeCharacter(input = {}) {
  return character(checkInput(input), new Set());
}

function message(value, warnings) {
  object(value, 'Message');
  if (own(value, 'tools') || own(value, 'tool_calls') || own(value, 'tool_call_id') || own(value, 'function_call')) throw new TypeError('Tool messages and tool calls cannot be imported');
  omitted(value, ['id', 'role', 'sender', 'content', 'text'], warnings, 'message');
  const original = value.role ?? value.sender;
  const roles = new Map([['user', 'user'], ['human', 'user'], ['assistant', 'assistant'], ['ai', 'assistant'], ['model', 'assistant'], ['bot', 'assistant']]);
  const role = typeof original === 'string' ? roles.get(original.toLowerCase()) : undefined;
  if (!role) throw new TypeError('Conversation messages must have user or assistant roles');
  const content = value.content ?? value.text;
  if (typeof content !== 'string') throw new TypeError('Message content must be text');
  return { id: identity(value.id), role, content: text(content, 'Message content', LIMITS.text) };
}

function thread(value, keyedId, warnings, budget) {
  object(value, 'Thread');
  omitted(value, ['id', 'name', 'title', 'messages', 'history', 'createdAt', 'created_at', 'pinned', 'context', 'lore'], warnings, 'thread');
  const list = value.messages ?? value.history ?? [];
  if (!Array.isArray(list)) throw new TypeError('Thread messages must be an array');
  if (list.length > LIMITS.perThread || (budget.messages += list.length) > LIMITS.messages) throw new RangeError('Import contains too many messages');
  const messages = list.map(item => message(item, warnings));
  unique(messages, warnings, 'message');
  const timestamp = value.createdAt ?? value.created_at;
  if (timestamp !== undefined && !((typeof timestamp === 'string' && timestamp.length <= 64 && Number.isFinite(Date.parse(timestamp))) || (typeof timestamp === 'number' && Number.isFinite(timestamp) && Number.isFinite(new Date(timestamp).getTime())))) throw new TypeError('Thread createdAt must be a valid date string or finite millisecond timestamp');
  if (value.pinned !== undefined && typeof value.pinned !== 'boolean') throw new TypeError('Thread pinned must be a boolean');
  const context = combineText(text(value.context, 'Thread context'), narrative(value.lore, 'Thread lore'), 'Thread context');
  return { id: identity(value.id ?? keyedId), name: name(value.name ?? value.title, 'Imported chat'), messages, createdAt: timestamp === undefined ? new Date().toISOString() : new Date(timestamp).toISOString(), pinned: value.pinned ?? false, ...(context ? { context } : {}) };
}

function unique(items, warnings, label) {
  const seen = new Set();
  for (const item of items) {
    if (seen.has(item.id)) { item.id = uuid(); warnings.add(`Duplicate ${label} identifiers were replaced.`); }
    seen.add(item.id);
  }
}

function settings(value, warnings) {
  if (value === undefined) return defaults();
  object(value, 'Settings');
  omitted(value, ['temperature', 'numPredict'], warnings, 'settings');
  const temperature = value.temperature ?? 0.7;
  const numPredict = value.numPredict ?? 256;
  if (typeof temperature !== 'number' || !Number.isFinite(temperature) || temperature < 0 || temperature > 2) throw new RangeError('Temperature must be between 0 and 2');
  if (!Number.isSafeInteger(numPredict) || numPredict < 1 || numPredict > 32768) throw new RangeError('numPredict must be an integer between 1 and 32768');
  return { temperature, numPredict };
}

function selectedPersona(value, warnings) {
  if (value.persona !== undefined) return text(value.persona, 'Persona');
  if (value.personaCards === undefined) return '';
  const cards = collection(value.personaCards, LIMITS.characters, 'Personas').map(([key, item]) => {
    object(item, 'Persona');
    omitted(item, ['id', 'name', 'description', 'injectionFrequency'], warnings, 'persona card');
    if (item.injectionFrequency !== undefined && (!Number.isSafeInteger(item.injectionFrequency) || item.injectionFrequency < 0)) throw new TypeError('Persona injectionFrequency must be a nonnegative integer');
    return { id: identity(item.id ?? key), name: text(item.name, 'Persona name', 200), description: text(item.description, 'Persona description'), injectionFrequency: item.injectionFrequency ?? 5 };
  });
  const chosen = cards.find(item => item.id === value.activePersonaId);
  if (!chosen) return '';
  if (cards.length > 1) warnings.add('Only the selected persona was imported; other persona cards were omitted.');
  if (chosen.injectionFrequency === 0) {
    warnings.add('The selected persona disables prompt injection; its name and description were not added to the active persona text.');
    return '';
  }
  warnings.add('The selected persona is included on every request; its original per-turn injection frequency is not supported.');
  return text([chosen.name ? `Name: ${chosen.name}` : '', chosen.description].filter(Boolean).join('\n\n'), 'Persona');
}

function normalizeState(value, warnings) {
  if (value.version !== undefined && value.version !== 1) throw new TypeError('Unsupported state version');
  omitted(value, ['version', 'threads', 'threadOrder', 'activeThreadId', 'characters', 'characterCards', 'activeCharacterId', 'activeCardId', 'persona', 'personaCards', 'activePersonaId', 'selectedModel', 'settings'], warnings, 'backup');
  const budget = { messages: 0 };
  const threads = collection(value.threads ?? [], LIMITS.threads, 'Threads').map(([key, item]) => thread(item, key, warnings, budget));
  if (!threads.length) threads.push(makeThread());
  const characters = collection(value.characters ?? value.characterCards ?? [], LIMITS.characters, 'Characters').map(([key, item]) => character(item, warnings, key));
  unique(threads, warnings, 'thread'); unique(characters, warnings, 'character');
  if (value.threadOrder !== undefined) {
    if (!Array.isArray(value.threadOrder) || value.threadOrder.length > LIMITS.threads || value.threadOrder.some(id => typeof id !== 'string')) throw new TypeError('threadOrder must be a bounded array of identifiers');
    const positions = new Map();
    for (const id of value.threadOrder) if (!positions.has(id)) positions.set(id, positions.size);
    threads.sort((a, b) => (positions.get(a.id) ?? LIMITS.threads) - (positions.get(b.id) ?? LIMITS.threads));
    if (value.threadOrder.some(id => !threads.some(item => item.id === id))) warnings.add('Unknown threadOrder identifiers were ignored; all available threads were retained.');
  }
  const activeCharacterId = value.activeCharacterId ?? value.activeCardId;
  return {
    version: 1, threads, activeThreadId: threads.some(item => item.id === value.activeThreadId) ? value.activeThreadId : threads[0].id,
    characters, activeCharacterId: characters.some(item => item.id === activeCharacterId) ? activeCharacterId : null,
    persona: selectedPersona(value, warnings), selectedModel: text(value.selectedModel, 'Selected model', 300), settings: settings(value.settings, warnings),
  };
}

export function importData(input, currentState) {
  const value = checkInput(input);
  const warnings = new Set();
  if (own(value, 'threads') || own(value, 'characters') || own(value, 'characterCards')) {
    return { state: normalizeState(value, warnings), warnings: [...warnings] };
  }
  if (!own(value, 'name') && !own(value, 'spec')) throw new TypeError('Import is not a supported state backup or character card');
  const state = currentState === undefined ? createState() : normalizeState(checkInput(currentState), warnings);
  if (state.characters.length >= LIMITS.characters) throw new RangeError('Characters exceeds 200 entries');
  const card = character(value, warnings);
  if (state.characters.some(item => item.id === card.id)) card.id = uuid();
  state.characters.push(card);
  state.activeCharacterId = card.id;
  return { state, warnings: [...warnings] };
}

export function exportData(state) {
  return normalizeState(checkInput(state), new Set());
}

export function buildMessages(threadValue, characterValue, persona = '') {
  const warnings = new Set();
  const normalized = thread(checkInput(threadValue), undefined, warnings, { messages: 0 });
  const card = characterValue === undefined || characterValue === null ? null : character(checkInput(characterValue), warnings);
  const parts = [];
  if (card) {
    if (card.systemPrompt) parts.push(card.systemPrompt);
    parts.push(`Character: ${card.name}`);
    if (card.description) parts.push(`Description: ${card.description}`);
    if (card.personality) parts.push(`Personality: ${card.personality}`);
    if (card.scenario) parts.push(`Scenario: ${card.scenario}`);
  }
  if (normalized.context) parts.push(`Conversation context:\n${normalized.context}`);
  const personaText = text(persona, 'Persona');
  if (personaText) parts.push(`User persona: ${personaText}`);
  const history = [];
  for (const item of normalized.messages) {
    if (!history.length && item.role === 'assistant') {
      parts.push(`Opening assistant context from conversation history:\n${item.content}`);
    } else if (history.at(-1)?.role === item.role) {
      history.at(-1).content += `\n\n${item.content}`;
    } else history.push({ role: item.role, content: item.content });
  }
  if (history.at(-1)?.role !== 'user') throw new TypeError('A generation request must end with a user message');
  return [
    ...(parts.length ? [{ role: 'system', content: parts.join('\n\n') }] : []),
    ...history,
  ];
}
