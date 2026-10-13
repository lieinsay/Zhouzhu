import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { loadCatalog } from '../lib/catalog.mjs';
import { assertProjectStructure, validateProject, diffProjects, ROW_SHAPES } from '../lib/validation.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const APP_PATH = resolve(ROOT, 'tools/content-studio/public/app.js');
const baseline = await loadCatalog(ROOT);
const appSource = await readFile(APP_PATH, 'utf8');
const bootIndex = appSource.lastIndexOf('try{applyWorkspace');
assert(bootIndex > 0, '前端启动入口存在');
const stateSource = appSource.slice(0, bootIndex).replace(/^import[^\n]+\n/, '');

// Execute the production event handlers and state/render functions with a minimal
// DOM shell. These are business-state regressions, not browser/layout acceptance.
function frontend() {
  const handlers = new Map();
  const shell = new Map(['#app', '#toast', '#import-file'].map(selector => [selector, {
    innerHTML: '', textContent: '', className: '', addEventListener() {},
  }]));
  const requests = [];
  const context = vm.createContext({
    structuredClone, validateProject, diffProjects, ROW_SHAPES,
    initialWorkspace: { baseline, project: baseline, revision: 'initial' },
    document: {
      querySelector: selector => shell.get(selector) ?? null,
      querySelectorAll: () => [],
      addEventListener: (name, handler) => handlers.set(name, handler),
    },
    window: { addEventListener() {} },
    setTimeout: () => 0, clearTimeout() {},
    fetch: (path, options) => new Promise(resolveRequest => {
      requests.push({ path, body: JSON.parse(options.body), resolveRequest });
    }),
  });
  vm.runInContext(stateSource, context, { filename: APP_PATH });
  const run = source => vm.runInContext(source, context);
  run('applyWorkspace(initialWorkspace)');
  return {
    run,
    read: source => structuredClone(run(source)),
    input: target => handlers.get('input')({ target }),
    requests,
    completeSave(index = 0) {
      const request = requests[index];
      request.resolveRequest({ ok: true, json: async () => ({
        baseline: structuredClone(baseline), project: structuredClone(request.body.project), revision: `saved-${index}`,
      }) });
    },
  };
}

const input = (field, value, numeric = false) => ({
  dataset: { field, ...(numeric ? { type: 'number' } : {}) }, value,
  type: numeric ? 'number' : 'text', readOnly: false,
});

test('数值清空后切换页面再编辑，仍使用数字类型且可恢复完整草稿', () => {
  const f = frontend();
  const id = baseline.items[0].id;
  f.run(`navigate('items', ${JSON.stringify(id)})`);
  f.input(input('stack', '', true));
  assert.equal(f.read('selected().stack'), null);
  f.run(`navigate('recipes'); navigate('items', ${JSON.stringify(id)})`);
  const html = f.run('genericForm(selected())');
  const stackInput = html.match(/<input data-field="stack"[^>]*>/)?.[0];
  assert.match(stackInput, /type="number"/);
  assert.match(stackInput, /data-type="number"/);
  f.input(input('stack', '48', stackInput.includes('data-type="number"')));
  assert.equal(f.read('selected().stack'), 48);
  assert.doesNotThrow(() => assertProjectStructure(f.read('project')));
});

test('新增记录的ID临时与已有ID相同，继续输入不会修改原记录', () => {
  const f = frontend();
  const original = baseline.items[0];
  f.run("navigate('items'); newRecord()");
  f.input(input('id', original.id));
  f.input(input('id', `${original.id}_custom`));
  f.input(input('name', '新增草稿名称'));
  assert.deepEqual(f.read('project.items[0]'), original);
  const added = f.read('project.items.at(-1)');
  assert.equal(added.id, `${original.id}_custom`);
  assert.equal(added.name, '新增草稿名称');
  assert.equal(f.read('selected().id'), added.id);
});

