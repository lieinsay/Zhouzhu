"""正式美术版本与独立应用。绝不读取或写入内容草稿 project.visuals。"""
import copy
import hashlib
import re
import threading
import uuid
from pathlib import Path
from .common import StudioError, atomic_json, digest, read_json, timestamp

ASSET_ID = r'(item|module|building|furniture)\.[a-z][a-z0-9_]*'
VERSION = r'v[1-9][0-9]*'
EMPTY = {'schemaVersion': 1, 'bindings': {}}


class ArtLibrary:
    def __init__(self, repository_root, workspace_directory):
        self.root = Path(repository_root).resolve()
        self.registry_file = self.root / 'assets/art/registry.json'
        self.active_file = self.root / 'game/art/bindings.json'
        self.draft_file = Path(workspace_directory) / 'art-draft.json'
        self.lock = threading.RLock()

    def file(self, relative):
        if not isinstance(relative, str) or not relative or '\\' in relative or ':' in relative:
            raise StudioError(422, '美术文件必须是仓库内相对路径')
        path = (self.root / relative).resolve()
        if not path.is_relative_to(self.root) or '..' in relative.split('/'):
            raise StudioError(403, '不允许访问美术库外文件')
        return path

    def registry(self):
        registry = read_json(self.registry_file, {'schemaVersion': 1, 'assets': []})
        if registry.get('schemaVersion') != 1 or not isinstance(registry.get('assets'), list):
            raise StudioError(500, '正式美术登记格式错误')
        ids = set()
        for asset in registry['assets']:
            if not re.fullmatch(ASSET_ID, asset.get('id', '')) or asset['id'] in ids:
                raise StudioError(500, '正式美术身份重复或格式错误')
            ids.add(asset['id'])
            versions = asset.get('versions', [])
            if len({v['version'] for v in versions}) != len(versions) or any(not re.fullmatch(VERSION, v['version']) for v in versions):
                raise StudioError(500, '正式美术版本重复或格式错误')
        return registry

    def snapshot(self):
        with self.lock:
            registry = self.registry()
            active = read_json(self.active_file, EMPTY)
            saved = read_json(self.draft_file, None)
            pending = saved['bindings'] if saved else {key: value['version'] for key, value in active['bindings'].items()}
            base = saved['baseRevision'] if saved else digest(active)
            revision = digest({'registry': registry, 'active': active, 'draft': saved})
            return dict(registry=registry, active=active, pending=pending, revision=revision,
                        sourceChanged=base != digest(active), hasDraft=saved is not None)

    def version(self, registry, asset_id, version):
        asset = next((a for a in registry['assets'] if a['id'] == asset_id), None)
        value = next((v for v in asset['versions'] if v['version'] == version), None) if asset else None
        if value is None:
            raise StudioError(422, '未登记的美术身份或版本')
        return asset, value

    def validate_bindings(self, registry, bindings):
        if not isinstance(bindings, dict):
            raise StudioError(422, 'bindings 必须是美术身份到版本的映射')
        from .catalog import load_catalog
        catalog = load_catalog(self.root)
        known = {namespace+'.'+row['id'] for namespace, collection in
                 [('item', 'items'), ('module', 'modules'), ('building', 'buildings')]
                 for row in catalog[collection]}
        for asset_id, version in bindings.items():
            if asset_id not in known:
                raise StudioError(422, '美术绑定未对应当前游戏定义；新增命名空间需先接入其定义表')
            _, value = self.version(registry, asset_id, version)
            if value.get('agentReview', {}).get('status') != 'accepted':
                raise StudioError(422, f'{asset_id}/{version} 尚未通过代理审图')
            for key in ('original', 'runtime'):
                info = value[key]
                path = self.file(info['path'])
                if key == 'runtime' and not info['path'].startswith('game/art/'):
                    raise StudioError(422, '游戏图片必须位于 game/art 内')
                if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != info['sha256']:
                    raise StudioError(422, f'{asset_id}/{version} 的{key}文件缺失或指纹变化')

    def stage(self, data):
        with self.lock:
            state = self.snapshot()
            if not isinstance(data, dict) or set(data) != {'revision', 'bindings'}:
                raise StudioError(400, '美术暂存格式为 {revision, bindings}')
            if data['revision'] != state['revision'] or state['sourceChanged']:
                raise StudioError(409, '美术登记、应用或草稿已变化，请刷新后核对')
            self.validate_bindings(state['registry'], data['bindings'])
            atomic_json(self.draft_file, dict(schemaVersion=1, baseRevision=digest(state['active']), bindings=data['bindings']))
            return self.snapshot()

    def apply(self, data):
        with self.lock:
            self.active_file.parent.mkdir(parents=True, exist_ok=True)
            lockfile = self.active_file.parent / '.apply.lock'
            try:
                handle = lockfile.open('x')
            except FileExistsError as error:
                raise StudioError(409, '另一美术应用正在进行；保留草稿，请稍后刷新') from error
            try:
                state = self.snapshot()
                if not isinstance(data, dict) or set(data) != {'revision'}:
                    raise StudioError(400, '美术应用格式为 {revision}')
                if data['revision'] != state['revision'] or state['sourceChanged'] or not state['hasDraft']:
                    raise StudioError(409, '请先刷新并暂存待应用的美术绑定')
                self.validate_bindings(state['registry'], state['pending'])
                bindings = {}
                for key, version in state['pending'].items():
                    _, value = self.version(state['registry'], key, version)
                    bindings[key] = dict(version=version, path='res://' + value['runtime']['path'][5:], sha256=value['runtime']['sha256'])
                application = dict(id=str(uuid.uuid4()), appliedAt=timestamp(), actor='content-studio-art',
                                   registrySha256=hashlib.sha256(self.registry_file.read_bytes()).hexdigest())
                active = dict(schemaVersion=1, styleId=state['registry'].get('styleId'), bindings=bindings, application=application)
                # 先保存可恢复的应用记录，再原子替换唯一运行时绑定文件。
                atomic_json(self.root / 'assets/art/applications' / (application['id'] + '.json'),
                            dict(application=application, previous=state['active'], next=active,
                                 note='仅应用美术；未读取内容草稿，未修改配方、经济、存档或模型'))
                atomic_json(self.active_file, active)
                self.draft_file.unlink(missing_ok=True)
                return self.snapshot()
            finally:
                handle.close()
                lockfile.unlink(missing_ok=True)

    def image(self, asset_id, version):
        _, value = self.version(self.registry(), asset_id, version)
        return self.file(value['runtime']['path']).read_bytes()

    def thumbnails(self):
        state = self.snapshot()
        result = []
        for asset in state['registry']['assets']:
            if not asset['id'].startswith('item.'):
                continue
            item_id = asset['id'][5:]
            for value in asset['versions']:
                version = value['version']
                result.append(dict(id=item_id if version == 'v1' else 'item-'+item_id+'-'+version, itemId=item_id, name=asset['name'],
                    url='/api/art/image/'+asset['id']+'/'+version, version=version, origin=value['origin'],
                    formalActive=state['active']['bindings'].get(asset['id'], {}).get('version') == version,
                    sha256=value['runtime']['sha256']))
        return result
