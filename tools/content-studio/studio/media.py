"""现有 PNG/网格清单和仅属于工具的 OBJ 模型库。"""
import copy
import hashlib
import math
import re
import threading
import uuid
from pathlib import Path
from .common import StudioError, atomic_json, read_json, timestamp
from .validation import matches

MAX_OBJ_BYTES = 5 * 1024 * 1024
ID = r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,127}'
MODEL_ID = r'[a-zA-Z0-9][a-zA-Z0-9_:-]{0,127}'
UUID = r'[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}'


def fail(message, status=422):
    raise StudioError(status, message)


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def subtract(a, b):
    return [x-y for x, y in zip(a, b)]


def parse_obj(content):
    if not isinstance(content, str) or len(content.encode('utf-8')) > MAX_OBJ_BYTES:
        fail('OBJ 内容必须是最多 5 MiB 的文本')
    if '\0' in content:
        fail('OBJ 含有非法空字节')
    vertices, normals, texcoords, faces = [], [], [], []
    triangle_count = polygon_checks = 0
    def number(token, line):
        try:
            value = float(token)
        except (ValueError, TypeError):
            value = math.nan
        if not math.isfinite(value) or abs(value) > 1000000:
            fail(f'OBJ 第 {line} 行坐标不是允许范围内的有限数值')
        return value
    def index(token, count, line, kind):
        if not re.fullmatch(r'-?\d+', token):
            fail(f'OBJ 第 {line} 行 {kind} 索引格式错误')
        value = int(token)
        result = value-1 if value > 0 else count+value
        if value == 0 or not 0 <= result < count:
            fail(f'OBJ 第 {line} 行 {kind} 索引越界')
        return result
    for line, raw in enumerate(content.splitlines(), 1):
        text = raw.split('#', 1)[0].strip()
        if not text:
            continue
        op, *tokens = text.split()
        if op in ('v', 'vn'):
            if not 3 <= len(tokens) <= (7 if op == 'v' else 3):
                fail(f'OBJ 第 {line} 行 {op} 需要三维坐标')
            values = [number(token, line) for token in tokens]
            if op == 'v' and len(values) == 4 and values[3] != 1:
                fail(f'OBJ 第 {line} 行暂不支持非 1 的齐次坐标')
            (vertices if op == 'v' else normals).append(values[:3])
            if len(vertices) > 100000 or len(normals) > 100000:
                fail('OBJ 顶点/法线数超过 100000')
        elif op == 'vt':
            if not 1 <= len(tokens) <= 3:
                fail(f'OBJ 第 {line} 行纹理坐标格式错误')
            texcoords.append([number(token, line) for token in tokens])
            if len(texcoords) > 100000:
                fail('OBJ 纹理坐标数超过 100000')
        elif op == 'f':
            if not 3 <= len(tokens) <= 1024:
                fail(f'OBJ 第 {line} 行面需含 3～1024 个顶点')
            corners = []
            for token in tokens:
                parts = token.split('/')
                if len(parts) > 3:
                    fail(f'OBJ 第 {line} 行面索引格式错误')
                vertex = index(parts[0], len(vertices), line, '顶点')
                if len(parts) >= 2 and parts[1]:
                    index(parts[1], len(texcoords), line, '纹理')
                normal = index(parts[2], len(normals), line, '法线') if len(parts) == 3 and parts[2] else None
                corners.append((vertex, normal))
            if len(corners) > 3:
                polygon_checks += len(corners)**2
                if polygon_checks > 5000000:
                    fail('OBJ 多边形复杂度过高，请先在建模软件中三角化再上传')
                points = [vertices[corner[0]] for corner in corners]
                anchor, plane = points[0], None
                for i in range(1, len(points)-1):
                    candidate = cross(subtract(points[i], anchor), subtract(points[i+1], anchor))
                    length = math.hypot(*candidate)
                    if length > 1e-12:
                        plane = [value/length for value in candidate]
                        break
                if plane is None:
                    fail(f'OBJ 第 {line} 行多边形面积为零')
                span = max(math.hypot(*subtract(point, anchor)) for point in points)
                tolerance = max(1e-12, span*span*1e-8)
                for point in points:
                    if abs(sum(v*n for v,n in zip(subtract(point, anchor), plane))) > max(1e-8, span*1e-5):
                        fail(f'OBJ 第 {line} 行多边形不共面，请先在建模软件中三角化再上传')
                for i, start in enumerate(points):
                    edge = subtract(points[(i+1) % len(points)], start)
                    for point in points:
                        if sum(v*n for v,n in zip(cross(edge, subtract(point, start)), plane)) < -tolerance:
                            fail(f'OBJ 第 {line} 行是凹面或自交多边形，请先在建模软件中三角化再上传')
            triangle_count += len(corners)-2
            if triangle_count > 100000:
                fail('OBJ 三角面数超过 100000')
            faces.extend((corners[0], corners[i], corners[i+1]) for i in range(1, len(corners)-1))
        elif op not in ('o', 'g', 's', 'usemtl', 'mtllib'):
            fail(f'OBJ 第 {line} 行暂不支持指令 {op}，仅支持多边形几何')
    if not vertices or not faces:
        fail('OBJ 至少需要三个顶点和一个面')
    positions, output_normals, colors, indices = [], [], [], []
    bounds = {'min': [math.inf]*3, 'max': [-math.inf]*3}
    for face in faces:
        a, b, c = [vertices[corner[0]] for corner in face]
        product = cross(subtract(b, a), subtract(c, a))
        length = math.hypot(*product)
        if length <= 1e-12:
            continue
        flat_normal = [value/length for value in product]
        for vertex, normal in face:
            point = vertices[vertex]
            for axis, value in enumerate(point):
                bounds['min'][axis] = min(bounds['min'][axis], value)
                bounds['max'][axis] = max(bounds['max'][axis], value)
            given = flat_normal if normal is None else normals[normal]
            size = math.hypot(*given)
            if size <= 1e-12:
                fail('OBJ 含有零长度法线')
            positions.extend(point)
            output_normals.extend(value/size for value in given)
            colors.extend([0.62, 0.55, 0.43, 1])
            indices.append(len(indices))
    if not indices:
        fail('OBJ 不包含有效面积的三角面')
    return dict(schemaVersion=1, winding='ccw', units='m', parts=[dict(positions=positions, normals=output_normals, color=[0.62, 0.55, 0.43], colors=colors, indices=indices)], bounds=bounds, triangleCount=len(indices)//3)


class MediaLibrary:
    def __init__(self, public_directory, workspace_directory):
        self.public_directory = Path(public_directory).resolve()
        self.workspace = Path(workspace_directory).resolve()
        self.directory = self.workspace / 'assets'
        self.file = self.directory / 'models.json'
        self.lock = threading.RLock()

    def json(self, path, fallback):
        try:
            return read_json(path, fallback)
        except (ValueError, OSError) as error:
            raise StudioError(500, f'媒体库 {path.name} 无法读取：{error}') from error

    def get_assets(self, project):
        directory = self.public_directory / 'assets/thumbnails'
        available = {file.name for file in directory.iterdir()} if directory.exists() else set()
        manifest = self.json(directory / 'manifest.json', None)
        rows = manifest if isinstance(manifest, list) else manifest.get('thumbnails') if isinstance(manifest, dict) else None
        if manifest is not None and not isinstance(rows, list):
            fail('缩略图 manifest 必须包含 thumbnails 数组', 500)
        thumbnails = []
        if rows is not None:
            for row in rows:
                if not isinstance(row, dict) or not matches(ID, row.get('id')) or not matches(ID, row.get('itemId')):
                    fail('缩略图 manifest 的 ID 格式错误', 500)
                filename = row.get('filename')
                if filename is None:
                    url = row.get('url', '')
                    filename = url[len('/assets/thumbnails/'):] if isinstance(url, str) and url.startswith('/assets/thumbnails/') else row['id']+'-v1.png'
                if not matches(r'[a-zA-Z0-9][a-zA-Z0-9_.-]*\.png', filename):
                    fail('缩略图 manifest 文件名必须是库内 PNG', 500)
                if filename in available:
                    thumbnails.append({**row, 'filename': filename, 'url': '/assets/thumbnails/'+filename, 'origin': 'imagegen'})
        else:
            for item in project['items']:
                filename = next((name for name in (item['id']+'-v1.png', item['id']+'.png') if name in available), None)
                if filename:
                    thumbnails.append(dict(id=item['id'], itemId=item['id'], name=item['name'], filename=filename, url='/assets/thumbnails/'+filename, origin='imagegen'))
        model_manifest = self.json(self.public_directory / 'assets/models/manifest.json', {'models': [], 'missingItems': []})
        if not isinstance(model_manifest, dict) or not isinstance(model_manifest.get('models'), list):
            fail('模型 manifest 必须包含 models 数组', 500)
        models = copy.deepcopy(model_manifest['models'])
        for row in models:
            if not isinstance(row, dict) or not matches(MODEL_ID, row.get('id')) or row.get('format') != 'mesh-json' or not matches(r'/assets/models/[a-zA-Z0-9][a-zA-Z0-9_.-]*\.json', row.get('url')):
                fail('模型 manifest 的 ID、格式或库内 URL 错误', 500)
        models.extend(self.read_uploaded()['models'])
        for kind, values in [('缩略图', thumbnails), ('模型', models)]:
            if len({row['id'] for row in values}) != len(values):
                fail(f'{kind} manifest 存在重复 ID', 500)
        covered = {row['itemId'] for row in thumbnails}
        if hasattr(self, 'art'):
            formal = self.art.thumbnails()
            formal_ids = {row['id'] for row in formal}
            thumbnails = formal + [row for row in thumbnails if row['id'] not in formal_ids]
            covered.update(row['itemId'] for row in thumbnails)
        return dict(thumbnails=thumbnails, models=models, missingItems=model_manifest.get('missingItems', []), missingThumbnails=[{'id': row['id'], 'name': row['name']} for row in project['items'] if row['id'] not in covered])

    def read_uploaded(self):
        if not self.directory.resolve().is_relative_to(self.workspace):
            fail('不允许访问媒体库外的文件', 403)
        with self.lock:
            data = self.json(self.file, {'format': 'zhouzhu-model-library/v1', 'models': []})
            if not isinstance(data, dict) or data.get('format') != 'zhouzhu-model-library/v1' or not isinstance(data.get('models'), list):
                fail('上传模型库清单损坏，已保留原文件', 500)
            for row in data['models']:
                if not isinstance(row, dict) or not matches(UUID, row.get('id')) or row.get('url') != '/api/models/'+row['id'] or row.get('kind') != 'imported' or row.get('format') != 'mesh-json':
                    fail('上传模型库清单损坏，已保留原文件', 500)
            return data

    def upload(self, data):
        if not isinstance(data, dict) or set(data) - {'name', 'filename', 'content'} or not isinstance(data.get('name'), str) or not data['name'].strip() or len(data['name']) > 120 or not isinstance(data.get('filename'), str) or not data['filename'].lower().endswith('.obj') or len(data['filename']) > 255 or re.search(r'[\\/\0]', data['filename']):
            fail('上传格式为 {name, filename: "模型.obj", content: "OBJ 文本"}', 400)
        mesh = parse_obj(data.get('content'))
        identifier = str(uuid.uuid4())
        model = dict(id=identifier, name=data['name'].strip(), filename=data['filename'], url='/api/models/'+identifier, format='mesh-json', kind='imported', usage='custom', itemIds=[], triangleCount=mesh['triangleCount'], uploadedAt=timestamp(), source={'filename': data['filename'], 'sha256': hashlib.sha256(data['content'].encode('utf-8')).hexdigest()}, notes=['仅导入 OBJ 三角面或凸共面多边形几何；凹面需先三角化；MTL、贴图和动画未导入。'])
        with self.lock:
            library = self.read_uploaded()
            path = self.directory / (identifier+'.json')
            atomic_json(path, {**mesh, 'id': identifier, 'name': model['name']})
            try:
                atomic_json(self.file, {**library, 'models': [*library['models'], model]})
            except Exception:
                path.unlink(missing_ok=True)
                raise
        return model

    def read_model(self, identifier):
        if not matches(UUID, identifier):
            fail('上传模型 ID 不存在', 404)
        with self.lock:
            if not any(row['id'] == identifier for row in self.read_uploaded()['models']):
                fail('上传模型不存在', 404)
            path = (self.directory / (identifier+'.json')).resolve()
            if not path.is_relative_to(self.directory.resolve()):
                fail('不允许访问媒体库外的文件', 403)
            if not path.exists():
                fail('上传模型文件缺失', 404)
            return self.json(path, None)
