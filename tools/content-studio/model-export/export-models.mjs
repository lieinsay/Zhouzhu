import { readFile, writeFile, mkdir, copyFile, readdir, stat, unlink } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { coreExportBuildSpec, CORE_SOURCES } from './build-core-export.mjs';
import { loadCatalog } from '../lib/catalog.mjs';

const TOOL_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const BRIDGE_SOURCES = ['src/placed_props.cpp', 'src/scatter_meshes.cpp', 'src/placeables.cpp', 'src/item_slots.cpp', 'src/crops.cpp', 'src/deposit_kinds.cpp', 'src/art_mesh.cpp', 'src/curved_material.cpp', 'src/art_surface.cpp','src/shared_looks.cpp','src/model_art.cpp',...CORE_SOURCES];
const BRIDGE_HEADERS = ['src/placed_props.h', 'src/scatter_meshes.h', 'src/placeables.h', 'src/item_slots.h', 'src/crops.h', 'src/deposit_kinds.h','src/art_mesh.h','src/art_surface.h','src/curved_material.h','src/art_mesh_kit.h','src/model_art.h','src/shared_looks.h'];
const GAME_DLL = 'game/bin/zhouzhu.windows.template_debug.x86_64.dll';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');

export function parseArguments(args) {
  const options = { root: path.resolve(TOOL_DIRECTORY, '../../..'), output: null, godot: null };
  for (let i = 0; i < args.length; i++) {
    const key = args[i];
    if (!['--root', '--output', '--godot'].includes(key) || !args[i + 1]) throw new Error(`未知参数或缺少值：${key}`);
    options[key.slice(2)] = path.resolve(args[++i]);
  }
  options.output ||= path.join(options.root, 'tools/content-studio/public/assets/models');
  return options;
}

export function assertMesh(mesh, id = 'mesh') {
  if (mesh.schemaVersion !== 1 || !Array.isArray(mesh.parts) || !mesh.parts.length) throw new Error(`${id}：没有真实网格`);
  for (const part of mesh.parts) {
    if (!Array.isArray(part.positions) || !part.positions.length || part.positions.length % 3 || !part.positions.every(Number.isFinite)) throw new Error(`${id}：位置数组不完整`);
    if (!Array.isArray(part.normals) || part.normals.length !== part.positions.length || !part.normals.every(Number.isFinite)) throw new Error(`${id}：法线数组不完整`);
    if (!Array.isArray(part.indices) || !part.indices.length || part.indices.length % 3 || !part.indices.every(v => Number.isInteger(v) && v >= 0 && v < part.positions.length / 3)) throw new Error(`${id}：三角索引越界`);
    if (!Array.isArray(part.color) || part.color.length !== 3 || !part.color.every(Number.isFinite)) throw new Error(`${id}：缺少真实材质颜色`);
  }
  return mesh;
}

export function runProcess(command, args, { cwd, timeout = 120000, env = process.env } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '', timedOut = false;
    child.stdout.on('data', b => { stdout += b.toString('utf8'); });
    child.stderr.on('data', b => { stderr += b.toString('utf8'); });
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeout);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('close', (code, signal) => { clearTimeout(timer); resolve({ code, signal, timedOut, stdout, stderr, command, args }); });
  });
}

async function fingerprint(root, inputs) {
  return Promise.all(inputs.map(async source => ({ path: source, sha256: sha(await readFile(path.resolve(root, source))) })));
}

