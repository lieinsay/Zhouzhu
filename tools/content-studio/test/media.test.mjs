import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, readFile, writeFile, copyFile, rm } from 'node:fs/promises';
import { resolve, dirname, relative, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { createHash } from 'node:crypto';
import { request } from 'node:http';
import { loadCatalog, SOURCE_PATHS } from '../lib/catalog.mjs';
import { assertProjectStructure, validateProject, validateVisuals, diffProjects } from '../lib/validation.mjs';
import { MediaLibrary, parseObj } from '../lib/media.mjs';
import { WorkspaceStore } from '../lib/store.mjs';
import { createStudioServer, makeCppProposal } from '../server.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const baseline = await loadCatalog(ROOT);
const triangle = 'v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n';
const visual = { thumbnail: 'timber', model: 'prop:workbench', scale: [1, 1, 1], rotation: [0, 90, 0], offset: [0, 0.2, 0] };

async function fixture(context) {
  const directory = await mkdtemp(resolve(tmpdir(), 'zhouzhu-media-test-'));
  context.after(async () => {
    const rel = relative(tmpdir(), directory);
    assert(!rel.startsWith('..') && !isAbsolute(rel) && rel.startsWith('zhouzhu-media-test-'));
    await rm(directory, { recursive: true, force: true });
  });
  const repositoryRoot = resolve(directory, 'repository');
  const workspaceDirectory = resolve(directory, 'workspace');
  const publicDirectory = resolve(directory, 'public');
  await mkdir(resolve(repositoryRoot, 'src'), { recursive: true });
  await mkdir(resolve(publicDirectory, 'assets/thumbnails'), { recursive: true });
  await mkdir(resolve(publicDirectory, 'assets/models'), { recursive: true });
  await Promise.all(SOURCE_PATHS.map(path => copyFile(resolve(ROOT, path), resolve(repositoryRoot, path))));
  await writeFile(resolve(publicDirectory, 'index.html'), '<!doctype html><title>媒体测试</title>');
  await writeFile(resolve(publicDirectory, 'assets/thumbnails/timber-v1.png'), Buffer.from([137, 80, 78, 71]));
  await writeFile(resolve(publicDirectory, 'assets/models/workbench.json'), JSON.stringify({ schemaVersion: 1, parts: [] }));
  await writeFile(resolve(publicDirectory, 'assets/models/manifest.json'), JSON.stringify({ models: [{ id: 'prop:workbench', name: '工作台', url: '/assets/models/workbench.json', format: 'mesh-json', kind: 'game', usage: 'placed', itemIds: ['workbench'], collection: 'props', recordIds: ['workbench'], source: { path: 'src/props.cpp', sha256: 'a'.repeat(64) }, notes: [] }], missingItems: [{ id: 'timber', reason: '没有独立携带实体' }] }));
  return { directory, repositoryRoot, workspaceDirectory, publicDirectory };
}
function call(url, method = 'GET', data, headers = {}) {
  return new Promise((resolveResult, reject) => {
    const target = new URL(url);
    const body = data === undefined ? null : JSON.stringify(data);
    const req = request(target, { method, headers: { ...(body ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(body) } : {}), ...headers } }, res => {
      const chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        let value; try { value = JSON.parse(text); } catch { value = text; }
        resolveResult({ status: res.statusCode, value, headers: res.headers });
      });
    });
    req.on('error', reject); req.end(body);
  });
}

test('旧 v1 草稿兼容，可选视觉映射只进入 JSON/变更报告，不进入 C++ 片段', () => {
  assert.doesNotThrow(() => assertProjectStructure(baseline));
  assert.deepEqual(validateVisuals(baseline), []);
  const project = structuredClone(baseline);
  project.visuals = { items: { timber: structuredClone(visual) } };
  assert.doesNotThrow(() => assertProjectStructure(project));
  assert.deepEqual(validateProject(project, baseline, { thumbnails: [{ id: 'timber' }], models: [{ id: 'prop:workbench' }] }), []);
  const [change] = diffProjects(baseline, project);
  assert.equal(change.collection, 'visuals'); assert.equal(change.id, 'items.timber'); assert.equal(change.targetCollection, 'items'); assert.equal(change.targetId, 'timber'); assert.equal(change.type, 'added');
  assert.deepEqual(change.after, visual);
  assert.equal(makeCppProposal(project, baseline), makeCppProposal(baseline, baseline));
  const modified = structuredClone(project); modified.visuals.items.timber.rotation[1] = 180;
  assert.equal(diffProjects(project, modified)[0].type, 'modified');
  assert.equal(diffProjects(project, baseline)[0].type, 'deleted');
});

