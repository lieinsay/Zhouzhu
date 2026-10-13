import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as media from '../public/media.js';
import { ModelViewer, modelMatrix, transformPoint, prepareMesh } from '../public/model-viewer.js';
import { loadCatalog } from '../lib/catalog.mjs';
import { validateProject, diffProjects, ROW_SHAPES } from '../lib/validation.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const baseline = await loadCatalog(ROOT);
const app = await readFile(resolve(ROOT, 'tools/content-studio/public/app.js'), 'utf8');
const stateSource = app.slice(0, app.lastIndexOf('try{applyWorkspace')).replace(/^import[^\n]+\n/, '');
const project = () => ({ items: [{ id: 'timber', name: '原木' }, { id: 'stone', name: '石头' }], props: [{ id: 'bench', item: 'timber' }] });
const library = media.normalizeLibrary({
  thumbnails: [{ id: 'timber-v1', itemId: 'timber', name: '原木', url: '/assets/thumbnails/timber-v1.png' }],
  models: [
    { id: 'scatter:tree', itemIds: ['timber'], collection: 'scatters', recordIds: ['tree'], usage: 'gather-source', kind: 'game', format: 'mesh-json', url: '/assets/models/tree.json' },
    { id: 'prop:bench', itemIds: ['timber'], collection: 'props', recordIds: ['bench'], usage: 'placed', kind: 'game', format: 'mesh-json', url: '/assets/models/bench.json' },
  ],
  missingItems: [{ id: 'stone', reason: '没有独立库存实体。' }], missingThumbnails: [{ id: 'stone' }],
});

test('媒体资产只能使用本地资产库URL，保留两种明确的缺失原因', () => {
  for (const bad of ['https://evil.test/icon.png', '//evil.test/icon.png', '/assets/../config', '/assets/%2e%2e/config', '/assets/%252e%252e/config', '/assets\\models\\a.json', '/assets/a.png?redirect=1', '/api/workspace']) assert.equal(media.safeAssetUrl(bad), null, bad);
  assert.equal(media.safeAssetUrl('/api/models/0a1b-22'), '/api/models/0a1b-22');
  const normalized = media.normalizeLibrary({ thumbnails: [{ id: 'bad', url: 'https://evil.test/icon.png' }, library.thumbnails[0]], models: [{ ...library.models[0], format: 'obj' }, library.models[1]], missingItems: library.missingItems, missingThumbnails: library.missingThumbnails });
  assert.equal(normalized.thumbnails.length, 1);
  assert.equal(normalized.models.length, 1);
  assert.deepEqual(normalized.missingItems, library.missingItems);
  assert.deepEqual(normalized.missingThumbnails, library.missingThumbnails);
});

test('缩略图沿产物与道具引用映射，采集源不会冒充库存实体', () => {
  const p = project();
  assert.equal(media.resolveVisual(p, { id: 'make_timber', product: 'timber' }, 'recipes', library).thumbnail.id, 'timber-v1');
  assert.equal(media.resolveVisual(p, p.props[0], 'props', library).model.id, 'prop:bench');
  const onlyGather = { ...library, models: [library.models[0]] };
  assert.equal(media.resolveVisual(p, p.items[0], 'items', onlyGather).model, null);
  assert.equal(media.resolveVisual(p, { id: 'tree', product: 'timber' }, 'scatters', onlyGather).model.id, 'scatter:tree');
  assert.equal(p.visuals, undefined, '默认预览不写入草稿');
  p.visuals = { items: { timber: { thumbnail: null } } };
  assert.equal(media.resolveVisual(p, { id: 'make_timber', product: 'timber' }, 'recipes', library).thumbnail, null, '配方跟随道具缩略图的显式绑定');
});