test('清空各内容集合后仍能新增完整定义；没有道具的配方也可作为待修正草稿', () => {
  for (const collection of ['items', 'recipes', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops']) {
    const f = frontend();
    f.run(`project[${JSON.stringify(collection)}] = []; navigate(${JSON.stringify(collection)}); newRecord()`);
    const project = f.read('project');
    assert.equal(project[collection].length, 1, collection);
    assert.doesNotThrow(() => assertProjectStructure(project), collection);
    assert.equal(project[collection][0].source, undefined, collection);
  }
  const f = frontend();
  f.run("project.items = []; project.recipes = []; navigate('recipes'); newRecord()");
  assert.equal(f.read('project.recipes.length'), 1);
  assert.doesNotThrow(() => assertProjectStructure(f.read('project')));
  assert(f.read('issues').some(issue => issue.collection === 'recipes' && issue.severity === 'error'));
});

test('保存请求期间继续编辑，已发送快照不变且后续内容保持未保存状态', async () => {
  const f = frontend();
  f.run("navigate('items')");
  f.input(input('name', '本次请求保存的名称'));
  const pending = f.run('save()');
  assert.equal(f.requests.length, 1);
  assert.equal(f.requests[0].path, '/api/workspace');
  f.input(input('name', '请求期间继续修改的名称'));
  f.completeSave();
  await pending;
  assert.equal(f.requests[0].body.project.items[0].name, '本次请求保存的名称');
  assert.equal(f.read('savedProject.items[0].name'), '本次请求保存的名称');
  assert.equal(f.read('project.items[0].name'), '请求期间继续修改的名称');
  assert.equal(f.read('state.dirty'), true);
});

test('保存请求期间切换集合和选中条目，响应后保留当前选中定义', async () => {
  const f = frontend();
  f.run("navigate('recipes')");
  const pending = f.run('save()');
  const id = baseline.items.at(-1).id;
  f.run(`navigate('items', ${JSON.stringify(id)})`);
  f.completeSave();
  await pending;
  assert.equal(f.read('state.collection'), 'items');
  assert.equal(f.read('state.selected'), id);
  assert.equal(f.read('selected().id'), id);
  assert.equal(f.read('state.dirty'), false);
});

test('参数下拉可选完整源枚举，未知草稿值保持可见而不冒充第一项', () => {
  const f = frontend();
  assert(baseline.meta.enums.PartMaterial.includes('CLAY'));
  assert(!baseline.pieces.some(piece => piece.material === 'CLAY'));
  f.run("navigate('pieces'); selected().material = 'CLAY'");
  assert.match(f.run('genericForm(selected())'), /<option value="CLAY" selected>CLAY<\/option>/);
  f.run("selected().material = 'CUSTOM_MATERIAL'");
  assert.match(f.run('genericForm(selected())'), /<option value="CUSTOM_MATERIAL" selected>CUSTOM_MATERIAL · 需实现或修正<\/option>/);
  assert.equal(f.read('selected().material'), 'CUSTOM_MATERIAL');
});

test('导入工作站名称和非法ID中的HTML始终按文本转义', () => {
  const f = frontend();
  const injectedName = '<button data-action="reset">EVIL</button>';
  f.run(`navigate('recipes'); project.stations.find(row => row.id === 'WORKBENCH').name = ${JSON.stringify(injectedName)}; selected().station = 'WORKBENCH'`);
  for (const expression of ['renderWorkbench()', 'renderChain(selected())']) {
    const html = f.run(expression);
    assert(!html.includes(injectedName), expression);
    assert(html.includes('&lt;button data-action=&quot;reset&quot;&gt;EVIL&lt;/button&gt;'), expression);
  }
  const injectedId = 'EVIL" data-action="reset';
  f.run(`project.stations[1].id = ${JSON.stringify(injectedId)}; selected().station = ${JSON.stringify(injectedId)}`);
  const html = f.run('recipeForm(selected())');
  assert(!html.includes(`data-station="${injectedId}"`));
  assert(html.includes('data-station="EVIL&quot; data-action=&quot;reset"'));
});
