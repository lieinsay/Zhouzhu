// Shared by Node and the browser. No filesystem or runtime dependencies.
export const COLLECTIONS = ['items', 'recipes', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops', 'stations'];
export const VISUAL_COLLECTIONS = ['items', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops'];
export const ROW_SHAPES = {
  items: { id: 'string', name: 'string', stack: 'number', color: 'color' },
  recipes: { id: 'string', product: 'string', yield: 'number', station: 'string', ingredients: 'ingredients' },
  props: { id: 'string', item: 'string', width: 'number', depth: 'number', height: 'number', look: 'string', slots: 'number', station: 'nullable-string', light_m: 'number' },
  pieces: { id: 'string', item: 'string', role: 'string', slot: 'string', material: 'string' },
  buildings: { id: 'string', name: 'string', width_m: 'number', depth_m: 'number', plinth_m: 'number', wall_m: 'number', roof_m: 'number', levels: 'number', house: 'boolean' },
  modules: { id: 'string', name: 'string', kind: 'string', size_x: 'number', size_y: 'number', size_z: 'number', mass: 'number', thrust: 'number', bay: 'number', cargo_deck: 'boolean', top_only: 'boolean', bottom_only: 'boolean', max_count: 'number', required: 'nullable-string', removable: 'boolean', enterable: 'boolean', furnish: 'boolean', look: 'string' },
  deposits: { id: 'string', key: 'string', name: 'string', form: 'string', site: 'string', verb: 'string', product: 'string', gather_s: 'number', base_stock: 'number', refill_hours: 'number', look: 'string', color: 'color' },
  scatters: { id: 'string', key: 'string', name: 'string', verb: 'string', product: 'string', gather_s: 'number', yield: 'number', refill_hours: 'number', look: 'string', solid: 'boolean', radius: 'number', color: 'color' },
  tools: { id: 'string', item: 'string', products: 'strings', speed: 'number' },
  crops: { id: 'string', name: 'string', seed: 'string', product: 'string', grow_factor: 'number', yield: 'number' },
  stations: { id: 'string', name: 'string' },
};
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const finite = value => typeof value === 'number' && Number.isFinite(value);
const stationIds = ['HAND', 'WORKBENCH', 'KILN'];
const fallbackEnums = {
  PropLook: ['WORKBENCH', 'KILN', 'CHEST', 'TORCH', 'FENCE'],
  PieceRole: ['FOUNDATION', 'FLOOR', 'WALL', 'DOOR', 'WINDOW', 'GABLE', 'ROOF', 'PILLAR', 'LADDER'],
  PieceSlot: ['CELL', 'EDGE', 'CORNER'],
  PartMaterial: ['STONE', 'WALL', 'FLOOR', 'WOOD', 'DARK_WOOD', 'ROOF', 'CLAY', 'CLOTH', 'STRAW', 'GRAIN', 'LACQUER', 'BRICK'],
  ModuleKind: ['BRIDGE', 'STORAGE', 'SAIL', 'FLOATSTONE', 'DECK', 'LONG_SAIL', 'TALL_HOLD', 'WIDE_DECK'],
  ModuleLook: ['HELM', 'CARGO', 'SAIL', 'FLOATSTONE', 'BARE'],
  DepositForm: ['POINT', 'PATCH', 'FIELD'],
  DepositLook: ['NONE', 'QUARRY', 'PIT', 'BAR', 'SLUICE', 'MINE', 'CRUST', 'SPRING', 'HOTSPRING', 'CAVE'],
  ScatterLook: ['BROADLEAF', 'CONIFER', 'SHRUB', 'REEDS', 'BOULDER', 'FLOATSTONE', 'CLAY_BANK', 'GRAVEL_HEAP', 'PLACER', 'ORE_ROCK', 'CRUST'],
};

export function assertProjectStructure(project) {
  if (!object(project) || project.schemaVersion !== 1) throw new Error('项目必须是 schemaVersion: 1 的 JSON 对象');
  const topKeys = new Set(['schemaVersion', 'meta', 'visuals', ...COLLECTIONS]);
  for (const key of Object.keys(project)) if (!topKeys.has(key)) throw new Error(`不支持的项目字段 ${key}`);
  if (!object(project.meta)) throw new Error('项目缺少 meta 来源元数据');
  for (const collection of COLLECTIONS) {
    const rows = project[collection];
    if (!Array.isArray(rows) || rows.length > 10000) throw new Error(`${collection} 必须是数组且不超过 10000 行`);
    for (const [index, row] of rows.entries()) {
      const path = `${collection}[${index}]`;
      if (!object(row)) throw new Error(`${path} 必须是对象`);
      const shape = ROW_SHAPES[collection];
      for (const key of Object.keys(row)) if (key !== 'source' && !Object.hasOwn(shape, key)) throw new Error(`${path} 不支持字段 ${key}`);
      for (const [field, type] of Object.entries(shape)) {
        const v = row[field];
        let valid;
        if (type === 'color') valid = Array.isArray(v) && v.length === 3 && v.every(finite);
        else if (type === 'strings') valid = Array.isArray(v) && v.length <= 10000 && v.every(s => typeof s === 'string');
        else if (type === 'ingredients') valid = Array.isArray(v) && v.length <= 10000 && v.every(n => object(n) && Object.keys(n).every(k => k === 'item' || k === 'count') && typeof n.item === 'string' && finite(n.count));
        else if (type === 'nullable-string') valid = v === null || typeof v === 'string';
        else if (type === 'number') valid = finite(v);
        else valid = typeof v === type;
        if (!valid) throw new Error(`${path}.${field} 结构错误，期望 ${type}`);
        if (typeof v === 'string' && v.length > 4096) throw new Error(`${path}.${field} 字符串过长`);
      }
      if (row.source !== undefined && (!object(row.source) || typeof row.source.path !== 'string' || !Number.isInteger(row.source.line) || row.source.line < 1)) throw new Error(`${path}.source 来源结构错误`);
    }
  }
  if (!Array.isArray(project.meta.sources) || !project.meta.sources.every(s => object(s) && typeof s.path === 'string' && /^[a-f0-9]{64}$/.test(s.sha256))) throw new Error('meta.sources 必须包含源码路径和 SHA-256');
  for (const field of ['rawItems', 'externalItems']) if (!Array.isArray(project.meta[field]) || !project.meta[field].every(s => typeof s === 'string')) throw new Error(`meta.${field} 必须是物品 ID 数组`);
  if (project.meta.enums !== undefined && (!object(project.meta.enums) || !Object.values(project.meta.enums).every(v => Array.isArray(v) && v.every(s => typeof s === 'string')))) throw new Error('meta.enums 必须是枚举名称到字符串数组的映射');
  assertVisualStructure(project.visuals);
}

function assertVisualStructure(visuals) {
  if (visuals === undefined) return;
  if (!object(visuals)) throw new Error('visuals 必须是按集合和记录 ID 分组的对象');
  for (const [collection, entries] of Object.entries(visuals)) {
    if (!VISUAL_COLLECTIONS.includes(collection) || !object(entries) || Object.keys(entries).length > 10000) throw new Error(`visuals.${collection} 集合不受支持或结构错误`);
    for (const [id, visual] of Object.entries(entries)) {
      const path = `visuals.${collection}.${id}`;
      if (!/^[a-z][a-z0-9_]*$/.test(id) || !object(visual)) throw new Error(`${path} 记录结构错误`);
      for (const [field, value] of Object.entries(visual)) {
        if (field === 'thumbnail' || field === 'model') {
          const validId = field === 'model' ? /^[a-zA-Z0-9][a-zA-Z0-9_:-]{0,127}$/ : /^[a-zA-Z0-9][a-zA-Z0-9_-]{0,127}$/;
          if (value !== null && (typeof value !== 'string' || !validId.test(value))) throw new Error(`${path}.${field} 必须是媒体库 ID 或 null，不能是 URL 或路径`);
        } else if (['scale', 'rotation', 'offset'].includes(field)) {
          if (!Array.isArray(value) || value.length !== 3 || !value.every(finite)) throw new Error(`${path}.${field} 必须是有限数值三元组`);
          const valid = field === 'scale' ? value.every(n => n > 0 && n <= 100) : value.every(n => Math.abs(n) <= (field === 'rotation' ? 36000 : 100000));
          if (!valid) throw new Error(`${path}.${field} 超过允许范围`);
        } else throw new Error(`${path} 不支持字段 ${field}`);
      }
    }
  }
}

export function validateVisuals(project, library) {
  const issues = [];
  try { assertVisualStructure(project.visuals); } catch (error) { return [{ severity: 'error', collection: 'project', id: '', field: 'visuals', message: error.message }]; }
  const thumbnails = library && new Set((library.thumbnails ?? []).map(asset => asset.id));
  const models = library && new Set((library.models ?? []).map(asset => asset.id));
  for (const [collection, entries] of Object.entries(project.visuals ?? {})) {
    const ids = new Set((project[collection] ?? []).map(row => row.id));
    for (const [id, visual] of Object.entries(entries)) {
      const issue = (field, message) => issues.push({ severity: 'error', collection, id, field: `visuals.${field}`, message });
      if (!ids.has(id)) issue('id', `视觉配置目标“${id}”不存在，可能已被删除`);
      if (visual.thumbnail && thumbnails && !thumbnails.has(visual.thumbnail)) issue('thumbnail', `缩略图“${visual.thumbnail}”不在工具媒体库中`);
      if (visual.model && models && !models.has(visual.model)) issue('model', `模型“${visual.model}”不在工具媒体库中`);
    }
  }
  return issues;
}

export function validateProject(project, baseline = project, library) {
  const issues = [];
  const issue = (collection, row, field, message, severity = 'error') => issues.push({ severity, collection, id: row?.id ?? '', field, message });
  try { assertProjectStructure(project); } catch (error) {
    issue('project', null, 'structure', error.message); return issues;
  }
  issues.push(...validateVisuals(project, library));
  const itemIds = new Set(project.items.map(item => item.id));
  const enums = { ...fallbackEnums, ...(baseline?.meta?.enums ?? {}) };
  const ref = (collection, row, field, id, optional = false) => {
    if (!id && optional) return;
    if (!itemIds.has(id)) issue(collection, row, field, `引用的物品“${id}”不存在，可能已被删除`);
  };
  const range = (collection, row, field, min, max = Number.MAX_VALUE, integer = false) => {
    const value = row[field];
    if (!finite(value) || value < min || value > max || (integer && !Number.isInteger(value))) issue(collection, row, field, `${field} 应为${integer ? '整数' : '有限数值'}，范围 ${min}～${max === Number.MAX_VALUE ? '有限正数' : max}`);
  };
  const oneOf = (collection, row, field, options) => {
    if (!options.includes(row[field])) issue(collection, row, field, `不支持 ${field} 值“${row[field]}”，新增类型还需要对应的游戏实现`);
  };
  for (const collection of COLLECTIONS) {
    const seen = new Set();
    for (const row of project[collection]) {
      if (!/^[a-z][a-z0-9_]*$/.test(row.id) && !(collection === 'stations' && stationIds.includes(row.id))) issue(collection, row, 'id', 'ID 需以小写字母开头，仅含小写字母、数字和下划线');
      if (seen.has(row.id)) issue(collection, row, 'id', `ID“${row.id}”重复`);
      seen.add(row.id);
      if ('name' in row && !row.name.trim()) issue(collection, row, 'name', '显示名称不能为空');
      if ('color' in row && row.color.some(n => n < 0 || n > 1)) issue(collection, row, 'color', '颜色分量应在 0～1 内');
    }
  }
  if (project.stations.length !== 3 || stationIds.some(id => !project.stations.some(row => row.id === id))) issue('stations', null, 'id', '制作设施固定为 HAND、WORKBENCH、KILN，不能新增或删除');
  for (const row of project.items) range('items', row, 'stack', 1, 2147483647, true);
  for (const row of project.recipes) {
    ref('recipes', row, 'product', row.product);
    range('recipes', row, 'yield', 1, 2147483647, true);
    oneOf('recipes', row, 'station', stationIds);
    if (row.ingredients.length < 1 || row.ingredients.length > 3) issue('recipes', row, 'ingredients', '当前 RecipeDef 支持 1～3 种材料');
    const inputs = new Set();
    for (const [index, ingredient] of row.ingredients.entries()) {
      ref('recipes', row, `ingredients.${index}.item`, ingredient.item);
      if (!Number.isInteger(ingredient.count) || ingredient.count < 1 || ingredient.count > 2147483647) issue('recipes', row, `ingredients.${index}.count`, '材料份数需为 1～2147483647 的整数');
      if (inputs.has(ingredient.item)) issue('recipes', row, 'ingredients', `材料“${ingredient.item}”重复，请合并份数`);
      inputs.add(ingredient.item);
      if (ingredient.item === row.product) issue('recipes', row, 'ingredients', '配方直接依赖自己的产物，无法作为制作入口');
    }
  }
  for (const row of project.props) {
    ref('props', row, 'item', row.item);
    if (row.id !== row.item) issue('props', row, 'id', '可放置物的 ID 必须与 item 相同');
    for (const field of ['width', 'depth', 'height']) range('props', row, field, Number.MIN_VALUE);
    range('props', row, 'slots', 0, 2147483647, true);
    range('props', row, 'light_m', 0);
    oneOf('props', row, 'look', enums.PropLook);
    oneOf('props', row, 'station', [null, 'WORKBENCH', 'KILN']);
  }
  for (const row of project.pieces) {
    ref('pieces', row, 'item', row.item);
    if (row.id !== row.item) issue('pieces', row, 'id', '搭建件的 ID 必须与 item 相同');
    oneOf('pieces', row, 'role', enums.PieceRole);
    oneOf('pieces', row, 'slot', enums.PieceSlot);
    oneOf('pieces', row, 'material', enums.PartMaterial);
    const expected = ['WALL', 'DOOR', 'WINDOW', 'GABLE'].includes(row.role) ? 'EDGE' : row.role === 'PILLAR' ? 'CORNER' : 'CELL';
    if (row.slot !== expected) issue('pieces', row, 'slot', `${row.role} 的现有槽位规则要求 ${expected}`);
  }
  for (const row of project.buildings) {
    for (const field of ['width_m', 'depth_m']) range('buildings', row, field, Number.MIN_VALUE);
    for (const field of ['plinth_m', 'wall_m', 'roof_m']) range('buildings', row, field, 0);
    range('buildings', row, 'levels', 1, 2147483647, true);
    if (row.house && row.wall_m <= 0) issue('buildings', row, 'wall_m', '房屋的墙高必须大于 0');
    if (row.house && row.roof_m <= 0) issue('buildings', row, 'roof_m', '现有房屋外壳要求正屋顶高度');
  }
  const moduleKinds = new Set();
  for (const [index, row] of project.modules.entries()) {
    oneOf('modules', row, 'kind', enums.ModuleKind);
    oneOf('modules', row, 'look', enums.ModuleLook);
    if (moduleKinds.has(row.kind)) issue('modules', row, 'kind', `模块枚举 ${row.kind} 重复；现有表按枚举一一对应`);
    moduleKinds.add(row.kind);
    if (enums.ModuleKind[index] !== row.kind) issue('modules', row, 'kind', `当前游戏按枚举下标读取模块，第 ${index + 1} 行必须为 ${enums.ModuleKind[index] ?? '现有枚举范围内的定义'}`);
    range('modules', row, 'size_x', 1, 3, true); range('modules', row, 'size_y', 1, 3, true); range('modules', row, 'size_z', 1, 7, true);
    if (row.size_x * row.size_y * row.size_z > 12) issue('modules', row, 'size_x', '单模块占格不能超过现有空艇的 12 节上限');
    range('modules', row, 'mass', Number.MIN_VALUE); range('modules', row, 'thrust', 0);
    range('modules', row, 'bay', 0, 2147483647, true); range('modules', row, 'max_count', 0, 2147483647, true);
    if (row.top_only && row.bottom_only) issue('modules', row, 'top_only', '同时要求顶层与底层，仅能用于单层布局，请核对', 'warning');
  }
  const scatterLooks = new Set();
  for (const [index, row] of project.scatters.entries()) {
    if (scatterLooks.has(row.look)) issue('scatters', row, 'look', `散布类型 ${row.look} 重复；现有表按枚举一一对应`);
    scatterLooks.add(row.look);
    if (enums.ScatterLook[index] !== row.look) issue('scatters', row, 'look', `当前游戏按枚举下标读取散布物，第 ${index + 1} 行必须为 ${enums.ScatterLook[index] ?? '现有枚举范围内的定义'}`);
  }
  for (const look of enums.ScatterLook) if (!scatterLooks.has(look)) issue('scatters', { id: look }, 'look', `现有散布枚举 ${look} 缺少定义，游戏按枚举索引读取`);
  for (const kind of enums.ModuleKind) if (!moduleKinds.has(kind)) issue('modules', { id: kind }, 'kind', `现有模块枚举 ${kind} 缺少定义，游戏按枚举索引读取`);
  for (const collection of ['deposits', 'scatters']) for (const row of project[collection]) {
    if (row.id !== row.key) issue(collection, row, 'id', '资源记录的 ID 必须与 key 相同');
    ref(collection, row, 'product', row.product, true);
    range(collection, row, 'gather_s', 0); range(collection, row, 'refill_hours', 0);
    if (collection === 'deposits') {
      oneOf(collection, row, 'form', enums.DepositForm); oneOf(collection, row, 'look', enums.DepositLook);
      range(collection, row, 'base_stock', 0, 2147483647, true);
      if (row.form === 'FIELD') {
        if (row.gather_s <= 0) issue(collection, row, 'gather_s', '采场每份工时必须大于 0');
        if (row.base_stock <= 0) issue(collection, row, 'base_stock', '采场基础存量必须大于 0');
      } else if (row.product) issue(collection, row, 'product', '点状地标与片状资源不会通过采场表产出；片状采集需配置散布物', 'warning');
    } else {
      oneOf(collection, row, 'look', enums.ScatterLook);
      range(collection, row, 'yield', 1, 2147483647, true); range(collection, row, 'radius', Number.MIN_VALUE);
      if (row.gather_s <= 0) issue(collection, row, 'gather_s', '散布采集每份工时必须大于 0');
    }
  }
  for (const row of project.tools) {
    ref('tools', row, 'item', row.item);
    if (row.id !== row.item) issue('tools', row, 'id', '工具的 ID 必须与 item 相同');
    range('tools', row, 'speed', Number.MIN_VALUE);
    if (!row.products.length) issue('tools', row, 'products', '工具至少应有一种对口产物');
    if (new Set(row.products).size !== row.products.length) issue('tools', row, 'products', '对口产物重复');
    for (const id of row.products) ref('tools', row, 'products', id);
  }
  for (const row of project.crops) {
    ref('crops', row, 'seed', row.seed); ref('crops', row, 'product', row.product);
    range('crops', row, 'grow_factor', Number.MIN_VALUE); range('crops', row, 'yield', 1, 2147483647, true);
  }
  // Fixed-point reachability allows alternative recipes to break dependency cycles.
  // Roots are source-declared/existing acquisition routes, not every new uncrafted item.
  const available = new Set((baseline.meta.externalItems ?? []).filter(id => itemIds.has(id)));
  for (const row of project.deposits) if (row.form === 'FIELD' && row.product) available.add(row.product);
  for (const row of project.scatters) if (row.product) available.add(row.product);
  const stations = new Set(['HAND']);
  let progress = true;
  while (progress) {
    progress = false;
    const acquire = id => { if (itemIds.has(id) && !available.has(id)) { available.add(id); progress = true; } };
    for (const row of project.props) if (row.station && available.has(row.item) && !stations.has(row.station)) { stations.add(row.station); progress = true; }
    for (const row of project.crops) if (available.has(row.seed)) acquire(row.product);
    for (const row of project.recipes) if (row.ingredients.length && stations.has(row.station) && row.ingredients.every(n => available.has(n.item))) acquire(row.product);
  }
  for (const row of project.recipes) {
    if (!stations.has(row.station) && stationIds.includes(row.station)) issue('recipes', row, 'station', `制作设施 ${row.station} 无法从现有获取入口自举；核对设施配方及放置定义`);
    const blocked = row.ingredients.filter(n => itemIds.has(n.item) && !available.has(n.item)).map(n => n.item);
    if (blocked.length) issue('recipes', row, 'ingredients', `材料无法从现有获取入口到达：${[...new Set(blocked)].join('、')}（可能存在闭环或缺少来源）`);
  }
  for (const row of project.items) if (!available.has(row.id) && !project.recipes.some(r => r.product === row.id)) issue('items', row, 'id', '尚未配置制作、采集或作物来源；该物品不能通过本草稿的获取链得到', 'warning');
  for (const row of project.modules) if (!baseline.modules?.some(r => r.id === row.id)) issue('modules', row, 'kind', '新增空艇模块还需要更新游戏枚举、网格与布局逻辑；草稿不会安装这些行为', 'warning');
  for (const collection of ['deposits', 'scatters']) for (const row of project[collection]) if (baseline !== project && !baseline[collection]?.some(r => r.id === row.id)) issue(collection, row, 'key', '新增资源键还需生成器/散布规则提供实例；本工具仅保存配置提案', 'warning');
  if (baseline !== project && baseline.meta.sources.some(s => !project.meta.sources.some(p => p.path === s.path && p.sha256 === s.sha256))) issue('project', null, 'sources', '草稿的源文件指纹与当前代码不同；先核对变更报告，再决定是否按当前基线重新整理草稿', 'warning');
  return issues;
}

function withoutSource(row) {
  if (Array.isArray(row)) return row.map(withoutSource);
  if (!object(row)) return row;
  return Object.fromEntries(Object.keys(row).filter(key => key !== 'source').sort().map(key => [key, withoutSource(row[key])]));
}

export function diffProjects(baseline, project) {
  const changes = [];
  for (const collection of COLLECTIONS) {
    const before = new Map((baseline[collection] ?? []).map(row => [row.id, row]));
    const after = new Map((project[collection] ?? []).map(row => [row.id, row]));
    for (const [id, row] of before) {
      if (!after.has(id)) changes.push({ collection, id, type: 'deleted', before: row, after: null });
      else if (JSON.stringify(withoutSource(row)) !== JSON.stringify(withoutSource(after.get(id)))) changes.push({ collection, id, type: 'modified', before: row, after: after.get(id) });
    }
    for (const [id, row] of after) if (!before.has(id)) changes.push({ collection, id, type: 'added', before: null, after: row });
  }
  for (const collection of VISUAL_COLLECTIONS) {
    const before = baseline.visuals?.[collection] ?? {};
    const after = project.visuals?.[collection] ?? {};
    for (const id of new Set([...Object.keys(before), ...Object.keys(after)])) {
      if (JSON.stringify(withoutSource(before[id])) === JSON.stringify(withoutSource(after[id]))) continue;
      changes.push({ collection: 'visuals', id: `${collection}.${id}`, targetCollection: collection, targetId: id, type: !Object.hasOwn(before, id) ? 'added' : !Object.hasOwn(after, id) ? 'deleted' : 'modified', before: before[id] ?? null, after: after[id] ?? null });
    }
  }
  return changes;
}

export const diffProject = diffProjects;