test('显式绑定保存局部变换、处理缺失资产，并能完全恢复默认', () => {
  const p = project(), row = p.items[0];
  media.writeVisual(p, row, 'items', library, 'scale', 2, 1);
  media.writeVisual(p, row, 'items', library, 'thumbnail', null);
  media.writeVisual(p, row, 'items', library, 'model', 'missing');
  const visual = media.resolveVisual(p, row, 'items', library);
  assert.deepEqual(visual.config.scale, [1, 2, 1]);
  assert.equal(visual.thumbnail, null);
  assert.equal(visual.missingModel, true);
  media.clearVisual(p, 'items', row.id);
  assert.equal(p.visuals, undefined);
});

test('ID编辑发生暂时碰撞时保留双方视觉配置，继续输入恢复本条绑定', () => {
  const p = project(), row = p.items[0];
  p.visuals = { items: { timber: { model: 'own' }, stone: { model: 'other' } } };
  row.id = 'stone'; media.renameVisual(p, row, 'items', 'timber');
  assert.deepEqual(p.visuals.items, { timber: { model: 'own' }, stone: { model: 'other' } });
  row.id = 'custom'; media.renameVisual(p, row, 'items', 'stone');
  assert.deepEqual(p.visuals.items, { custom: { model: 'own' }, stone: { model: 'other' } });
});

test('模型配置按XYZ角度和Y向上应用，非等比缩放保持正确法线与顶点色', () => {
  const transformed = transformPoint(modelMatrix({ scale: [2, 3, 4], rotation: [0, 90, 0], offset: [1, 2, 3] }), [1, 0, 0]);
  assert(Math.abs(transformed[0] - 1) < 1e-6);
  assert(Math.abs(transformed[1] - 2) < 1e-6);
  assert(Math.abs(transformed[2] - 1) < 1e-6);
  const mesh = { schemaVersion: 1, parts: [{ positions: [0,0,0, 1,0,0, 0,1,0], normals: [0,0,1, 0,0,1, 0,0,1], indices: [2,0,1], color: [.5,.5,.5], colors: [1,0,0,1, 0,1,0,.5, 0,0,1,.75] }] };
  const prepared = prepareMesh(mesh, { scale: [2, 3, 4], rotation: [0, 90, 0], offset: [1, 2, 3] });
  assert.equal(prepared.triangles, 1);
  assert.deepEqual(Array.from(prepared.vertices.slice(6, 10)), [0,0,1,.75], '根据索引保留蓝色顶点与透明度');
  assert(Math.abs(prepared.vertices[3] - 1) < 1e-6);
  assert.deepEqual(Array.from(prepared.lines.slice(6, 10)), [0,0,1,.75], '线框仍保留顶点色');
  assert.throws(() => prepareMesh({ schemaVersion: 1, parts: [{ ...mesh.parts[0], colors: [1,0,0,1] }] }), /顶点色无效/);
  assert.throws(() => prepareMesh({ schemaVersion: 1, parts: [{ ...mesh.parts[0], indices: [0,1,9] }] }), /索引无效/);
});

function frontend() {
  const handlers = new Map();
  const shell = new Map(['#app', '#toast', '#import-file'].map(selector => [selector, { innerHTML: '', textContent: '', className: '', addEventListener() {} }]));
  const context = vm.createContext({ structuredClone, validateProject, diffProjects, ROW_SHAPES, injectedMedia: media, injectedLibrary: library, initialWorkspace: { baseline, project: baseline, revision: 'initial' }, document: { querySelector: selector => shell.get(selector) ?? null, querySelectorAll: () => [], addEventListener: (name, handler) => handlers.set(name, handler) }, window: { addEventListener() {} }, setTimeout: () => 0, clearTimeout() {} });
  vm.runInContext(stateSource, context);
  const run = text => vm.runInContext(text, context);
  run('mediaToolkit=injectedMedia; assetLibrary=injectedLibrary; assetsReady=true; applyWorkspace(initialWorkspace)');
  return { run, read: text => structuredClone(run(text)), input: target => handlers.get('input')({ target }) };
}

