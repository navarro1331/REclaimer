/* Newly authored integration; loaded after the compatible UI, before its boot. */
'use strict';
let sparkRevision = '', sparkDirty = false, sparkSaving = false, sparkConflict = false;
const sparkOriginalLoad = loadState, sparkOriginalSave = saveState;
const sparkUuid = () => (crypto.randomUUID ? crypto.randomUUID() : 'spark-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2));
function sparkStashDraft(){const t=getActiveThread(),input=document.getElementById('msg-input');if(t&&input)t.draft=input.value;}
const sparkSwitchThread=switchThread;
switchThread=function(id){sparkStashDraft();sparkSwitchThread(id);document.getElementById('msg-input').value=getActiveThread()?.draft||'';};
const sparkCreateThread=createThread;
createThread=function(){sparkStashDraft();sparkCreateThread();document.getElementById('msg-input').value='';};
const sparkForkAt=forkAt;
forkAt=function(index){sparkStashDraft();sparkForkAt(index);sparkStashDraft();saveState();};
let sparkDraftTimer;
document.getElementById('msg-input').addEventListener('input',()=>{sparkStashDraft();sparkDirty=true;clearTimeout(sparkDraftTimer);sparkDraftTimer=setTimeout(()=>saveState(),300);});
function sparkIdentity() {
  for (const t of state.threads) {
    if(t.model && t.model !== window.sparkModel) throw Error('This conversation belongs to '+t.model+'. Open that model workspace.');
    t.model = window.sparkModel;
    for (const m of t.messages || []) if (!m.id) m.id = sparkUuid();
  }
}
loadState = async function() {
  const response = await fetch('/state', {cache:'no-store'});
  if (!response.ok) throw Error('Could not open this model workspace: '+response.status);
  const saved = await response.json();
  // Search credentials remain only in this browser's existing per-model cache.
  try { const cached=JSON.parse(localStorage.getItem(STORAGE_KEY)||'{}'); if(cached.settings?.apiKey){saved.settings ||= {};saved.settings.apiKey=cached.settings.apiKey;} } catch {}
  sparkRevision = saved.sparkRevision;
  try { _idb = await idbOpen(); STORAGE_BACKEND = 'idb'; } catch { STORAGE_BACKEND = 'localstorage'; }
  await sparkOriginalLoad(JSON.stringify(saved));
  sparkIdentity();
};
saveState = function(options) {
  sparkIdentity();
  sparkOriginalSave(options);
  sparkDirty = true;
  void sparkSave();
};
async function sparkSave() {
  if (sparkSaving || !sparkDirty || sparkConflict || !sparkRevision) return !sparkConflict;
  sparkSaving = true; sparkDirty = false;
  try {
    sparkIdentity();
    const body = JSON.parse(redactedSyncJson());
    body.sparkRevision = sparkRevision;
    const response = await fetch('/state', {method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const saved = await response.json();
    if (!response.ok) {
      sparkDirty = true;
      if (response.status === 409 && !String(saved.error).includes('busy')) {
        sparkConflict = true;
        toast('This model changed in another window. Export your browser copy before reloading.');
      }
      throw Error(saved.error || 'Workspace save failed');
    }
    sparkRevision = saved.sparkRevision;
    if (saved.sparkMemoryWarning) toast(saved.sparkMemoryWarning);
    const indicator = document.getElementById('spark-save-status');
    if (indicator) indicator.textContent = 'Saved in 343';
    return true;
  } catch (error) {
    const indicator = document.getElementById('spark-save-status');
    if (indicator) indicator.textContent = error.message;
    return false;
  } finally { sparkSaving = false; }
}
// Native storage is authoritative. Never race it with the old /state mirror.
scheduleStateSync = () => {};
forceServerFlush = () => { sparkDirty = true; void sparkSave(); };
checkServerStateOnBoot = async () => {};
setInterval(() => { if (sparkDirty) void sparkSave(); }, 1000);
let sparkRefreshing = false;
async function sparkRefresh() {
  if (!sparkRevision || sparkRefreshing || sparkSaving || sparkDirty || sparkConflict || isGenerating || state.editingIndex != null || document.querySelector('.modal.open,dialog[open]')) return;
  sparkRefreshing = true;
  const priorRevision = sparkRevision;
  try {
    const response = await fetch('/state', {cache:'no-store'});
    if (!response.ok) return;
    const saved = await response.json();
    if (sparkSaving || sparkDirty || isGenerating || sparkRevision !== priorRevision || saved.sparkRevision === priorRevision) return;
    const active = state.activeThreadId, input = document.getElementById('msg-input'), draft = input?.value || '';
    saved.settings ||= {}; saved.settings.apiKey = state.settings.apiKey || '';
    applyLoadedState(saved); sparkRevision = saved.sparkRevision; sparkIdentity();
    state.activeThreadId = state.threads.some(t=>t.id===active) ? active : null;
    render(); if(input) input.value = draft;
    const indicator=document.getElementById('spark-save-status');if(indicator)indicator.textContent='Updated from 343';
  } catch {} finally { sparkRefreshing = false; }
}
setInterval(() => void sparkRefresh(), 5000);
const sparkLoadActiveModel = loadActiveModel;
loadActiveModel = async function() { await sparkLoadActiveModel(); activeModel.thinkingFormat = identifyModelFromFilename(window.sparkModel).thinkingFormat || 'none'; };
window.addEventListener('beforeunload', event => {
  if (sparkDirty || sparkSaving || sparkConflict) { event.preventDefault(); event.returnValue = ''; }
});
onHeaderModelChange = async function(value) {
  const next = typeof value === 'string' ? value : value?.value;
  if (!next || next === window.sparkModel) return;
  if (isGenerating) { if(value?.value)value.value=window.sparkModel;toast('Stop generation before changing models.');return; }
  if (!(await sparkFlush())) { if(value?.value)value.value=window.sparkModel;return; }
  location.href = '/m/' + encodeURIComponent(next) + '/chat.html';
};
async function sparkFlush() {
  const deadline=Date.now()+10000;
  do {
    while(sparkSaving&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,50));
    if(sparkSaving||sparkConflict)return false;
    if(!(await sparkSave()))return false;
  } while(sparkDirty&&Date.now()<deadline);
  return !sparkDirty&&!sparkSaving&&!sparkConflict;
}
_restartModelForPerf = async function() { toast('Runtime settings saved. They apply to the next reply.'); };
async function sparkMemories() {
  if(isGenerating||!(await sparkFlush())){toast('Finish the current save or generation first.');return;}
  const response = await fetch('/spark/memories');
  if (!response.ok) { toast('Memories could not be read.'); return; }
  const memory = await response.json();
  const modal = document.createElement('dialog');
  modal.style.cssText = 'background:#142030;color:#e1ecf5;border:1px solid #69dfec;border-radius:12px;max-width:640px;width:85vw;padding:24px';
  modal.innerHTML = '<h2>Memories for this model</h2><p class="model"></p><label><input type="checkbox" name="enabled"> Use memories</label><br><label><input type="checkbox" name="automatic"> Learn clear facts automatically</label><p>One memory per line. Remove a line to forget it.</p><textarea rows="12" style="width:100%;box-sizing:border-box"></textarea><p class="error"></p><button class="cancel">Cancel</button> <button class="save">Save</button>';
  modal.querySelector('.model').textContent = window.sparkModel;
  modal.querySelector('[name=enabled]').checked = memory.enabled;
  modal.querySelector('[name=automatic]').checked = memory.automatic;
  modal.querySelector('textarea').value = memory.text;
  modal.querySelector('.cancel').onclick = () => modal.close();
  modal.onclose = () => modal.remove();
  modal.querySelector('.save').onclick = async () => {
    const result = await fetch('/spark/memories',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:modal.querySelector('textarea').value,enabled:modal.querySelector('[name=enabled]').checked,automatic:modal.querySelector('[name=automatic]').checked})});
    const data = await result.json();
    if (!result.ok) { modal.querySelector('.error').textContent = data.error; return; }
    const latest = await (await fetch('/state')).json();sparkRevision = latest.sparkRevision;
    modal.close();toast('Memories saved for '+window.sparkModel);
  };
  document.body.appendChild(modal);modal.showModal();
}
const bar = document.createElement('div');
bar.style.cssText = 'flex:0 0 auto;background:#101927;color:#cfe5f0;border-bottom:1px solid #263b4b;padding:5px 10px;font:11px system-ui;display:flex;gap:10px;align-items:center;flex-wrap:wrap';
const brand = document.createElement('span');brand.textContent = 'Guilty Spark 343';
const memoryButton = document.createElement('button');memoryButton.textContent = 'Model memories';memoryButton.onclick = sparkMemories;
memoryButton.className = 'btn btn-sm';
const saveStatus = document.createElement('span');saveStatus.id = 'spark-save-status';saveStatus.textContent = 'Saved in 343';
bar.append(brand,memoryButton,saveStatus);document.getElementById('chat').insertBefore(bar,document.getElementById('messages'));
document.title = 'Guilty Spark 343 — '+window.sparkModel;
