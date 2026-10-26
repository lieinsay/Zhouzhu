let state, dirty = false;
const el = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function api(path, data) {
  const response = await fetch(path, data ? {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)} : {});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || '请求失败');
  return result;
}
function render(message = '') {
  dirty = false;
  el('assets').innerHTML = state.registry.assets.map(asset => {
    const chosen = state.pending[asset.id] || '';
    const current = state.active.bindings[asset.id]?.version || '未应用（游戏使用回退）';
    const version = asset.versions.find(v => v.version === chosen) || asset.versions.at(-1);
    return `<article class="art-card" data-id="${esc(asset.id)}"><img src="/api/art/image/${esc(asset.id)}/${esc(version.version)}" alt="${esc(asset.name)}"><h2>${esc(asset.name)} <small>${esc(asset.id)}</small></h2><label>待绑定版本<select aria-label="${esc(asset.id)}版本"><option value="">取消正式绑定</option>${asset.versions.map(v => `<option value="${esc(v.version)}" ${v.version===chosen?'selected':''} ${v.agentReview?.status!=='accepted'?'disabled':''}>${esc(v.version)} · ${esc(v.origin)} · ${esc(v.agentReview?.status)}</option>`).join('')}</select></label><p>游戏已应用：${esc(current)}<br>代理审图：${esc(version.agentReview?.status)}<br>用户逐项确认：${esc(version.userConfirmation || '未逐项确认')}<br>来源：${esc(version.original.path)}<br>派生：${esc(version.runtime.path)}<br>SHA256 ${esc(version.runtime.sha256)}</p><details><summary>提示词与审图记录</summary><pre>${esc(version.prompt)}</pre><p>${esc(version.agentReview?.note)}</p></details></article>`;
  }).join('');
  el('application').textContent = JSON.stringify(state.active.application || {}, null, 2);
  el('status').textContent = message || `${state.registry.assets.length} 项登记；${Object.keys(state.active.bindings).length} 项已应用`;
  el('apply').disabled = !state.hasDraft || state.sourceChanged;
}
async function run(action) {
  for (const id of ['refresh','stage','apply']) el(id).disabled = true;
  try { await action(); } catch (error) { el('status').textContent = error.message; }
  finally { el('refresh').disabled = false; el('stage').disabled = false; el('apply').disabled = dirty || !state?.hasDraft || state?.sourceChanged; }
}
el('refresh').onclick = () => run(async () => {state=await api('/api/art');render();});
el('stage').onclick = () => run(async () => {state=await api('/api/art/stage',{revision:state.revision,bindings:state.pending});render('美术绑定已独立暂存；核对后可应用。');});
el('apply').onclick = () => run(async () => {state=await api('/api/art/apply',{revision:state.revision});render('美术已应用到游戏；重新启动游戏读取，内容草稿未应用。');});
el('assets').onchange = event => {
  const select = event.target.closest('select'); if (!select) return;
  const card=select.closest('[data-id]'), id=card.dataset.id;
  if (select.value) state.pending[id]=select.value; else delete state.pending[id];
  if (select.value) card.querySelector('img').src='/api/art/image/'+id+'/'+select.value;
  dirty=true; el('apply').disabled=true; el('status').textContent='有未暂存美术选择；先暂存再应用。';
};
await run(async () => {state=await api('/api/art');render();});
