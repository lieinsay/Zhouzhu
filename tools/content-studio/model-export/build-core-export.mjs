// 只描述独立导出器的 MSVC 构建，不调用 shell、不触碰游戏 CMake 缓存或 DLL。
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const CORE_SOURCES = Object.freeze([
  'src/piece_mesh.cpp',
  'src/build_pieces.cpp',
  'src/building_mesh.cpp',
  'src/building_parts.cpp',
  'src/buildings.cpp',
  'src/house_template.cpp',
  'src/interior_rules.cpp',
  'src/land_berth.cpp',
  'src/settlement_model.cpp',
  'src/map_ground.cpp',
  'src/ship_layout.cpp',
]);

export const CORE_HEADERS = Object.freeze([
  'src/piece_mesh.h', 'src/build_pieces.h', 'src/building_mesh.h',
  'src/building_parts.h', 'src/buildings.h', 'src/house_template.h',
  'src/interior_rules.h', 'src/land_berth.h', 'src/settlement_model.h',
  'src/map_ground.h', 'src/ground_query.h', 'src/ship_layout.h',
  'src/wild_landing.h', 'src/mesh_kit.h',
]);

export const CORE_TEMPLATES = Object.freeze([
  'game/buildings/house/sanjian.txt',
  'game/buildings/hall/citang.txt',
  'game/buildings/granary/cang.txt',
  'game/buildings/tower/wanglou.txt',
]);

const exporterSource = 'tools/content-studio/model-export/core-export.cpp';

/** 在已载入当前 Rider MSVC x64 环境的调用方中使用；此函数本身不执行构建。 */
export function coreExportBuildSpec(repoRoot, buildDirectory) {
  const root = path.resolve(repoRoot);
  const buildDir = path.resolve(buildDirectory);
  const output = path.join(buildDir, 'core-export.exe');
  return {
    command: 'cl.exe',
    cwd: buildDir,
    output,
    args: [
      '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/MD', '/Zi', '/Od', '/W3',
      `/I${path.join(root, 'src')}`,
      `/Fe:${output}`,
      `/Fd:${path.join(buildDir, 'core-export.pdb')}`,
      path.join(root, exporterSource),
      ...CORE_SOURCES.map(source => path.join(root, source)),
      '/link', '/INCREMENTAL:NO',
    ],
    inputs: [exporterSource, ...CORE_SOURCES, ...CORE_HEADERS, ...CORE_TEMPLATES],
    instructions: '先核对当前 CMakeCache 的 MSVC x64 路径，载入对应 vcvars64；创建工具自己的忽略构建目录，再按本规格编译。游戏缓存及 DLL 保持不变。',
  };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = process.argv[2] || fileURLToPath(new URL('../../../', import.meta.url));
  const buildDir = process.argv[3] || path.join(root, 'tools/content-studio/.cache/model-export');
  process.stdout.write(`${JSON.stringify(coreExportBuildSpec(root, buildDir), null, 2)}\n`);
}
