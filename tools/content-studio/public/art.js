import {ModelViewer} from './model-viewer.js';
let state, dirty = false, modelViewer, resourceKind='images';
const savedViews=new Map();
const apiBase=()=>resourceKind==='models'?'/api/art/models':'/api/art';
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
  el('assets').innerHTML = state.registry.assets.filter(asset => !el('namespace').value || asset.id.startsWith(el('namespace').value+'.')).map(asset => {
    const chosen = state.pending[asset.id] || '';
    const current = state.active.bindings[asset.id]?.version || '未应用（游戏使用回退）';
    const version = asset.versions.find(v => v.version === chosen) || asset.versions.at(-1);
    return `<article class="art-card" data-id="${esc(asset.id)}"><img src="${apiBase()}/image/${esc(asset.id)}/${esc(version.version)}" alt="${esc(asset.name)}"><h2>${esc(asset.name)} <small>${esc(asset.id)}</small></h2><label>待绑定版本<select aria-label="${esc(asset.id)}版本"><option value="">取消正式绑定</option>${asset.versions.map(v => `<option value="${esc(v.version)}" ${v.version===chosen?'selected':''} ${v.agentReview?.status!=='accepted'||v.bindingPolicy==='fixture_only'?'disabled':''}>${esc(v.version)} · ${esc(v.origin)} · ${esc(v.agentReview?.status)}</option>`).join('')}</select></label><p>${version.bindingPolicy==='fixture_only'?'仅受控样件，未开放游戏绑定。<br>':''}${version.styleGap?esc(version.styleGap)+"<br>":""}${version.author?`作者：${esc(version.author.name)} · 来源许可：${esc(version.author.license)}<br>`:''}${version.skeleton?`骨骼：${esc(version.skeleton.bones)} · 服装与身体逐顶点蒙皮<br>`:''}游戏已应用：${esc(current)}<br>代理审图：${esc(version.agentReview?.status)}<br>用户逐项确认：${esc(version.userConfirmation || '未逐项确认')}<br>来源：${esc(version.original.path)}<br>派生：${esc(version.runtime.path)}<br>SHA256 ${esc(version.runtime.sha256)}</p>${version.model?`<button class="btn" data-model-version="${esc(version.version)}">旋转查看同源网格</button>`:""}<details><summary>${resourceKind==='models'?'来源、派生与审查':'提示词与审图记录'}</summary><pre>${esc(version.prompt || version.derivation?.operation || '')}</pre><p>${esc(version.agentReview?.note || version.agentReview?.scope)}</p></details></article>`;
  }).join('');
  el('application').textContent = JSON.stringify(state.active.application || {}, null, 2);
  el('status').textContent = message || `${state.registry.assets.length} 项登记；${Object.keys(state.active.bindings).length} 项已应用`;
  el('apply').disabled = !state.hasDraft || state.sourceChanged;
  el('apply').textContent=resourceKind==='models'?'应用实体到游戏':'应用美术到游戏';
  el('stage').textContent=resourceKind==='models'?'暂存实体绑定':'暂存美术绑定';
  el('resource-helper').textContent=resourceKind==='models'?'此页仅应用已审查的真实三维模型与视觉参数；图片、模型和玩法草稿分别暂存。应用后重启游戏读取。':'此页只应用已登记并通过代理审图的图片版本；不应用配方、经济、模型变换或其他内容草稿。用户确认栏仅在用户逐项确认后记录。';
}
async function run(action) {
  for (const id of ['refresh','stage','apply']) el(id).disabled = true;
  try { await action(); } catch (error) { el('status').textContent = error.message; }
  finally { el('refresh').disabled = false; el('stage').disabled = false; el('apply').disabled = dirty || !state?.hasDraft || state?.sourceChanged; }
}
el('refresh').onclick = () => run(async () => {state=await api(apiBase());render();});
el('stage').onclick = () => run(async () => {state=await api(apiBase()+'/stage',{revision:state.revision,bindings:state.pending});render('绑定已独立暂存；核对后可应用。');});
el('apply').onclick = () => run(async () => {state=await api(apiBase()+'/apply',{revision:state.revision});render('已应用到游戏；重新启动读取，内容草稿未应用。');});
el('assets').onchange = event => {
  const select = event.target.closest('select'); if (!select) return;
  const card=select.closest('[data-id]'), id=card.dataset.id;
  if (select.value) state.pending[id]=select.value; else delete state.pending[id];
  render('有未暂存美术选择；先暂存再应用。');
  dirty=true; el('apply').disabled=true; el('status').textContent='有未暂存美术选择；先暂存再应用。';
};
await run(async () => {state=await api(apiBase());render();});
el('resource-kind').onchange=()=>run(async()=>{
  savedViews.set(resourceKind,{state,dirty,namespace:el('namespace').value});
  resourceKind=el('resource-kind').value;
  const saved=savedViews.get(resourceKind);state=saved?.state||await api(apiBase());el('namespace').value=saved?.namespace||'';
  render();dirty=saved?.dirty||false;el('apply').disabled=dirty||!state.hasDraft||state.sourceChanged;
});

el('namespace').onchange = () => { const wasDirty=dirty; render(); dirty=wasDirty; el('apply').disabled = dirty || !state?.hasDraft || state?.sourceChanged; };
el('assets').onclick=async event=>{
  const button=event.target.closest('[data-model-version]');if(!button)return;
  const id=button.closest('[data-id]').dataset.id;
  const dialog=el('model-dialog');dialog.showModal();
  modelViewer?.dispose();modelViewer=new ModelViewer(el('model-canvas'),{onStatus:message=>el('model-status').textContent=message});
  try{await modelViewer.load(apiBase()+'/model/'+id+'/'+button.dataset.modelVersion,{});if(resourceKind==='models'&&id.startsWith('actor.')){modelViewer.setWireframe(true);el('model-wire').checked=true;}}catch(error){el('model-status').textContent=error.message;}
};
el('model-close').onclick=()=>el('model-dialog').close();
el('model-dialog').onclose=()=>{modelViewer?.dispose();modelViewer=null;};
el('model-wire').onchange=event=>modelViewer?.setWireframe(event.target.checked);
