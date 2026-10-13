import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, readFile, writeFile, copyFile, readdir, rm } from 'node:fs/promises';
import { resolve, dirname, relative, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { createHash } from 'node:crypto';
import { request } from 'node:http';
import { loadCatalog, parseInitializer, SOURCE_PATHS } from '../lib/catalog.mjs';
import { assertProjectStructure, validateProject, diffProjects } from '../lib/validation.mjs';
import { WorkspaceStore } from '../lib/store.mjs';
import { createStudioServer, makeCppProposal } from '../server.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const baseline = await loadCatalog(ROOT);
const clone = () => structuredClone(baseline);

async function fixture(context) {
  const directory = await mkdtemp(resolve(tmpdir(), 'zhouzhu-content-studio-test-'));
  context.after(async () => {
    const rel = relative(tmpdir(), directory);
    assert(!rel.startsWith('..') && !isAbsolute(rel) && rel.startsWith('zhouzhu-content-studio-test-'));
    await rm(directory, { recursive: true, force: true });
  });
  const repositoryRoot = resolve(directory, 'repository');
  const workspaceDirectory = resolve(directory, 'workspace');
  const publicDirectory = resolve(directory, 'public');
  await mkdir(resolve(repositoryRoot, 'src'), { recursive: true });
  await mkdir(publicDirectory);
  await writeFile(resolve(publicDirectory, 'index.html'), '<!doctype html><title>工坊测试</title>');
  await Promise.all(SOURCE_PATHS.map(path => copyFile(resolve(ROOT, path), resolve(repositoryRoot, path))));
  return { directory, repositoryRoot, workspaceDirectory, publicDirectory };
}
const sourceHashes = async root => Promise.all(SOURCE_PATHS.map(async path => [path, createHash('sha256').update(await readFile(resolve(root, path))).digest('hex')]));

test('真实源码每张表完整导入、行号指向原行、字段与头文件默认值一致', async () => {
  assert.deepEqual(Object.fromEntries(Object.entries(baseline).filter(([, value]) => Array.isArray(value)).map(([key, value]) => [key, value.length])), {
    items: 49, recipes: 24, props: 5, pieces: 11, buildings: 5, modules: 8, deposits: 16, scatters: 11, tools: 3, crops: 2, stations: 3,
  });
  assert.deepEqual(validateProject(baseline), []);
  const house = baseline.buildings.find(row => row.id === 'house');
  assert.equal(house.levels, 1); assert.equal(house.house, true);
  assert.equal(baseline.buildings.find(row => row.id === 'tower').levels, 3);
  assert.equal(baseline.buildings.find(row => row.id === 'berth').house, false);
  assert.equal(baseline.modules.find(row => row.id === 'wide_deck').size_x, 2);
  assert.equal(baseline.deposits.find(row => row.id === 'spring').product, '');
  assert.equal(baseline.scatters.find(row => row.id === 'broadleaf').product, 'timber');
  const files = new Map(await Promise.all(SOURCE_PATHS.map(async path => [path, (await readFile(resolve(ROOT, path), 'utf8')).split('\n')])));
  for (const collection of Object.values(baseline).filter(Array.isArray)) for (const row of collection) if (row.source) assert(files.get(row.source.path)[row.source.line - 1].includes(`"${row.id}"`), `${row.id} source line`);
  assert.deepEqual(baseline.meta.sources.map(s => [s.path, s.sha256]), await sourceHashes(ROOT));
  assert.equal(baseline.meta.ores.length, 8);
});

test('初始化器不静默跳过不支持的宏/表达式/字段变化', async context => {
  assert.throws(() => parseInitializer('const ItemDef ITEMS[] = { { MACRO(1) } };', 'ITEMS'), /尚不支持/);
  assert.throws(() => parseInitializer('const ItemDef ITEMS[] = { { 1 + 2 } };', 'ITEMS'), /缺少逗号/);
  assert.throws(() => parseInitializer('const ItemDef ITEMS[] = { { "bad\\q" } };', 'ITEMS'), /尚不支持字符串转义/);
  const f = await fixture(context);
  const path = resolve(f.repositoryRoot, 'src/crops.cpp');
  await writeFile(path, (await readFile(path, 'utf8')).replace('"millet", "粟", 64,', '"millet", "粟", 64, 99,'));
  await assert.rejects(loadCatalog(f.repositoryRoot), /src\/crops.cpp:8.*字段数错误/);
});

test('校验真实配方约束、重复ID/材料、未知引用、删除被引用及数值边界', () => {
  const project = clone();
  project.items.push(structuredClone(project.items[0]));
  project.items.find(row => row.id === 'stone').stack = 0;
  project.items = project.items.filter(row => row.id !== 'plank');
  project.recipes[0].yield = -1;
  project.recipes[0].ingredients = [{ item: 'timber', count: 0 }, { item: 'timber', count: 1 }, { item: 'missing', count: 1 }, { item: 'stone', count: 1 }];
  const issues = validateProject(project, baseline);
  assert(issues.some(row => row.collection === 'items' && row.message.includes('重复')));
  assert(issues.some(row => row.field === 'stack'));
  assert(issues.some(row => row.message.includes('plank') && row.message.includes('不存在')));
  assert(issues.some(row => row.message.includes('1～3')));
  assert(issues.some(row => row.message.includes('材料') && row.message.includes('重复')));
  assert(issues.some(row => row.field.includes('.count')));
  assert(issues.some(row => row.message.includes('missing')));
});

test('配方闭环无法自举，但替代配方能解除环；工作台必须从随身链获取', () => {
  const project = clone();
  project.items.push({ id: 'cycle_a', name: '甲', stack: 64, color: [0.1, 0.2, 0.3] }, { id: 'cycle_b', name: '乙', stack: 64, color: [0.2, 0.3, 0.4] });
  project.recipes.push({ id: 'cycle_a', product: 'cycle_a', yield: 1, station: 'HAND', ingredients: [{ item: 'cycle_b', count: 1 }] }, { id: 'cycle_b', product: 'cycle_b', yield: 1, station: 'HAND', ingredients: [{ item: 'cycle_a', count: 1 }] });
  assert.equal(validateProject(project, baseline).filter(row => row.id.startsWith('cycle_') && row.severity === 'error').length, 2);
  project.recipes.push({ id: 'cycle_a_alt', product: 'cycle_a', yield: 1, station: 'HAND', ingredients: [{ item: 'timber', count: 1 }] });
  assert.equal(validateProject(project, baseline).filter(row => row.id.startsWith('cycle_') && row.severity === 'error').length, 0);
  project.recipes.find(row => row.id === 'workbench').station = 'WORKBENCH';
  assert(validateProject(project, baseline).some(row => row.id === 'workbench' && row.message.includes('自举')));
  const self = clone(); self.recipes[0].ingredients = [{ item: 'plank', count: 1 }];
  assert(validateProject(self, baseline).some(row => row.id === 'plank' && row.message.includes('自己的产物')));
});

test('新道具缺少获取来源提示警告、可由新增采集入口接通制作链', () => {
  const project = clone();
  project.items.push({ id: 'new_ore', name: '试验矿', stack: 64, color: [0.1, 0.2, 0.3] });
  assert(validateProject(project, baseline).some(row => row.id === 'new_ore' && row.severity === 'warning'));
  project.scatters.push({ ...structuredClone(project.scatters[0]), id: 'new_ore_scatter', key: 'new_ore_scatter', product: 'new_ore' });
  assert(!validateProject(project, baseline).some(row => row.id === 'new_ore'));
  assert(validateProject(project, baseline).some(row => row.id === 'new_ore_scatter' && row.message.includes('提供实例')));
});

test('变更报告忽略来源元数据，准确显示新增、修改、删除', () => {
  const project = clone();
  project.items[0].source.line += 4;
  assert.deepEqual(diffProjects(baseline, project), []);
  project.items[0].name = '粟测试';
  project.recipes.pop();
  project.items.push({ id: 'test_item', name: '试验', stack: 64, color: [0, 0, 0] });
  assert.deepEqual(diffProjects(baseline, project).map(row => [row.collection, row.id, row.type]), [['items', 'millet', 'modified'], ['items', 'test_item', 'added'], ['recipes', 'ladder', 'deleted']]);
});

test('模块/散布枚举索引的顺序、重复和缺项会明确报错', () => {
  const project = clone();
  [project.modules[0], project.modules[1]] = [project.modules[1], project.modules[0]];
  project.scatters[1].look = project.scatters[0].look;
  const issues = validateProject(project, baseline);
  assert(issues.some(row => row.collection === 'modules' && row.message.includes('下标')));
  assert(issues.some(row => row.collection === 'scatters' && row.message.includes('重复')));
  assert(issues.some(row => row.collection === 'scatters' && row.message.includes('CONIFER') && row.message.includes('缺少')));
});

test('草稿原子保存、重启恢复、旧revision冲突，语义错误可保存、结构错误拒绝', async context => {
  const f = await fixture(context);
  const original = await sourceHashes(f.repositoryRoot);
  const store = await new WorkspaceStore(f).init();
  const first = await store.getWorkspace();
  first.project.items[0].name = '草稿粟';
  first.project.recipes[0].yield = 0;
  const saved = await store.saveProject(first.project, first.revision);
  assert.notEqual(saved.revision, first.revision);
  assert(saved.issues.some(row => row.field === 'yield'));
  assert(saved.changes.some(row => row.collection === 'items' && row.id === 'millet'));
  await assert.rejects(store.saveProject(first.project, first.revision), error => error.status === 409);
  const restarted = await new WorkspaceStore(f).init();
  const restored = await restarted.getWorkspace();
  assert.equal(restored.project.items[0].name, '草稿粟');
  assert.equal(restored.revision, saved.revision);
  restored.project.items[0].stack = 'bad';
  await assert.rejects(restarted.saveProject(restored.project, restored.revision), error => error.status === 422);
  const files = await readdir(f.workspaceDirectory);
  assert.deepEqual(files, ['draft.json']);
  assert.deepEqual(await sourceHashes(f.repositoryRoot), original);
});

test('已有草稿检测运行中源码漂移、拒绝继续写入，重置备份后建立当前基线', async context => {
  const f = await fixture(context);
  const store = await new WorkspaceStore(f).init();
  const first = await store.getWorkspace();
  first.project.items[0].name = '保留提案';
  const saved = await store.saveProject(first.project, first.revision);
  const source = resolve(f.repositoryRoot, 'src/crops.cpp');
  await writeFile(source, (await readFile(source, 'utf8')).replace('"millet", "粟", 64', '"millet", "粟新基线", 64'));
  const drift = await store.getWorkspace();
  assert.equal(drift.project.items[0].name, '保留提案');
  assert.equal(drift.baseline.items[0].name, '粟新基线');
  assert(drift.issues.some(row => row.field === 'sources' && row.severity === 'warning'));
  await assert.rejects(store.saveProject(saved.project, saved.revision), error => error.status === 409 && error.message.includes('指纹'));
  const reset = await store.resetProject(drift.revision);
  assert.deepEqual(reset.changes, []); assert.deepEqual(reset.issues, []);
  assert.equal(reset.project.items[0].name, '粟新基线');
  const backup = (await readdir(f.workspaceDirectory)).find(path => path.startsWith('draft.backup.'));
  assert(backup);
  assert.equal(JSON.parse(await readFile(resolve(f.workspaceDirectory, backup), 'utf8')).project.items[0].name, '保留提案');
});

test('损坏草稿不能静默丢弃重建', async context => {
  const f = await fixture(context);
  await mkdir(f.workspaceDirectory);
  await writeFile(resolve(f.workspaceDirectory, 'draft.json'), '{"bad":true}');
  await assert.rejects(new WorkspaceStore(f).init(), /已保留原文件/);
  assert.equal(await readFile(resolve(f.workspaceDirectory, 'draft.json'), 'utf8'), '{"bad":true}');
});

test('HTTP导入导出/差异/建议与只读静态边界、同源写入、冲突及大小限制', async context => {
  const f = await fixture(context);
  const original = await sourceHashes(f.repositoryRoot);
  const studio = await createStudioServer(f);
  const url = await studio.listen(0);
  context.after(() => new Promise(resolveClose => studio.server.close(resolveClose)));
  const first = await (await fetch(`${url}/api/workspace`)).json();
  assert.equal(first.baseline.items.length, 49);
  const exported = await fetch(`${url}/api/export`);
  assert(exported.headers.get('content-disposition').includes('attachment'));
  assert.deepEqual(await exported.json(), first.project);
  first.project.items[0].name = '网络草稿粟';
  const post = data => fetch(`${url}/api/import`, { method: 'POST', headers: { 'Content-Type': 'application/json', Origin: url }, body: JSON.stringify(data) });
  const savedResponse = await post({ project: first.project, revision: first.revision });
  assert.equal(savedResponse.status, 200);
  const saved = await savedResponse.json();
  assert.equal((await post({ project: first.project, revision: first.revision })).status, 409);
  const report = await (await fetch(`${url}/api/report`)).json();
  assert.equal(report.changes[0].id, 'millet');
  const cpp = await (await fetch(`${url}/api/patch`)).text();
  assert(cpp.includes('网络草稿粟')); assert(cpp.includes('const RecipeDef RECIPES[]'));
  assert.equal((await fetch(`${url}/lib/validation.mjs`)).status, 200);
  assert.equal((await fetch(`${url}/lib/catalog.mjs`)).status, 404);
  assert.equal((await fetch(`${url}/workspace/draft.json`)).status, 404);
  assert.equal((await fetch(`${url}/%2e%2e%2fserver.mjs`)).status, 403);
  const invalidHost = await new Promise((resolveResponse, reject) => {
    const req = request(`${url}/`, { headers: { Host: 'evil.example' } }, res => { res.resume(); resolveResponse(res.statusCode); });
    req.on('error', reject); req.end();
  });
  assert.equal(invalidHost, 403);
  assert.equal((await fetch(`${url}/api/workspace`, { method: 'POST', headers: { 'Content-Type': 'application/json', Origin: 'https://evil.example' }, body: JSON.stringify({ project: saved.project, revision: saved.revision }) })).status, 403);
  assert.equal((await fetch(`${url}/api/workspace`, { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: '{}' })).status, 415);
  assert.equal((await fetch(`${url}/api/workspace`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: 'x'.repeat(5 * 1024 * 1024 + 1) })).status, 413);
  const resetResponse = await fetch(`${url}/api/reset`, { method: 'POST', headers: { 'Content-Type': 'application/json', Origin: url }, body: JSON.stringify({ revision: saved.revision }) });
  assert.equal(resetResponse.status, 200);
  assert.equal((await resetResponse.json()).changes.length, 0);
  assert.deepEqual(await sourceHashes(f.repositoryRoot), original);
});

test('C++建议禁止导出错误配方，并保持中文及字符串转义', () => {
  const project = clone();
  project.items[0].name = '粟"测试\\一';
  const cpp = makeCppProposal(project, baseline);
  assert(cpp.includes('粟\\"测试\\\\一'));
  project.recipes[0].ingredients.push({ item: 'plank', count: 1 });
  assert.throws(() => makeCppProposal(project, baseline), error => error.status === 422);
  assert.throws(() => assertProjectStructure({ ...project, schemaVersion: 2 }), /schemaVersion/);
});
