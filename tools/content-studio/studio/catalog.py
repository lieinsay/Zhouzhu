"""只读导入游戏 C++ 字面量表。遇到未知表达式或结构变化立即报错。"""
import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

SOURCE_PATHS = [
    'src/crops.cpp', 'src/crops.h', 'src/recipes.cpp', 'src/recipes.h',
    'src/placeables.cpp', 'src/placeables.h', 'src/build_pieces.cpp', 'src/build_pieces.h',
    'src/buildings.cpp', 'src/buildings.h', 'src/ship_layout.cpp', 'src/ship_layout.h',
    'src/deposit_kinds.cpp', 'src/deposit_kinds.h', 'src/building_parts.h',
]


@dataclass
class Node:
    value: object
    pos: int


class Parser:
    def __init__(self, text, path, at):
        self.text, self.path, self.at = text, path, at

    def error(self, message):
        line = self.text.count('\n', 0, self.at) + 1
        raise ValueError(f'{self.path}:{line}：{message}')

    def skip(self):
        while self.at < len(self.text):
            rest = self.text[self.at:]
            match = re.match(r'[\s\ufeff]+', rest)
            if match:
                self.at += len(match[0])
            elif rest.startswith('//'):
                end = self.text.find('\n', self.at)
                self.at = len(self.text) if end < 0 else end
            elif rest.startswith('/*'):
                end = self.text.find('*/', self.at + 2)
                if end < 0:
                    self.error('未结束的注释')
                self.at = end + 2
            else:
                return

    def value(self):
        self.skip()
        pos = self.at
        rest = self.text[pos:]
        if rest.startswith('{'):
            self.at += 1
            values = []
            self.skip()
            while self.at < len(self.text) and self.text[self.at] != '}':
                values.append(self.value())
                self.skip()
                if self.at < len(self.text) and self.text[self.at] == '}':
                    break
                if self.at >= len(self.text) or self.text[self.at] != ',':
                    self.error('初始化器缺少逗号，或使用了尚不支持的表达式')
                self.at += 1
                self.skip()
            if self.at >= len(self.text):
                self.error('未结束的初始化列表')
            self.at += 1
            return Node(values, pos)
        if rest.startswith('"'):
            self.at += 1
            result = ''
            escapes = {'"': '"', '\\': '\\', 'n': '\n', 'r': '\r', 't': '\t'}
            while self.at < len(self.text):
                char = self.text[self.at]
                self.at += 1
                if char == '"':
                    return Node(result, pos)
                if char == '\\':
                    char = self.text[self.at:self.at + 1]
                    self.at += 1
                    if char not in escapes:
                        self.error(f'尚不支持字符串转义 \\{char}')
                    result += escapes[char]
                elif char in '\r\n':
                    self.error('字符串中出现未转义换行')
                else:
                    result += char
            self.error('未结束的字符串')
        number = re.match(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?\b', rest)
        if number:
            self.at += len(number[0])
            value = float(number[0].rstrip('fF'))
            return Node(int(value) if value.is_integer() else value, pos)
        identifier = re.match(r'[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*', rest)
        if identifier:
            word = identifier[0]
            self.at += len(word)
            if word == 'int':
                self.skip()
                if self.text[self.at:self.at + 1] != '(':
                    self.error('仅支持 int(枚举值)')
                self.at += 1
                inner = self.value()
                self.skip()
                if self.text[self.at:self.at + 1] != ')':
                    self.error('未结束的 int 转换')
                self.at += 1
                return Node(inner.value, pos)
            if word in ('true', 'false', 'nullptr'):
                return Node({'true': True, 'false': False, 'nullptr': None}[word], pos)
            if '::' in word:
                return Node({'enum': word}, pos)
        self.error(f'尚不支持的初始化器：{rest[:50]}')


def parse_initializer(text, declaration, path='source.cpp'):
    match = re.search(r'\b' + re.escape(declaration) + r'\s*\[\s*\]\s*=\s*', text)
    if not match:
        raise ValueError(f'{path}：找不到初始化表 {declaration}')
    parser = Parser(text, path, match.end())
    node = parser.value()
    parser.skip()
    if text[parser.at:parser.at + 1] != ';' or not isinstance(node.value, list):
        parser.error('数组声明格式不受支持')
    return node.value


def fields(node, counts, path):
    if not isinstance(node.value, list) or len(node.value) not in counts:
        raise ValueError(f'{path}：初始化行字段数错误（期望 {"/".join(map(str, counts))}）')
    return [value.value for value in node.value]


def enum(value, name, path):
    if not isinstance(value, dict) or not value.get('enum', '').startswith(name + '::'):
        raise ValueError(f'{path}：期望 {name} 枚举')
    return value['enum'][len(name) + 2:]


def enum_values(text, name, path):
    match = re.search(r'enum\s+class\s+' + name + r'\b[^{}]*\{([\s\S]*?)\}', text)
    if not match:
        raise ValueError(f'{path}：找不到枚举 {name}')
    body = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', match[1])
    values = [word.strip() for word in body.split(',') if word.strip()]
    if any(not re.fullmatch(r'[A-Z][A-Z_]*', word) for word in values):
        raise ValueError(f'{path}：{name} 的枚举声明格式尚不支持')
    return [word for word in values if word != 'COUNT']


def load_catalog(repository_root):
    source, hashes = {}, []
    for path in SOURCE_PATHS:
        raw = (Path(repository_root) / path).read_bytes()
        try:
            source[path] = raw.decode('utf-8')
        except UnicodeDecodeError as error:
            raise ValueError(f'{path}：源码不是完整有效的 UTF-8') from error
        hashes.append({'path': path, 'sha256': hashlib.sha256(raw).hexdigest()})
    if not re.search(r'int\s+levels\s*=\s*1\s*;', source['src/buildings.h']) or not re.search(r'bool\s+house\s*=\s*true\s*;', source['src/buildings.h']):
        raise ValueError('src/buildings.h：BuildingDef 默认字段已变化，请更新导入器后再读取省略字段的行')

    def rows(path, declaration, count, names, enum_fields=None, defaults=()):
        result = []
        for node in parse_initializer(source[path], declaration, path):
            line = source[path].count('\n', 0, node.pos) + 1
            location = f'{path}:{line}'
            values = fields(node, count, location)
            values.extend(defaults[len(values) - min(count):])
            row = dict(zip(names.split(), values))
            for key, name in (enum_fields or {}).items():
                row[key] = enum(row[key], name, location)
            row['source'] = {'path': path, 'line': line}
            result.append(row)
        return result

    catalog = {'schemaVersion': 1}
    catalog['items'] = rows('src/crops.cpp', 'ITEMS', [6], 'id name stack r g b')
    for row in catalog['items']:
        row['color'] = [row.pop(key) for key in ('r', 'g', 'b')]
    catalog['recipes'] = rows('src/recipes.cpp', 'RECIPES', [5], 'id product yield station ingredients', {'station': 'Station'})
    for row in catalog['recipes']:
        row['ingredients'] = [dict(zip(('item', 'count'), fields(node, [2], '配方材料'))) for node in row['ingredients']]
    catalog['props'] = rows('src/placeables.cpp', 'PROPS', [8], 'item width depth height look slots station light_m', {'look': 'PropLook'})
    for row in catalog['props']:
        row['id'] = row['item']
        row['station'] = None if row['station'] == -1 else enum(row['station'], 'Station', '可放置物')
    catalog['pieces'] = rows('src/build_pieces.cpp', 'pieces::DEFS', [4], 'item role slot material', {'role': 'PieceRole', 'slot': 'PieceSlot', 'material': 'PartMaterial'})
    for row in catalog['pieces']:
        row['id'] = row['item']
    catalog['buildings'] = rows('src/buildings.cpp', 'DEFS', [7, 8, 9], 'id name width_m depth_m plinth_m wall_m roof_m levels house', defaults=(1, True))
    catalog['modules'] = rows('src/ship_layout.cpp', 'DEFS', [18], 'kind id name size_x size_y size_z mass thrust bay cargo_deck top_only bottom_only max_count required removable enterable furnish look', {'kind': 'ModuleKind', 'look': 'ModuleLook'})
    catalog['deposits'] = rows('src/deposit_kinds.cpp', 'KINDS', [13], 'key name form site verb product gather_s base_stock refill_hours look r g b', {'form': 'DepositForm', 'look': 'DepositLook'})
    catalog['scatters'] = rows('src/deposit_kinds.cpp', 'SCATTER', [13], 'key name verb product gather_s yield refill_hours look solid radius r g b', {'look': 'ScatterLook'})
    for collection in ('deposits', 'scatters'):
        for row in catalog[collection]:
            row['id'] = row['key']
            row['color'] = [row.pop(key) for key in ('r', 'g', 'b')]
    catalog['tools'] = rows('src/crops.cpp', 'TOOLS', [3], 'item products speed')
    for row in catalog['tools']:
        row['id'], row['products'] = row['item'], row['products'].split(',')
    catalog['crops'] = rows('src/crops.cpp', 'CROPS', [6], 'id name seed product grow_factor yield')
    catalog['stations'] = [{'id': key, 'name': name} for key, name in [('HAND', '随身'), ('WORKBENCH', '工作台'), ('KILN', '窑')]]
    ores = [dict(zip(('subtype', 'item'), fields(node, [2], 'ORES'))) for node in parse_initializer(source['src/deposit_kinds.cpp'], 'ORES')]
    raw_items = [node.value for node in parse_initializer(source['src/deposit_kinds.cpp'], 'RAW_ITEMS')]
    products = {row['product'] for row in catalog['recipes']}
    specs = [('Station', 'src/recipes.h'), ('PropLook', 'src/placeables.h'), ('PieceRole', 'src/build_pieces.h'), ('PieceSlot', 'src/build_pieces.h'), ('PartMaterial', 'src/building_parts.h'), ('ModuleKind', 'src/ship_layout.h'), ('ModuleLook', 'src/ship_layout.h'), ('DepositForm', 'src/deposit_kinds.h'), ('DepositLook', 'src/deposit_kinds.h'), ('ScatterLook', 'src/deposit_kinds.h')]
    catalog['meta'] = {
        'sources': hashes, 'rawItems': raw_items,
        'externalItems': [row['id'] for row in catalog['items'] if row['id'] not in products],
        'ores': ores, 'enums': {name: enum_values(source[path], name, path) for name, path in specs},
        'notes': ['直接导入当前 C++ 常量表；工具草稿不会被游戏加载。', '资源点的空 product 可能表示地标或由既有子类规则决定；工具不补猜采集产物。', 'externalItems 是基线中无制作配方的物品入口（采集、耕作、贸易、改地形等），不表示每样都可野外采集。', '新增 enum、机械行为、船模块类型及资源子类还需要游戏代码实现；草稿仅表达提案。'],
    }
    return catalog