function quoteResponse(value) {
  if (/[\r\n"]/.test(value)) throw new Error('编译路径包含不支持的字符');
  return `"${value}"`;
}

async function compile(root, buildDir) {
  const cache = await readFile(path.join(root, 'cmake-build-debug/CMakeCache.txt'), 'utf8');
  const compiler = /^CMAKE_CXX_COMPILER:FILEPATH=(.+)$/m.exec(cache)?.[1].trim();
  if (!compiler?.replaceAll('\\', '/').endsWith('/Hostx64/x64/cl.exe') || !/^CMAKE_GENERATOR:INTERNAL=Ninja\r?$/m.test(cache) || !/^CMAKE_BUILD_TYPE:STRING=Debug\r?$/m.test(cache)) throw new Error('当前 Rider 缓存不是已约定的 MSVC x64 / Ninja / Debug');
  const normalized = compiler.replaceAll('\\', '/');
  const marker = normalized.indexOf('/VC/Tools/MSVC/');
  const visualStudio = normalized.slice(0, marker);
  const version = /\/VC\/Tools\/MSVC\/([^/]+)\//.exec(normalized)?.[1];
  const vcvars = path.join(visualStudio, 'VC/Auxiliary/Build/vcvars64.bat');
  if (marker < 0 || /[\r\n"%&|<>^]/.test(vcvars)) throw new Error('不能安全解析当前 MSVC 环境入口');
  await stat(vcvars);
  await mkdir(buildDir, { recursive: true });
  const core = coreExportBuildSpec(root, buildDir);
  const godotLibrary = path.join(root, 'cmake-build-debug/bin/libgodot-cpp.windows.template_debug.x86_64.lib');
  await stat(godotLibrary);
  const bridgeOutput = path.join(buildDir, 'content-studio-models.dll');
  const bridgeArgs = [
    '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/MD', '/Zi', '/Od', '/W3', '/LD',
    '/DGDEXTENSION', '/DDEBUG_ENABLED', '/DHOT_RELOAD_ENABLED', '/DNOMINMAX', '/DTHREADS_ENABLED', '/DTYPED_METHOD_BIND', '/DWINDOWS_ENABLED', '/D_HAS_EXCEPTIONS=0',
    ...['src', 'extern/godot-cpp/include', 'cmake-build-debug/extern/godot-cpp/gen/include', 'extern/godot-cpp/gdextension'].map(p => `/I${path.join(root, p)}`),
    `/Fe:${bridgeOutput}`, `/Fd:${path.join(buildDir, 'content-studio-models.pdb')}`, path.join(TOOL_DIRECTORY, 'godot-bridge.cpp'), ...BRIDGE_SOURCES.map(p => path.join(root, p)),
    godotLibrary,
  ];
  const compileInputs = [...new Set([...core.inputs, ...BRIDGE_SOURCES, ...BRIDGE_HEADERS, 'src/recipes.h', 'tools/content-studio/model-export/godot-bridge.cpp', 'cmake-build-debug/bin/libgodot-cpp.windows.template_debug.x86_64.lib'])];
  const compileSources = await fingerprint(root, compileInputs);
  const stamp = { compiler, coreArgs: core.args, bridgeArgs, sources: compileSources };
  try {
    const previous = JSON.parse(await readFile(path.join(buildDir, 'build-stamp.json'), 'utf8'));
    if (JSON.stringify(previous) === JSON.stringify(stamp)) {
      await stat(core.output); await stat(bridgeOutput);
      return { core, bridgeOutput, compiler, language: 'cached-current-inputs' };
    }
  } catch { /* First build or changed tool inputs: compile independently below. */ }
  await writeFile(path.join(buildDir, 'core.rsp'), core.args.map(quoteResponse).join('\r\n'));
  await writeFile(path.join(buildDir, 'bridge.rsp'), bridgeArgs.map(quoteResponse).join('\r\n'));
  const rules = await readFile(path.join(root, 'cmake-build-debug/CMakeFiles/rules.ninja'), 'utf8');
  const language = /msvc_deps_prefix = Note: including file:/.test(rules) ? '1033' : /msvc_deps_prefix = 注意: 包含文件:/.test(rules) ? '2052' : null;
  if (!language) throw new Error('无法核对 Ninja 实际 MSVC 包含前缀');
  await writeFile(path.join(buildDir, 'compile.cmd'), [
    '@echo off', 'chcp 65001 >nul', `call "${vcvars}" -vcvars_ver=${version} > environment.log 2>&1`, 'if errorlevel 1 exit /b %errorlevel%',
    `set "VSLANG=${language}"`, 'cl.exe @core.rsp > core-build.log 2>&1', 'if errorlevel 1 exit /b %errorlevel%',
    'cl.exe @bridge.rsp > bridge-build.log 2>&1', 'exit /b %errorlevel%',
  ].join('\r\n'));
  const result = await runProcess(process.env.ComSpec || 'C:\\Windows\\System32\\cmd.exe', ['/d', '/c', 'compile.cmd'], { cwd: buildDir, timeout: 180000 });
  await writeFile(path.join(buildDir, 'compile-result.json'), JSON.stringify(result, null, 2));
  if (result.code !== 0 || result.timedOut) throw new Error(`独立工具编译失败，退出码 ${result.code}；查看 ${buildDir} 下的 *-build.log`);
  await stat(core.output); await stat(bridgeOutput);
  if (JSON.stringify(compileSources) !== JSON.stringify(await fingerprint(root, compileInputs))) throw new Error('独立工具编译期间输入已变化，不能复用该产物');
  await writeFile(path.join(buildDir, 'build-stamp.json'), JSON.stringify(stamp));
  return { core, bridgeOutput, compiler, language };
}

async function createRuntime(root, runtime, bridgeDll) {
  await mkdir(path.join(runtime, '.godot'), { recursive: true });
  await mkdir(path.join(runtime, 'shaders'), { recursive: true });
  // No autoloads, world data, game resource writes, or save filenames exist here.
  await writeFile(path.join(runtime, 'project.godot'), 'config_version=5\n[application]\nconfig/name="Content Studio Model Export"\nconfig/use_custom_user_dir=true\nconfig/custom_user_dir_name="ZhouzhuContentStudioModelExport"\n[debug]\nfile_logging/enable_file_logging=false\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n');
  await copyFile(path.join(root, 'game/ship.tscn'), path.join(runtime, 'ship.tscn'));
  const extension = (symbol, dll) => `[configuration]\nentry_symbol="${symbol}"\ncompatibility_minimum="4.4"\nreloadable=false\n[libraries]\nwindows.debug.x86_64="${dll.replaceAll('\\', '/')}"\n`;
  await writeFile(path.join(runtime, 'studio.gdextension'), extension('content_studio_library_init', bridgeDll));
  await writeFile(path.join(runtime, 'game.gdextension'), extension('zhouzhu_library_init', path.join(root, GAME_DLL)));
  await writeFile(path.join(runtime, '.godot/extension_list.cfg'), 'res://studio.gdextension\nres://game.gdextension\n');
  for (const entry of await readdir(path.join(root, 'game/shaders'), { withFileTypes: true })) {
    if (entry.isFile() && /\.(gdshader|gdshaderinc)$/.test(entry.name)) await copyFile(path.join(root, 'game/shaders', entry.name), path.join(runtime, 'shaders', entry.name));
  }
}

export async function exportModels({ root, output, godot }) {
  root = path.resolve(root); output = path.resolve(output);
  const buildDir = path.join(TOOL_DIRECTORY, '.build');
  const runtime = path.join(TOOL_DIRECTORY, 'runtime');
  const shaderInputs = (await readdir(path.join(root, 'game/shaders'), { withFileTypes: true })).filter(entry => entry.isFile() && /\.(gdshader|gdshaderinc)$/.test(entry.name)).map(entry => `game/shaders/${entry.name}`);
  const sourceInputs = [...new Set([...coreExportBuildSpec(root, buildDir).inputs, ...BRIDGE_SOURCES, ...BRIDGE_HEADERS,
    'tools/content-studio/model-export/godot-bridge.cpp', 'tools/content-studio/model-export/export-godot.gd',
    'src/recipes.h', 'src/ship.cpp', 'src/ship.h', 'src/ship_layout.h', 'src/curved_material.cpp', 'src/curved_material.h',
    'game/ship.tscn', GAME_DLL, ...shaderInputs])];
  const sources = await fingerprint(root, sourceInputs);
  const before = await fingerprint(root, [GAME_DLL, 'game/project.godot', 'CMakeLists.txt', 'cmake-build-debug/CMakeCache.txt', 'cmake-build-debug/bin/libgodot-cpp.windows.template_debug.x86_64.lib']);
  const build = await compile(root, buildDir);
  await mkdir(output, { recursive: true });
  const coreResult = await runProcess(build.core.output, [output, root], { cwd: buildDir, timeout: 30000 });
  await writeFile(path.join(buildDir, 'core-result.json'), JSON.stringify(coreResult, null, 2));
  if (coreResult.code !== 0 || coreResult.timedOut) throw new Error(`纯 C++ 模型导出失败：${coreResult.stderr}`);
  const core = JSON.parse(coreResult.stdout);
  await createRuntime(root, runtime, build.bridgeOutput);
  if (!godot) {
    const runConfig = await readFile(path.join(root, '.run/zhouzhu.run.xml'), 'utf8');
    godot = /RUN_PATH="([^"]+)"/.exec(runConfig)?.[1];
  }
  if (!godot) throw new Error('Rider 配置缺少 Godot 路径；可传入 --godot');
  const script = path.join(TOOL_DIRECTORY, 'export-godot.gd');
  const userData = path.join(runtime, 'user-data');
  await mkdir(userData, { recursive: true });
  const godotResult = await runProcess(godot, ['--headless', '--path', runtime, '--script', script, '--', `--output=${output}`], { cwd: runtime, timeout: 60000, env: { ...process.env, APPDATA: userData, LOCALAPPDATA: userData } });
  await writeFile(path.join(buildDir, 'godot-result.json'), JSON.stringify(godotResult, null, 2));
  let engine;
  try { engine = JSON.parse(await readFile(path.join(output, 'godot-manifest.json'), 'utf8')); }
  catch { throw new Error(`Godot 没有写出导出清单，退出码 ${godotResult.code}；查看 ${buildDir}/godot-result.json`); }
  const hashes = new Map(sources.map(source => [source.path, source.sha256]));
  const catalog = await loadCatalog(root);
  const names = new Map([...catalog.items, ...catalog.buildings, ...catalog.modules].map(record => [record.id, record.name]));
  const models = [...core.models, ...engine.models].map(row => {
    const sourcePath = typeof row.source === 'string' ? row.source : row.source.path;
    const scatterKey = row.usage === 'gather-source' ? catalog.scatters.find(scatter => row.id === `scatter:${scatter.id}` || row.id.startsWith(`scatter:${scatter.id}_`))?.id : null;
    if (row.usage === 'gather-source' && !scatterKey) throw new Error(`采集源没有对应散布定义：${row.id}`);
    return { ...row, name: row.name || names.get(row.recordIds?.[0]) || row.id, kind: 'game', format: 'mesh-json', notes: row.notes || row.source?.note || '', itemIds: row.itemIds || (row.item ? [row.item] : []), ...(scatterKey ? { collection: 'scatters', recordIds: [scatterKey] } : {}), source: { path: sourcePath, sha256: hashes.get(sourcePath) }, ...(row.usage === 'ship-module' ? { implementationSource: { path: 'src/ship.cpp', sha256: hashes.get('src/ship.cpp') } } : {}) };
  });
  for (const model of models) {
    const filename = model.url.slice('/assets/models/'.length);
    if (filename.includes('/') || filename.includes('\\') || !filename.endsWith('.json')) throw new Error(`导出器生成了非法资源路径：${model.url}`);
    const bytes = await readFile(path.join(output, filename));
    const mesh = assertMesh(JSON.parse(bytes), model.id);
    model.assetSha256 = sha(bytes);
    model.vertices = mesh.parts.reduce((n, part) => n + part.positions.length / 3, 0);
    model.triangles = mesh.parts.reduce((n, part) => n + part.indices.length / 3, 0);
    if (!model.source.sha256) throw new Error(`缺少来源指纹：${model.id}`);
  }
  const represented = new Set(models.filter(m => ['placed', 'construction', 'gather-source'].includes(m.usage)).flatMap(m => m.itemIds));
  const missingItems = catalog.items.filter(item => !represented.has(item.id)).map(item => ({
    id: item.id,
    reason: ['millet', 'bean'].includes(item.id) ? '田中有植株模型，库存种子/粮食没有独立模型；本轮尚未导出植株。' : item.id === 'salt' ? '盐井有采场模型，但没有独立岩盐物品模型；本轮尚未导出盐井。' : '当前游戏没有这件库存物品的独立模型，货运统一使用着色木箱。',
  }));
  const after = await fingerprint(root, before.map(source => source.path));
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('导出期间游戏 DLL/配置或主构建缓存发生变化，请核对并重新导出');
  const afterSources = await fingerprint(root, sourceInputs);
  if (JSON.stringify(sources) !== JSON.stringify(afterSources)) throw new Error('导出期间 C++、模板或着色器来源发生变化，不能赋予错误来源指纹，请重新导出');
  const manifest = {
    schemaVersion: 1, generatedAt: new Date().toISOString(), models, missingItems, sources,
    exporter: { core: 'current-source', propsAndScatter: 'tool-only-gdextension/current-source', ship: 'existing-game-dll', compiler: build.compiler, godot, headless: true, gameFilesUnchanged: before },
    limitations: ['采集源模型不是库存物品模型。', '纯 C++ 建筑使用当前模板和默认平地；搭建件使用注明的邻接上下文。作者家具与蒙皮人物使用独立model-registry，不能以此原生构件快照替代。', '保留静态原始几何与材质色；独立视窗不还原世界曲率、天气、季节、积雪或风摆。', '船模块从既有 DLL 的合法参考船布局截取，邻节会决定栏杆/墙面；不包含全船共用船首和连接梯子。独立导出工程不注入美术实体绑定或货物账本。'],
    failures: engine.failures || [],
  };
  await writeFile(path.join(output, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  await copyFile(path.join(output, 'godot-manifest.json'), path.join(buildDir, 'godot-manifest.json'));
  await unlink(path.join(output, 'godot-manifest.json'));
  return { modelCount: models.length, missingItemCount: missingItems.length, manifest: path.join(output, 'manifest.json'), godotExitCode: godotResult.code, failures: manifest.failures };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  exportModels(parseArguments(process.argv.slice(2))).then(result => {
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
    if (result.godotExitCode !== 0 || result.failures.length) process.exitCode = 1;
  }).catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
}
