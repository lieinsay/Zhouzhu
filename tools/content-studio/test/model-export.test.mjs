import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { assertMesh, parseArguments } from '../model-export/export-models.mjs';
import { loadCatalog } from '../lib/catalog.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const selection=JSON.parse(await readFile(path.join(root,'assets/art/model-library.json'),'utf8'));
const directory=path.join(root,selection.snapshot);
const manifest = JSON.parse(await readFile(path.join(directory, 'manifest.json'), 'utf8'));
const sha = data => createHash('sha256').update(data).digest('hex');

test('模型来自真实游戏生成入口且覆盖既有摆件、搭建件、建筑及船模块', async () => {
  const catalog = await loadCatalog(root);
  const ids = new Set(manifest.models.map(model => model.id));
  assert.equal(ids.size, manifest.models.length, '模型 ID 不重复');
  for (const item of catalog.props) assert.ok(ids.has(`prop:${item.id}`), item.id);
  for (const item of catalog.pieces) assert.ok(ids.has(`piece:${item.id}`), item.id);
  for (const building of catalog.buildings) assert.ok(ids.has(`building:${building.id}`), building.id);
  for (const module of catalog.modules) assert.ok(ids.has(`module:${module.id}`), module.id);
  for (const scatter of catalog.scatters) assert.ok(ids.has(`scatter:${scatter.id}`), scatter.id);
  assert.deepEqual(manifest.failures, []);
  assert.equal(manifest.exporter.headless, true);
  for (const model of manifest.models) {
    assert.equal(model.kind, 'game');
    assert.equal(model.format, 'mesh-json');
    assert.ok(model.name && model.notes);
    assert.match(model.source.sha256, /^[0-9a-f]{64}$/);
  }
});

test('静态模型数组与资源指纹有效，保持真实米制及逆时针法线', async () => {
  let faces = 0;
  for (const model of manifest.models) {
    assert.match(model.url, /^\/assets\/models\/[a-z0-9_-]+\.json$/);
    const bytes = await readFile(path.join(directory, path.basename(model.url)));
    assert.equal(sha(bytes), model.assetSha256, model.id);
    const mesh = assertMesh(JSON.parse(bytes), model.id);
    assert.equal(mesh.units, 'm', model.id);
    assert.equal(mesh.winding, 'ccw', model.id);
    for (const part of mesh.parts) for (let i = 0; i < part.indices.length; i += 3) {
      const [a, b, c] = part.indices.slice(i, i + 3).map(index => index * 3);
      const u = [0, 1, 2].map(k => part.positions[b + k] - part.positions[a + k]);
      const v = [0, 1, 2].map(k => part.positions[c + k] - part.positions[a + k]);
      const cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
      if (cross.every(n => Math.abs(n) < 1e-10)) continue;
      const dot = cross.reduce((sum, n, k) => sum + n * (part.normals[a + k] + part.normals[b + k] + part.normals[c + k]), 0);
      assert.ok(dot >= -1e-6, `${model.id} 的三角 ${i / 3} 面向与法线矛盾`);
      faces++;
    }
  }
  assert.ok(faces > 1000);
});

test('采集源如实标明模型用途，并明确库存模型缺口', async () => {
  const catalog = await loadCatalog(root);
  const covered = new Set(manifest.models.flatMap(model => model.itemIds));
  const missing = new Set(manifest.missingItems.map(item => item.id));
  for (const item of catalog.items) assert.equal(covered.has(item.id) || missing.has(item.id), true, item.id);
  for (const source of manifest.models.filter(model => model.id.startsWith('scatter:'))) {
    assert.equal(source.usage, 'gather-source');
    assert.equal(source.collection, 'scatters', '采集源不可默认绑定为库存物品实体');
    assert.equal(source.recordIds.length, 1);
    assert.ok(catalog.scatters.some(scatter => scatter.id === source.recordIds[0]), source.id);
  }
  assert.ok(manifest.models.some(model => model.usage === 'gather-source' && model.itemIds.includes('reed')));
  assert.ok(manifest.models.some(model => model.usage === 'gather-source' && model.itemIds.includes('peat')));
  assert.ok(manifest.models.some(model => model.usage === 'gather-source' && model.itemIds.includes('guano')));
  for (const id of ['salt', 'coin', 'stone_axe', 'stone_pick', 'wood_shovel']) assert.ok(missing.has(id), id);
  assert.ok(manifest.missingItems.every(item => item.reason));
});

test('生成来源和只读游戏产物保持已核对的完整 SHA', async () => {
  for (const source of [...manifest.sources, ...manifest.exporter.gameFilesUnchanged]) {
    assert.equal(sha(await readFile(path.resolve(root, source.path))), source.sha256, source.path);
  }
});

test('独立导出命令接受根目录和输出目录，并拒绝不完整输入网格', () => {
  assert.equal(parseArguments(['--root', root, '--output', directory]).output, directory);
  assert.throws(() => parseArguments(['--output']), /缺少值/);
  assert.throws(() => assertMesh({ schemaVersion: 1, parts: [] }), /没有真实网格/);
  assert.throws(() => assertMesh({ schemaVersion: 1, parts: [{ positions: [0, 0, 0], normals: [0, 1, 0], indices: [0, 1, 2], color: [1, 1, 1] }] }), /索引越界/);
});