test('视觉结构及目标/媒体引用明确校验，URL/路径不会作为可用资源引用', () => {
  for (const value of [{ items: { timber: { model: 'https://other.example/model.obj' } } }, { recipes: {} }, { items: { timber: { scale: [0, 1, 1] } } }, { items: { timber: { rotation: [NaN, 0, 0] } } }, { items: { timber: { offset: [1, 2] } } }, { items: { timber: { unknown: true } } }]) {
    assert.throws(() => assertProjectStructure({ ...baseline, visuals: value }), /visuals/);
  }
  const project = { ...baseline, visuals: { items: { missing: { thumbnail: 'unknown_image', model: 'unknown_model' } } } };
  const issues = validateVisuals(project, { thumbnails: [], models: [] });
  assert.equal(issues.length, 3);
  assert(issues.some(row => row.field === 'visuals.id'));
  assert(issues.some(row => row.field === 'visuals.thumbnail'));
  assert(issues.some(row => row.field === 'visuals.model'));
});

test('OBJ 解析三角/多边形、负索引和显式法线，拒绝非有限坐标、索引越界及无效几何', () => {
  const mesh = parseObj('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvn 0 0 2\nf -4/1/1 -3/1/1 -2/1/1 -1/1/1\nmtllib ../../never-open.mtl\n');
  assert.equal(mesh.schemaVersion, 1); assert.equal(mesh.units, 'm'); assert.equal(mesh.winding, 'ccw'); assert.equal(mesh.triangleCount, 2);
  assert.equal(mesh.parts[0].positions.length, 18); assert.deepEqual(mesh.parts[0].normals.slice(0, 3), [0, 0, 1]);
  assert.deepEqual(mesh.bounds, { min: [0, 0, 0], max: [1, 1, 0] });
  for (const text of ['v NaN 0 0\nf 1 1 1', 'v Infinity 0 0', 'v 0 0 0\nf 1 2 3', 'v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3', 'v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3', 'curv 0 1 1 2 3']) assert.throws(() => parseObj(text), /OBJ/);
  assert.throws(() => parseObj('v 0 0 0\nv 2 0 0\nv 1 0.5 0\nv 2 1 0\nv 0 1 0\nf 1 2 3 4 5'), /凹面.*三角化/);
  assert.throws(() => parseObj('v 0 0 0\nv 1 0 0\nv 1 1 1\nv 0 1 0\nf 1 2 3 4'), /不共面.*三角化/);
  assert.throws(() => parseObj('v 0 0 0\nv 1 1 0\nv 0 1 0\nv 1 0 0\nf 1 2 3 4'), /凹面或自交.*三角化/);
});

test('媒体库按磁盘有效缩略图读取，并保留游戏模型的真实来源/用途', async context => {
  const f = await fixture(context);
  const media = new MediaLibrary(f);
  let assets = await media.getAssets(baseline);
  assert.equal(assets.thumbnails.length, 1); assert.equal(assets.thumbnails[0].id, 'timber'); assert.equal(assets.thumbnails[0].origin, 'imagegen');
  assert.equal(assets.missingThumbnails.length, baseline.items.length - 1); assert.equal(assets.missingItems[0].reason, '没有独立携带实体');
  assert.equal(assets.models[0].collection, 'props'); assert.equal(assets.models[0].source.path, 'src/props.cpp');
  await writeFile(resolve(f.publicDirectory, 'assets/thumbnails/manifest.json'), JSON.stringify({ thumbnails: [{ id: 'timber', itemId: 'timber', name: '木材', url: '/assets/thumbnails/timber-v1.png', width: 1280, height: 1280, sha256: 'b'.repeat(64), origin: 'imagegen' }, { id: 'stone', itemId: 'stone', filename: 'stone-v1.png' }] }));
  assets = await media.getAssets(baseline);
  assert.equal(assets.thumbnails.length, 1); assert.equal(assets.thumbnails[0].width, 1280);
  await writeFile(resolve(f.publicDirectory, 'assets/thumbnails/manifest.json'), JSON.stringify({ thumbnails: [{ id: 'timber', itemId: 'timber', filename: '../secret.png' }] }));
  await assert.rejects(media.getAssets(baseline), /库内 PNG/);
});

