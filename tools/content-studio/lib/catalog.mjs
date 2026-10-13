import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';

export const SOURCE_PATHS = [
  'src/crops.cpp', 'src/crops.h', 'src/recipes.cpp', 'src/recipes.h',
  'src/placeables.cpp', 'src/placeables.h', 'src/build_pieces.cpp', 'src/build_pieces.h',
  'src/buildings.cpp', 'src/buildings.h', 'src/ship_layout.cpp', 'src/ship_layout.h',
  'src/deposit_kinds.cpp', 'src/deposit_kinds.h', 'src/building_parts.h',
];
const sha256 = text => createHash('sha256').update(text).digest('hex');

// Only the literal subset used by the source tables is accepted. Expressions,
// macros, unknown escapes and field-count changes stop import with a location.
class InitializerParser {
  constructor(text, path, at = 0) { this.text = text; this.path = path; this.at = at; }
  error(message) {
    const line = this.text.slice(0, this.at).split('\n').length;
    throw new Error(`${this.path}:${line}：${message}`);
  }
  skip() {
    for (;;) {
      const rest = this.text.slice(this.at);
      const ws = /^\s+/.exec(rest);
      if (ws) { this.at += ws[0].length; continue; }
      if (rest.startsWith('//')) {
        const end = this.text.indexOf('\n', this.at);
        this.at = end < 0 ? this.text.length : end; continue;
      }
      if (rest.startsWith('/*')) {
        const end = this.text.indexOf('*/', this.at + 2);
        if (end < 0) this.error('未结束的注释');
        this.at = end + 2; continue;
      }
      return;
    }
  }
  value() {
    this.skip();
    const pos = this.at;
    const rest = this.text.slice(pos);
    if (rest.startsWith('{')) {
      this.at++;
      const values = [];
      this.skip();
      while (this.text[this.at] !== '}') {
        if (this.at >= this.text.length) this.error('未结束的初始化列表');
        values.push(this.value());
        this.skip();
        if (this.text[this.at] === '}') break;
        if (this.text[this.at] !== ',') this.error('初始化器缺少逗号，或使用了尚不支持的表达式');
        this.at++; this.skip();
      }
      this.at++;
      return { value: values, pos };
    }
    if (rest.startsWith('"')) {
      this.at++;
      let value = '';
      while (this.at < this.text.length) {
        const c = this.text[this.at++];
        if (c === '"') return { value, pos };
        if (c === '\\') {
          const escape = this.text[this.at++];
          const escapes = { '"': '"', '\\': '\\', n: '\n', r: '\r', t: '\t' };
          if (!(escape in escapes)) this.error(`尚不支持字符串转义 \\${escape}`);
          value += escapes[escape];
        } else {
          if (c === '\n' || c === '\r') this.error('字符串中出现未转义换行');
          value += c;
        }
      }
      this.error('未结束的字符串');
    }
    const num = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?\b/.exec(rest);
    if (num) { this.at += num[0].length; return { value: Number(num[0].replace(/[fF]$/, '')), pos }; }
    const identifier = /^[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*/.exec(rest);
    if (identifier) {
      this.at += identifier[0].length;
      if (identifier[0] === 'int') {
        this.skip();
        if (this.text[this.at++] !== '(') this.error('仅支持 int(枚举值)');
        const inner = this.value(); this.skip();
        if (this.text[this.at++] !== ')') this.error('未结束的 int 转换');
        return { value: inner.value, pos };
      }
      if (identifier[0] === 'true') return { value: true, pos };
      if (identifier[0] === 'false') return { value: false, pos };
      if (identifier[0] === 'nullptr') return { value: null, pos };
      if (identifier[0].includes('::')) return { value: { enum: identifier[0] }, pos };
    }
    this.error(`尚不支持的初始化器：${rest.slice(0, 50)}`);
  }
}

export function parseInitializer(text, declaration, path = 'source.cpp') {
  const pattern = new RegExp(`\\b${declaration.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*\\[\\s*\\]\\s*=\\s*`);
  const match = pattern.exec(text);
  if (!match) throw new Error(`${path}：找不到初始化表 ${declaration}`);
  const parser = new InitializerParser(text, path, match.index + match[0].length);
  const result = parser.value(); parser.skip();
  if (text[parser.at] !== ';' || !Array.isArray(result.value)) parser.error('数组声明格式不受支持');
  return result.value;
}

function fields(node, count, path) {
  if (!Array.isArray(node.value) || !count.includes(node.value.length)) {
    throw new Error(`${path}：初始化行字段数错误（期望 ${count.join('/')}，实际 ${node.value?.length ?? '非列表'}）`);
  }
  return node.value.map(value => value.value);
}
function enumeration(value, name, path) {
  if (!value?.enum?.startsWith(`${name}::`)) throw new Error(`${path}：期望 ${name} 枚举`);
  return value.enum.slice(name.length + 2);
}
function typed(value, type, path) {
  if (typeof value !== type) throw new Error(`${path}：期望 ${type} 字面量`);
  return value;
}
function enumValues(text, name, path) {
  const match = new RegExp(`enum\\s+class\\s+${name}\\b[^{}]*\\{([\\s\\S]*?)\\}`).exec(text);
  if (!match) throw new Error(`${path}：找不到枚举 ${name}`);
  return match[1].replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g, '').split(',').map(v => v.trim()).filter(Boolean).map(v => {
    if (!/^[A-Z][A-Z_]*$/.test(v)) throw new Error(`${path}：${name} 的枚举声明格式尚不支持`);
    return v;
  }).filter(v => v !== 'COUNT');
}