test('实际前端用真实缩略图、显示模型来源并在用户修改后产生视觉diff', async () => {
  const f = frontend();
  f.run("navigate('items','timber'); state.tab='visual'");
  assert.match(f.run('art(selected())'), /<img src="\/assets\/thumbnails\/timber-v1.png" loading="lazy" alt="木料缩略图"/);
  assert.equal(f.read('project.visuals'), undefined);
  assert.equal(f.read('state.dirty'), false);
  f.input({ dataset: { visual: 'model' }, value: 'prop:bench' });
  assert.equal(f.read('state.dirty'), true);
  assert(f.read('changes').some(x => x.collection === 'visuals' && x.targetCollection === 'items' && x.targetId === 'timber'));
  const html = f.run('renderVisual(selected())');
  assert.match(html, /当前游戏模型/);
  assert.match(html, /场景采集源（不是库存道具实体）/);
  assert.match(html, /<canvas id="model-canvas"/);
  await f.run("performAction({dataset:{action:'visual-default'}})");
  assert.equal(f.read('project.visuals'), undefined);
  assert.equal(f.read('state.dirty'), false);
});

test('从全图库打开指定道具会切回编辑器，临时查看采集源不写入草稿', async () => {
  const f = frontend();
  f.run("state.itemView='gallery'; navigate('items','timber'); state.tab='visual'");
  assert.equal(f.read('state.itemView'), 'list');
  await f.run("performAction({dataset:{action:'model-peek',model:'scatter:tree'}})");
  assert.equal(f.read('state.modelPeek'), 'scatter:tree');
  assert.equal(f.read('state.dirty'), false);
  assert.equal(f.read('project.visuals'), undefined);
  assert.match(f.run('renderVisual(selected())'), /临时查看关联采集源/);
});

test('切换预览会取消绘制、删除GPU缓冲并释放所有监听器', () => {
  const calls = { deletedBuffers: [], deletedPrograms: [], lost: 0, removed: [], cancelled: [] };
  const canvas = { getContext: () => gl, addEventListener(name, callback) { this[name] = callback; }, removeEventListener(name) { calls.removed.push(name); }, clientWidth: 200, clientHeight: 200 };
  let bufferId = 0;
  const gl = {
    createShader: () => ({}), shaderSource() {}, compileShader() {}, getShaderParameter: () => true, deleteShader() {},
    createProgram: () => ({}), attachShader() {}, linkProgram() {}, getProgramParameter: () => true,
    getAttribLocation: (_, name) => ['aPosition', 'aNormal', 'aColor'].indexOf(name), getUniformLocation: () => ({}),
    createBuffer: () => ++bufferId, bindBuffer() {}, bufferData() {}, deleteBuffer: b => calls.deletedBuffers.push(b), deleteProgram: p => calls.deletedPrograms.push(p), getExtension: () => ({ loseContext: () => calls.lost++ }),
  };
  const previous = Object.fromEntries(['window', 'requestAnimationFrame', 'cancelAnimationFrame', 'ResizeObserver'].map(key => [key, globalThis[key]]));
  try {
    globalThis.window = { addEventListener() {}, removeEventListener(name) { calls.removed.push(name); } };
    globalThis.ResizeObserver = undefined;
    globalThis.requestAnimationFrame = () => 51;
    globalThis.cancelAnimationFrame = id => calls.cancelled.push(id);
    const viewer = new ModelViewer(canvas);
    viewer.mesh = { schemaVersion: 1, parts: [{ positions: [0,0,0, 1,0,0, 0,1,0], indices: [0,1,2] }] };
    viewer.setTransform({});
    viewer.dispose(); viewer.dispose();
    assert.equal(viewer.abort.signal.aborted, true);
    assert.deepEqual(calls.deletedBuffers, [1,2,3]);
    assert.equal(calls.deletedPrograms.length, 1);
    assert.equal(calls.lost, 1);
    assert.deepEqual(calls.cancelled, [51]);
    assert.equal(calls.removed.length, 7);
    assert.equal(viewer.mesh, null);
  } finally {
    for (const [key, value] of Object.entries(previous)) if (value === undefined) delete globalThis[key]; else globalThis[key] = value;
  }
});
