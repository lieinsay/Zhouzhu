"""与浏览器相同的数据契约和制作链规则；Python 服务独立执行校验。"""
import re
import sys
from .common import finite, integer, js_dumps, js_number

COLLECTIONS = ['items', 'recipes', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops', 'stations']
VISUAL_COLLECTIONS = ['items', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops']
ROW_SHAPES = {
    'items': dict(id='string', name='string', stack='number', color='color'),
    'recipes': {'id': 'string', 'product': 'string', 'yield': 'number', 'station': 'string', 'ingredients': 'ingredients'},
    'props': dict(id='string', item='string', width='number', depth='number', height='number', look='string', slots='number', station='nullable-string', light_m='number'),
    'pieces': dict(id='string', item='string', role='string', slot='string', material='string'),
    'buildings': dict(id='string', name='string', width_m='number', depth_m='number', plinth_m='number', wall_m='number', roof_m='number', levels='number', house='boolean'),
    'modules': dict(id='string', name='string', kind='string', size_x='number', size_y='number', size_z='number', mass='number', thrust='number', bay='number', cargo_deck='boolean', top_only='boolean', bottom_only='boolean', max_count='number', required='nullable-string', removable='boolean', enterable='boolean', furnish='boolean', look='string'),
    'deposits': dict(id='string', key='string', name='string', form='string', site='string', verb='string', product='string', gather_s='number', base_stock='number', refill_hours='number', look='string', color='color'),
    'scatters': {'id': 'string', 'key': 'string', 'name': 'string', 'verb': 'string', 'product': 'string', 'gather_s': 'number', 'yield': 'number', 'refill_hours': 'number', 'look': 'string', 'solid': 'boolean', 'radius': 'number', 'color': 'color'},
    'tools': dict(id='string', item='string', products='strings', speed='number'),
    'crops': {'id': 'string', 'name': 'string', 'seed': 'string', 'product': 'string', 'grow_factor': 'number', 'yield': 'number'},
    'stations': dict(id='string', name='string'),
}
STATIONS = ['HAND', 'WORKBENCH', 'KILN']
FALLBACK_ENUMS = {
    'PropLook': ['WORKBENCH', 'KILN', 'CHEST', 'TORCH', 'FENCE'],
    'PieceRole': ['FOUNDATION', 'FLOOR', 'WALL', 'DOOR', 'WINDOW', 'GABLE', 'ROOF', 'PILLAR', 'LADDER'],
    'PieceSlot': ['CELL', 'EDGE', 'CORNER'],
    'PartMaterial': ['STONE', 'WALL', 'FLOOR', 'WOOD', 'DARK_WOOD', 'ROOF', 'CLAY', 'CLOTH', 'STRAW', 'GRAIN', 'LACQUER', 'BRICK'],
    'ModuleKind': ['BRIDGE', 'STORAGE', 'SAIL', 'FLOATSTONE', 'DECK', 'LONG_SAIL', 'TALL_HOLD', 'WIDE_DECK'],
    'ModuleLook': ['HELM', 'CARGO', 'SAIL', 'FLOATSTONE', 'BARE'],
    'DepositForm': ['POINT', 'PATCH', 'FIELD'],
    'DepositLook': ['NONE', 'QUARRY', 'PIT', 'BAR', 'SLUICE', 'MINE', 'CRUST', 'SPRING', 'HOTSPRING', 'CAVE'],
    'ScatterLook': ['BROADLEAF', 'CONIFER', 'SHRUB', 'REEDS', 'BOULDER', 'FLOATSTONE', 'CLAY_BANK', 'GRAVEL_HEAP', 'PLACER', 'ORE_ROCK', 'CRUST'],
}
MIN_POSITIVE = float.fromhex('0x0.0000000000001p-1022')
MISSING = object()


def matches(pattern, value):
    return isinstance(value, str) and re.fullmatch(pattern, value) is not None


def assert_visual_structure(visuals=MISSING):
    if visuals is MISSING:
        return
    if not isinstance(visuals, dict):
        raise ValueError('visuals 必须是按集合和记录 ID 分组的对象')
    for collection, entries in visuals.items():
        if collection not in VISUAL_COLLECTIONS or not isinstance(entries, dict) or len(entries) > 10000:
            raise ValueError(f'visuals.{collection} 集合不受支持或结构错误')
        for identifier, visual in entries.items():
            path = f'visuals.{collection}.{identifier}'
            if not matches(r'[a-z][a-z0-9_]*', identifier) or not isinstance(visual, dict):
                raise ValueError(f'{path} 记录结构错误')
            for field, value in visual.items():
                if field in ('thumbnail', 'model'):
                    pattern = r'[a-zA-Z0-9][a-zA-Z0-9_:-]{0,127}' if field == 'model' else r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,127}'
                    if value is not None and not matches(pattern, value):
                        raise ValueError(f'{path}.{field} 必须是媒体库 ID 或 null，不能是 URL 或路径')
                elif field in ('scale', 'rotation', 'offset'):
                    if not isinstance(value, list) or len(value) != 3 or not all(finite(n) for n in value):
                        raise ValueError(f'{path}.{field} 必须是有限数值三元组')
                    valid = all(0 < n <= 100 for n in value) if field == 'scale' else all(abs(n) <= (36000 if field == 'rotation' else 100000) for n in value)
                    if not valid:
                        raise ValueError(f'{path}.{field} 超过允许范围')
                else:
                    raise ValueError(f'{path} 不支持字段 {field}')


def assert_project_structure(project):
    if not isinstance(project, dict) or type(project.get('schemaVersion')) is bool or project.get('schemaVersion') != 1:
        raise ValueError('项目必须是 schemaVersion: 1 的 JSON 对象')
    for key in project:
        if key not in {'schemaVersion', 'meta', 'visuals', *COLLECTIONS}:
            raise ValueError(f'不支持的项目字段 {key}')
    if not isinstance(project.get('meta'), dict):
        raise ValueError('项目缺少 meta 来源元数据')
    for collection in COLLECTIONS:
        rows = project.get(collection)
        if not isinstance(rows, list) or len(rows) > 10000:
            raise ValueError(f'{collection} 必须是数组且不超过 10000 行')
        for index, row in enumerate(rows):
            path = f'{collection}[{index}]'
            if not isinstance(row, dict):
                raise ValueError(f'{path} 必须是对象')
            shape = ROW_SHAPES[collection]
            for key in row:
                if key != 'source' and key not in shape:
                    raise ValueError(f'{path} 不支持字段 {key}')
            for field, kind in shape.items():
                value = row.get(field, MISSING)
                if kind == 'color':
                    valid = isinstance(value, list) and len(value) == 3 and all(finite(n) for n in value)
                elif kind == 'strings':
                    valid = isinstance(value, list) and len(value) <= 10000 and all(isinstance(n, str) for n in value)
                elif kind == 'ingredients':
                    valid = isinstance(value, list) and len(value) <= 10000 and all(isinstance(n, dict) and set(n) <= {'item', 'count'} and isinstance(n.get('item'), str) and finite(n.get('count')) for n in value)
                elif kind == 'nullable-string':
                    valid = value is None or isinstance(value, str)
                elif kind == 'number':
                    valid = finite(value)
                else:
                    valid = isinstance(value, str) if kind == 'string' else type(value) is bool
                if not valid:
                    raise ValueError(f'{path}.{field} 结构错误，期望 {kind}')
                if isinstance(value, str) and len(value.encode('utf-16-le', errors='surrogatepass')) // 2 > 4096:
                    raise ValueError(f'{path}.{field} 字符串过长')
            if 'source' in row:
                source = row['source']
                if not isinstance(source, dict) or not isinstance(source.get('path'), str) or not integer(source.get('line')) or source['line'] < 1:
                    raise ValueError(f'{path}.source 来源结构错误')
    meta = project['meta']
    if not isinstance(meta.get('sources'), list) or not all(isinstance(n, dict) and isinstance(n.get('path'), str) and matches(r'[a-f0-9]{64}', n.get('sha256')) for n in meta['sources']):
        raise ValueError('meta.sources 必须包含源码路径和 SHA-256')
    for field in ('rawItems', 'externalItems'):
        if not isinstance(meta.get(field), list) or not all(isinstance(n, str) for n in meta[field]):
            raise ValueError(f'meta.{field} 必须是物品 ID 数组')
    if 'enums' in meta and (not isinstance(meta['enums'], dict) or not all(isinstance(n, list) and all(isinstance(s, str) for s in n) for n in meta['enums'].values())):
        raise ValueError('meta.enums 必须是枚举名称到字符串数组的映射')
    assert_visual_structure(project.get('visuals', MISSING))


def validate_visuals(project, library=None):
    try:
        assert_visual_structure(project.get('visuals', MISSING))
    except ValueError as error:
        return [dict(severity='error', collection='project', id='', field='visuals', message=str(error))]
    issues = []
    thumbnails = {row['id'] for row in library.get('thumbnails', [])} if library is not None else None
    models = {row['id'] for row in library.get('models', [])} if library is not None else None
    for collection, entries in project.get('visuals', {}).items():
        ids = {row['id'] for row in project.get(collection, [])}
        for identifier, visual in entries.items():
            def issue(field, message):
                issues.append(dict(severity='error', collection=collection, id=identifier, field='visuals.' + field, message=message))
            if identifier not in ids:
                issue('id', f'视觉配置目标“{identifier}”不存在，可能已被删除')
            if visual.get('thumbnail') and thumbnails is not None and visual['thumbnail'] not in thumbnails:
                issue('thumbnail', f'缩略图“{visual["thumbnail"]}”不在工具媒体库中')
            if visual.get('model') and models is not None and visual['model'] not in models:
                issue('model', f'模型“{visual["model"]}”不在工具媒体库中')
    return issues


def validate_project(project, baseline=None, library=None):
    baseline = project if baseline is None else baseline
    issues = []
    def issue(collection, row, field, message, severity='error'):
        issues.append(dict(severity=severity, collection=collection, id=row['id'] if row else '', field=field, message=message))
    try:
        assert_project_structure(project)
    except (ValueError, TypeError, OverflowError) as error:
        issue('project', None, 'structure', str(error))
        return issues
    issues.extend(validate_visuals(project, library))
    item_ids = {row['id'] for row in project['items']}
    enums = {**FALLBACK_ENUMS, **baseline['meta'].get('enums', {})}
    def ref(collection, row, field, identifier, optional=False):
        if (identifier or not optional) and identifier not in item_ids:
            issue(collection, row, field, f'引用的物品“{identifier}”不存在，可能已被删除')
    def number_range(collection, row, field, minimum, maximum=sys.float_info.max, whole=False):
        value = row[field]
        if not finite(value) or value < minimum or value > maximum or (whole and not integer(value)):
            upper = '有限正数' if maximum == sys.float_info.max else js_number(maximum)
            issue(collection, row, field, f'{field} 应为{"整数" if whole else "有限数值"}，范围 {js_number(minimum)}～{upper}')
    def one_of(collection, row, field, options):
        if row[field] not in options:
            text = row[field] if isinstance(row[field], str) else js_dumps(row[field])
            issue(collection, row, field, f'不支持 {field} 值“{text}”，新增类型还需要对应的游戏实现')
    for collection in COLLECTIONS:
        seen = set()
        for row in project[collection]:
            identifier = row['id']
            if not matches(r'[a-z][a-z0-9_]*', identifier) and not (collection == 'stations' and identifier in STATIONS):
                issue(collection, row, 'id', 'ID 需以小写字母开头，仅含小写字母、数字和下划线')
            if identifier in seen:
                issue(collection, row, 'id', f'ID“{identifier}”重复')
            seen.add(identifier)
            if 'name' in row and not row['name'].strip():
                issue(collection, row, 'name', '显示名称不能为空')
            if 'color' in row and any(n < 0 or n > 1 for n in row['color']):
                issue(collection, row, 'color', '颜色分量应在 0～1 内')
    if len(project['stations']) != 3 or any(not any(row['id'] == key for row in project['stations']) for key in STATIONS):
        issue('stations', None, 'id', '制作设施固定为 HAND、WORKBENCH、KILN，不能新增或删除')
    for row in project['items']:
        number_range('items', row, 'stack', 1, 2147483647, True)
    for row in project['recipes']:
        ref('recipes', row, 'product', row['product'])
        number_range('recipes', row, 'yield', 1, 2147483647, True)
        one_of('recipes', row, 'station', STATIONS)
        if not 1 <= len(row['ingredients']) <= 3:
            issue('recipes', row, 'ingredients', '当前 RecipeDef 支持 1～3 种材料')
        inputs = set()
        for index, ingredient in enumerate(row['ingredients']):
            ref('recipes', row, f'ingredients.{index}.item', ingredient['item'])
            if not integer(ingredient['count']) or not 1 <= ingredient['count'] <= 2147483647:
                issue('recipes', row, f'ingredients.{index}.count', '材料份数需为 1～2147483647 的整数')
            if ingredient['item'] in inputs:
                issue('recipes', row, 'ingredients', f'材料“{ingredient["item"]}”重复，请合并份数')
            inputs.add(ingredient['item'])
            if ingredient['item'] == row['product']:
                issue('recipes', row, 'ingredients', '配方直接依赖自己的产物，无法作为制作入口')
    for row in project['props']:
        ref('props', row, 'item', row['item'])
        if row['id'] != row['item']:
            issue('props', row, 'id', '可放置物的 ID 必须与 item 相同')
        for field in ('width', 'depth', 'height'):
            number_range('props', row, field, MIN_POSITIVE)
        number_range('props', row, 'slots', 0, 2147483647, True)
        number_range('props', row, 'light_m', 0)
        one_of('props', row, 'look', enums['PropLook'])
        one_of('props', row, 'station', [None, 'WORKBENCH', 'KILN'])
    for row in project['pieces']:
        ref('pieces', row, 'item', row['item'])
        if row['id'] != row['item']:
            issue('pieces', row, 'id', '搭建件的 ID 必须与 item 相同')
        for field, name in [('role', 'PieceRole'), ('slot', 'PieceSlot'), ('material', 'PartMaterial')]:
            one_of('pieces', row, field, enums[name])
        expected = 'EDGE' if row['role'] in ('WALL', 'DOOR', 'WINDOW', 'GABLE') else 'CORNER' if row['role'] == 'PILLAR' else 'CELL'
        if row['slot'] != expected:
            issue('pieces', row, 'slot', f'{row["role"]} 的现有槽位规则要求 {expected}')
    for row in project['buildings']:
        for field in ('width_m', 'depth_m'):
            number_range('buildings', row, field, MIN_POSITIVE)
        for field in ('plinth_m', 'wall_m', 'roof_m'):
            number_range('buildings', row, field, 0)
        number_range('buildings', row, 'levels', 1, 2147483647, True)
        if row['house'] and row['wall_m'] <= 0:
            issue('buildings', row, 'wall_m', '房屋的墙高必须大于 0')
        if row['house'] and row['roof_m'] <= 0:
            issue('buildings', row, 'roof_m', '现有房屋外壳要求正屋顶高度')
    module_kinds = set()
    for index, row in enumerate(project['modules']):
        one_of('modules', row, 'kind', enums['ModuleKind'])
        one_of('modules', row, 'look', enums['ModuleLook'])
        if row['kind'] in module_kinds:
            issue('modules', row, 'kind', f'模块枚举 {row["kind"]} 重复；现有表按枚举一一对应')
        module_kinds.add(row['kind'])
        expected = enums['ModuleKind'][index] if index < len(enums['ModuleKind']) else '现有枚举范围内的定义'
        if expected != row['kind']:
            issue('modules', row, 'kind', f'当前游戏按枚举下标读取模块，第 {index + 1} 行必须为 {expected}')
        for field, limit in [('size_x', 3), ('size_y', 3), ('size_z', 7)]:
            number_range('modules', row, field, 1, limit, True)
        if row['size_x'] * row['size_y'] * row['size_z'] > 12:
            issue('modules', row, 'size_x', '单模块占格不能超过现有空艇的 12 节上限')
        number_range('modules', row, 'mass', MIN_POSITIVE)
        number_range('modules', row, 'thrust', 0)
        for field in ('bay', 'max_count'):
            number_range('modules', row, field, 0, 2147483647, True)
        if row['top_only'] and row['bottom_only']:
            issue('modules', row, 'top_only', '同时要求顶层与底层，仅能用于单层布局，请核对', 'warning')
    scatter_looks = set()
    for index, row in enumerate(project['scatters']):
        if row['look'] in scatter_looks:
            issue('scatters', row, 'look', f'散布类型 {row["look"]} 重复；现有表按枚举一一对应')
        scatter_looks.add(row['look'])
        expected = enums['ScatterLook'][index] if index < len(enums['ScatterLook']) else '现有枚举范围内的定义'
        if expected != row['look']:
            issue('scatters', row, 'look', f'当前游戏按枚举下标读取散布物，第 {index + 1} 行必须为 {expected}')
    for look in enums['ScatterLook']:
        if look not in scatter_looks:
            issue('scatters', {'id': look}, 'look', f'现有散布枚举 {look} 缺少定义，游戏按枚举索引读取')
    for kind in enums['ModuleKind']:
        if kind not in module_kinds:
            issue('modules', {'id': kind}, 'kind', f'现有模块枚举 {kind} 缺少定义，游戏按枚举索引读取')
    for collection in ('deposits', 'scatters'):
        for row in project[collection]:
            if row['id'] != row['key']:
                issue(collection, row, 'id', '资源记录的 ID 必须与 key 相同')
            ref(collection, row, 'product', row['product'], True)
            for field in ('gather_s', 'refill_hours'):
                number_range(collection, row, field, 0)
            if collection == 'deposits':
                one_of(collection, row, 'form', enums['DepositForm'])
                one_of(collection, row, 'look', enums['DepositLook'])
                number_range(collection, row, 'base_stock', 0, 2147483647, True)
                if row['form'] == 'FIELD':
                    if row['gather_s'] <= 0:
                        issue(collection, row, 'gather_s', '采场每份工时必须大于 0')
                    if row['base_stock'] <= 0:
                        issue(collection, row, 'base_stock', '采场基础存量必须大于 0')
                elif row['product']:
                    issue(collection, row, 'product', '点状地标与片状资源不会通过采场表产出；片状采集需配置散布物', 'warning')
            else:
                one_of(collection, row, 'look', enums['ScatterLook'])
                number_range(collection, row, 'yield', 1, 2147483647, True)
                number_range(collection, row, 'radius', MIN_POSITIVE)
                if row['gather_s'] <= 0:
                    issue(collection, row, 'gather_s', '散布采集每份工时必须大于 0')
    for row in project['tools']:
        ref('tools', row, 'item', row['item'])
        if row['id'] != row['item']:
            issue('tools', row, 'id', '工具的 ID 必须与 item 相同')
        number_range('tools', row, 'speed', MIN_POSITIVE)
        if not row['products']:
            issue('tools', row, 'products', '工具至少应有一种对口产物')
        if len(set(row['products'])) != len(row['products']):
            issue('tools', row, 'products', '对口产物重复')
        for identifier in row['products']:
            ref('tools', row, 'products', identifier)
    for row in project['crops']:
        ref('crops', row, 'seed', row['seed'])
        ref('crops', row, 'product', row['product'])
        number_range('crops', row, 'grow_factor', MIN_POSITIVE)
        number_range('crops', row, 'yield', 1, 2147483647, True)
    available = {key for key in baseline['meta'].get('externalItems', []) if key in item_ids}
    available.update(row['product'] for row in project['deposits'] if row['form'] == 'FIELD' and row['product'])
    available.update(row['product'] for row in project['scatters'] if row['product'])
    stations = {'HAND'}
    while True:
        before = (len(available), len(stations))
        for row in project['props']:
            if row['station'] and row['item'] in available:
                stations.add(row['station'])
        for row in project['crops']:
            if row['seed'] in available and row['product'] in item_ids:
                available.add(row['product'])
        for row in project['recipes']:
            if row['ingredients'] and row['station'] in stations and all(n['item'] in available for n in row['ingredients']) and row['product'] in item_ids:
                available.add(row['product'])
        if before == (len(available), len(stations)):
            break
    for row in project['recipes']:
        if row['station'] not in stations and row['station'] in STATIONS:
            issue('recipes', row, 'station', f'制作设施 {row["station"]} 无法从现有获取入口自举；核对设施配方及放置定义')
        blocked = list(dict.fromkeys(n['item'] for n in row['ingredients'] if n['item'] in item_ids and n['item'] not in available))
        if blocked:
            issue('recipes', row, 'ingredients', f'材料无法从现有获取入口到达：{"、".join(blocked)}（可能存在闭环或缺少来源）')
    for row in project['items']:
        if row['id'] not in available and not any(r['product'] == row['id'] for r in project['recipes']):
            issue('items', row, 'id', '尚未配置制作、采集或作物来源；该物品不能通过本草稿的获取链得到', 'warning')
    for row in project['modules']:
        if not any(r['id'] == row['id'] for r in baseline.get('modules', [])):
            issue('modules', row, 'kind', '新增空艇模块还需要更新游戏枚举、网格与布局逻辑；草稿不会安装这些行为', 'warning')
    for collection in ('deposits', 'scatters'):
        for row in project[collection]:
            if baseline is not project and not any(r['id'] == row['id'] for r in baseline.get(collection, [])):
                issue(collection, row, 'key', '新增资源键还需生成器/散布规则提供实例；本工具仅保存配置提案', 'warning')
    if baseline is not project and any(not any(saved.get('path') == source['path'] and saved.get('sha256') == source['sha256'] for saved in project['meta']['sources']) for source in baseline['meta']['sources']):
        issue('project', None, 'sources', '草稿的源文件指纹与当前代码不同；先核对变更报告，再决定是否按当前基线重新整理草稿', 'warning')
    return issues


def without_source(value):
    if isinstance(value, list):
        return [without_source(row) for row in value]
    if isinstance(value, dict):
        return {key: without_source(value[key]) for key in sorted(value) if key != 'source'}
    return value


def diff_projects(baseline, project):
    changes = []
    for collection in COLLECTIONS:
        before = {row['id']: row for row in baseline.get(collection, [])}
        after = {row['id']: row for row in project.get(collection, [])}
        for identifier, row in before.items():
            if identifier not in after:
                changes.append(dict(collection=collection, id=identifier, type='deleted', before=row, after=None))
            elif js_dumps(without_source(row)) != js_dumps(without_source(after[identifier])):
                changes.append(dict(collection=collection, id=identifier, type='modified', before=row, after=after[identifier]))
        for identifier, row in after.items():
            if identifier not in before:
                changes.append(dict(collection=collection, id=identifier, type='added', before=None, after=row))
    for collection in VISUAL_COLLECTIONS:
        before = baseline.get('visuals', {}).get(collection, {})
        after = project.get('visuals', {}).get(collection, {})
        for identifier in dict.fromkeys([*before, *after]):
            if identifier not in before or identifier not in after or js_dumps(without_source(before.get(identifier))) != js_dumps(without_source(after.get(identifier))):
                changes.append(dict(collection='visuals', id=f'{collection}.{identifier}', targetCollection=collection, targetId=identifier, type='added' if identifier not in before else 'deleted' if identifier not in after else 'modified', before=before.get(identifier), after=after.get(identifier)))
    return changes