export async function loadCatalog(repositoryRoot) {
  const source = new Map(await Promise.all(SOURCE_PATHS.map(async path => {
    const bytes = await readFile(resolve(repositoryRoot, path));
    const text = bytes.toString('utf8');
    if (!Buffer.from(text, 'utf8').equals(bytes)) throw new Error(`${path}：源码不是完整有效的 UTF-8，不能可靠导入或核对指纹`);
    return [path, text];
  })));
  if (!/int\s+levels\s*=\s*1\s*;/.test(source.get('src/buildings.h')) || !/bool\s+house\s*=\s*true\s*;/.test(source.get('src/buildings.h'))) throw new Error('src/buildings.h：BuildingDef 默认字段已变化，请更新导入器后再读取省略字段的行');
  const rows = (path, declaration, convert) => parseInitializer(source.get(path), declaration, path).map(node => {
    const line = source.get(path).slice(0, node.pos).split('\n').length;
    return { ...convert(node, `${path}:${line}`), source: { path, line } };
  });
  const catalog = { schemaVersion: 1 };
  catalog.items = rows('src/crops.cpp', 'ITEMS', (node, path) => {
    const [id, name, stack, r, g, b] = fields(node, [6], path);
    return { id: typed(id, 'string', path), name: typed(name, 'string', path), stack: typed(stack, 'number', path), color: [r, g, b].map(c => typed(c, 'number', path)) };
  });
  catalog.recipes = rows('src/recipes.cpp', 'RECIPES', (node, path) => {
    const [id, product, yieldCount, station, input] = fields(node, [5], path);
    if (!Array.isArray(input)) throw new Error(`${path}：配方材料必须是初始化列表`);
    const ingredients = input.map(n => {
      const [item, count] = fields(n, [2], path);
      return { item: typed(item, 'string', path), count: typed(count, 'number', path) };
    });
    return { id: typed(id, 'string', path), product: typed(product, 'string', path), yield: typed(yieldCount, 'number', path), station: enumeration(station, 'Station', path), ingredients };
  });
  catalog.props = rows('src/placeables.cpp', 'PROPS', (node, path) => {
    const [item, width, depth, height, look, slots, station, light_m] = fields(node, [8], path);
    return { id: typed(item, 'string', path), item, width: typed(width, 'number', path), depth: typed(depth, 'number', path), height: typed(height, 'number', path), look: enumeration(look, 'PropLook', path), slots: typed(slots, 'number', path), station: station === -1 ? null : enumeration(station, 'Station', path), light_m: typed(light_m, 'number', path) };
  });
  catalog.pieces = rows('src/build_pieces.cpp', 'pieces::DEFS', (node, path) => {
    const [item, role, slot, material] = fields(node, [4], path);
    return { id: typed(item, 'string', path), item, role: enumeration(role, 'PieceRole', path), slot: enumeration(slot, 'PieceSlot', path), material: enumeration(material, 'PartMaterial', path) };
  });
  catalog.buildings = rows('src/buildings.cpp', 'DEFS', (node, path) => {
    const [id, name, width_m, depth_m, plinth_m, wall_m, roof_m, levels = 1, house = true] = fields(node, [7, 8, 9], path);
    for (const v of [width_m, depth_m, plinth_m, wall_m, roof_m, levels]) typed(v, 'number', path);
    return { id: typed(id, 'string', path), name: typed(name, 'string', path), width_m, depth_m, plinth_m, wall_m, roof_m, levels, house: typed(house, 'boolean', path) };
  });
  catalog.modules = rows('src/ship_layout.cpp', 'DEFS', (node, path) => {
    const [kind, id, name, size_x, size_y, size_z, mass, thrust, bay, cargo_deck, top_only, bottom_only, max_count, required, removable, enterable, furnish, look] = fields(node, [18], path);
    for (const v of [size_x, size_y, size_z, mass, thrust, bay, max_count]) typed(v, 'number', path);
    for (const v of [cargo_deck, top_only, bottom_only, removable, enterable, furnish]) typed(v, 'boolean', path);
    if (required !== null) typed(required, 'string', path);
    return { kind: enumeration(kind, 'ModuleKind', path), id: typed(id, 'string', path), name: typed(name, 'string', path), size_x, size_y, size_z, mass, thrust, bay, cargo_deck, top_only, bottom_only, max_count, required, removable, enterable, furnish, look: enumeration(look, 'ModuleLook', path) };
  });
  catalog.deposits = rows('src/deposit_kinds.cpp', 'KINDS', (node, path) => {
    const [key, name, form, site, verb, product, gather_s, base_stock, refill_hours, look, r, g, b] = fields(node, [13], path);
    for (const v of [key, name, site, verb, product]) typed(v, 'string', path);
    for (const v of [gather_s, base_stock, refill_hours, r, g, b]) typed(v, 'number', path);
    return { id: key, key, name, form: enumeration(form, 'DepositForm', path), site, verb, product, gather_s, base_stock, refill_hours, look: enumeration(look, 'DepositLook', path), color: [r, g, b] };
  });
  catalog.scatters = rows('src/deposit_kinds.cpp', 'SCATTER', (node, path) => {
    const [key, name, verb, product, gather_s, yieldCount, refill_hours, look, solid, radius, r, g, b] = fields(node, [13], path);
    for (const v of [key, name, verb, product]) typed(v, 'string', path);
    for (const v of [gather_s, yieldCount, refill_hours, radius, r, g, b]) typed(v, 'number', path);
    return { id: key, key, name, verb, product, gather_s, yield: yieldCount, refill_hours, look: enumeration(look, 'ScatterLook', path), solid: typed(solid, 'boolean', path), radius, color: [r, g, b] };
  });
  catalog.tools = rows('src/crops.cpp', 'TOOLS', (node, path) => {
    const [item, products, speed] = fields(node, [3], path);
    return { id: typed(item, 'string', path), item, products: typed(products, 'string', path).split(','), speed: typed(speed, 'number', path) };
  });
  catalog.crops = rows('src/crops.cpp', 'CROPS', (node, path) => {
    const [id, name, seed, product, grow_factor, yieldCount] = fields(node, [6], path);
    for (const v of [id, name, seed, product]) typed(v, 'string', path);
    return { id, name, seed, product, grow_factor: typed(grow_factor, 'number', path), yield: typed(yieldCount, 'number', path) };
  });
  catalog.stations = [{ id: 'HAND', name: '随身' }, { id: 'WORKBENCH', name: '工作台' }, { id: 'KILN', name: '窑' }];
  const oreNodes = parseInitializer(source.get('src/deposit_kinds.cpp'), 'ORES', 'src/deposit_kinds.cpp');
  const ores = oreNodes.map(node => {
    const [subtype, item] = fields(node, [2], 'src/deposit_kinds.cpp');
    return { subtype: typed(subtype, 'string', 'ORES'), item: typed(item, 'string', 'ORES') };
  });
  const rawItems = parseInitializer(source.get('src/deposit_kinds.cpp'), 'RAW_ITEMS', 'src/deposit_kinds.cpp').map(node => typed(node.value, 'string', 'RAW_ITEMS'));
  const products = new Set(catalog.recipes.map(r => r.product));
  const externalItems = catalog.items.filter(item => !products.has(item.id)).map(item => item.id);
  const enumSpecs = [
    ['Station', 'src/recipes.h'], ['PropLook', 'src/placeables.h'], ['PieceRole', 'src/build_pieces.h'],
    ['PieceSlot', 'src/build_pieces.h'], ['PartMaterial', 'src/building_parts.h'],
    ['ModuleKind', 'src/ship_layout.h'], ['ModuleLook', 'src/ship_layout.h'],
    ['DepositForm', 'src/deposit_kinds.h'], ['DepositLook', 'src/deposit_kinds.h'], ['ScatterLook', 'src/deposit_kinds.h'],
  ];
  catalog.meta = {
    sources: SOURCE_PATHS.map(path => ({ path, sha256: sha256(source.get(path)) })),
    rawItems, externalItems, ores,
    enums: Object.fromEntries(enumSpecs.map(([name, path]) => [name, enumValues(source.get(path), name, path)])),
    notes: [
      '直接导入当前 C++ 常量表；工具草稿不会被游戏加载。',
      '资源点的空 product 可能表示地标或由既有子类规则决定；工具不补猜采集产物。',
      'externalItems 是基线中无制作配方的物品入口（采集、耕作、贸易、改地形等），不表示每样都可野外采集。',
      '新增 enum、机械行为、船模块类型及资源子类还需要游戏代码实现；草稿仅表达提案。',
    ],
  };
  return catalog;
}