test('视觉配置保存/导入保留已有版本冲突保护与来源校验，重新启动仍能读回', async context => {
  const f = await fixture(context);
  const media = new MediaLibrary(f);
  const store = await new WorkspaceStore({ ...f, mediaLibrary: media }).init();
  const start = await store.getWorkspace();
  const project = structuredClone(start.project); project.visuals = { items: { timber: structuredClone(visual) } };
  const saved = await store.saveProject(project, start.revision);
  assert.deepEqual(saved.project.visuals, project.visuals); assert.deepEqual(saved.issues, []);
  await assert.rejects(store.saveProject(project, start.revision), error => error.status === 409);
  const restarted = await new WorkspaceStore({ ...f, mediaLibrary: new MediaLibrary(f) }).init();
  assert.deepEqual((await restarted.getWorkspace()).project.visuals, project.visuals);
  const wrongSource = structuredClone(project); wrongSource.meta.sources[0].sha256 = '0'.repeat(64);
  await assert.rejects(restarted.saveProject(wrongSource, saved.revision), error => error.status === 409);
  const invalid = structuredClone(project); invalid.visuals.items.timber.model = '../outside.obj';
  await assert.rejects(restarted.saveProject(invalid, saved.revision), error => error.status === 422);
});

test('OBJ HTTP 上传、绑定及重启持久化，静态PNG缓存且同源/工作区读取边界保持', async context => {
  const f = await fixture(context);
  const before = await Promise.all(SOURCE_PATHS.map(async path => createHash('sha256').update(await readFile(resolve(f.repositoryRoot, path))).digest('hex')));
  const studio = await createStudioServer(f);
  const url = await studio.listen(0);
  context.after(() => new Promise(resolveClose => studio.server.close(resolveClose)));
  const assets = await call(`${url}/api/assets`);
  assert.equal(assets.status, 200); assert.equal(assets.value.thumbnails.length, 1);
  const png = await call(`${url}/assets/thumbnails/timber-v1.png`);
  assert.equal(png.status, 200); assert.equal(png.headers['cache-control'], 'private, max-age=3600');
  assert.equal((await call(`${url}/assets/models/workbench.json`)).headers['cache-control'], 'no-store');
  const rejected = await call(`${url}/api/models`, 'POST', { name: '模型', filename: 'test.obj', content: triangle }, { origin: 'https://other.example' });
  assert.equal(rejected.status, 403);
  const uploaded = await call(`${url}/api/models`, 'POST', { name: '小模型', filename: 'triangle.obj', content: triangle }, { origin: url });
  assert.equal(uploaded.status, 201); assert.equal(uploaded.value.model.kind, 'imported'); assert.equal(uploaded.value.model.triangleCount, 1);
  const model = uploaded.value.model;
  const geometry = await call(`${url}${model.url}`);
  assert.equal(geometry.status, 200); assert.equal(geometry.value.parts[0].indices.length, 3);
  const persisted = new MediaLibrary(f);
  assert.equal((await persisted.getAssets(baseline)).models.length, 2);
  assert.equal((await persisted.readModel(model.id)).name, '小模型');
  const workspace = (await call(`${url}/api/workspace`)).value;
  workspace.project.visuals = { items: { timber: { ...visual, model: model.id } } };
  const saved = await call(`${url}/api/import`, 'POST', { project: workspace.project, revision: workspace.revision });
  assert.equal(saved.status, 200); assert.deepEqual(saved.value.issues, []);
  const exported = await call(`${url}/api/export`); assert.equal(exported.value.visuals.items.timber.model, model.id);
  assert.equal((await call(`${url}/api/models/not-a-uuid`)).status, 404);
  assert.equal((await call(`${url}/workspace/assets/${model.id}.json`)).status, 404);
  assert.equal((await call(`${url}/api/models/%2e%2e%2fsecret`)).status, 403);
  assert.equal((await call(`${url}/api/models`, 'POST', { name: '模型', filename: '../secret.obj', content: triangle })).status, 400);
  assert.equal((await call(`${url}/api/models`, 'POST', { name: '模型', filename: 'bad.obj', content: 'v bad 0 0' })).status, 422);
  const after = await Promise.all(SOURCE_PATHS.map(async path => createHash('sha256').update(await readFile(resolve(f.repositoryRoot, path))).digest('hex')));
  assert.deepEqual(after, before);
});
