"""Python 运行时回归：临时草稿，不写入游戏或用户工作空间。"""
import copy
import hashlib
import http.client
import json
import shutil
import socket
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1]
ROOT = TOOL.parents[1]
sys.path.insert(0, str(TOOL))
from server import StudioServer, MAX_BODY_BYTES, parse_allowed_network, make_cpp_proposal
from studio.catalog import load_catalog, SOURCE_PATHS
from studio.common import StudioError, digest, loads
from studio.media import parse_obj
from studio.store import WorkspaceStore
from studio.validation import validate_project, diff_projects

OBJ = 'v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n'


class StudioTests(unittest.TestCase):
    def setUp(self):
        (TOOL / '.cache').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=TOOL / '.cache')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.root = self.directory / 'repository'
        for name in SOURCE_PATHS:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        self.workspace = self.directory / 'workspace'
        self.server = StudioServer(self.root, self.workspace, quiet=True)
        self.url = self.server.start(0)
        self.addCleanup(self.server.close)
        self.port = self.server.servers[0].server_address[1]

    def request(self, path, method='GET', body=None, headers=None, host='127.0.0.1', chunked=False):
        conn = http.client.HTTPConnection(host, self.port, timeout=10)
        data = json.dumps(body, ensure_ascii=False).encode() if body is not None else None
        options = {'Content-Type': 'application/json', **(headers or {})}
        conn.request(method, path, body=iter([data]) if chunked else data, headers=options, encode_chunked=chunked)
        response = conn.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        conn.close()
        return result

    def api(self, path='/api/workspace', **options):
        status, headers, data = self.request(path, **options)
        return status, json.loads(data)

    def test_catalog_and_media_complete(self):
        status, workspace = self.api()
        self.assertEqual(status, 200)
        self.assertEqual(len(workspace['project']['items']), 49)
        self.assertEqual(len(workspace['project']['recipes']), 24)
        self.assertEqual(workspace['issues'], [])
        _, assets = self.api('/api/assets')
        self.assertEqual(len(assets['thumbnails']), 49)
        self.assertEqual(len(assets['models']), 49)
        self.assertEqual(assets['missingThumbnails'], [])
        for row in assets['thumbnails'] + assets['models']:
            status, _, data = self.request(row['url'])
            self.assertEqual(status, 200, row['url'])
            self.assertEqual(hashlib.sha256(data).digest(), hashlib.sha256((TOOL / 'public' / row['url'].lstrip('/')).read_bytes()).digest())

    def test_save_import_export_reset_and_restart(self):
        _, initial = self.api()
        project = initial['project']
        project['items'][0]['name'] = '迁移测试'
        status, saved = self.api(method='POST', body={'project': project, 'revision': initial['revision']})
        self.assertEqual(status, 200)
        self.assertEqual(WorkspaceStore(self.root, self.workspace).get_workspace()['project'], saved['project'])
        status, exported = self.api('/api/export')
        self.assertEqual(exported, saved['project'])
        self.assertEqual(self.api('/api/report')[1]['revision'], saved['revision'])
        self.assertEqual(self.request('/api/patch')[0], 200)
        self.assertEqual(self.api('/api/import', method='POST', body={'project': project, 'revision': initial['revision']})[0], 409)
        status, imported = self.api('/api/import', method='POST', body={'project': project, 'revision': saved['revision']}, chunked=True)
        self.assertEqual(status, 200)
        old = (self.workspace / 'draft.json').read_bytes()
        status, reset = self.api('/api/reset', method='POST', body={'revision': imported['revision']})
        self.assertEqual(status, 200)
        self.assertEqual(reset['changes'], [])
        self.assertIn(old, [p.read_bytes() for p in self.workspace.glob('draft.backup.*.json')])

    def test_concurrent_saves_conflict(self):
        _, initial = self.api()
        with ThreadPoolExecutor(2) as pool:
            results = list(pool.map(lambda _: self.api(method='POST', body={'project': initial['project'], 'revision': initial['revision']})[0], range(2)))
        self.assertEqual(sorted(results), [200, 409])

    def test_legacy_node_draft_and_corruption_preserved(self):
        self.workspace.mkdir(exist_ok=True)
        target = self.workspace / 'draft.json'
        shutil.copyfile(TOOL / 'test_python/fixtures/legacy-node-draft.json', target)
        data = loads(target.read_text(encoding='utf-8'))
        self.assertEqual(digest(data['project']), data['sha256'])
        before = target.read_bytes()
        self.assertEqual(WorkspaceStore(self.root, self.workspace).get_workspace()['revision'], data['revision'])
        self.assertEqual(target.read_bytes(), before)
        target.write_bytes(b'{broken')
        with self.assertRaises(StudioError):
            WorkspaceStore(self.root, self.workspace)
        self.assertEqual(target.read_bytes(), b'{broken')

    def test_source_drift_blocks_save(self):
        _, initial = self.api()
        path = self.root / SOURCE_PATHS[0]
        path.write_bytes(path.read_bytes()+b'\n// source drift\n')
        self.assertEqual(self.api(method='POST', body={'project': initial['project'], 'revision': initial['revision']})[0], 409)
        _, fresh = self.api()
        self.assertEqual(self.api(method='POST', body={'project': initial['project'], 'revision': fresh['revision']})[0], 409)

    def test_upload_obj_persistence_and_invalid_geometry(self):
        status, result = self.api('/api/models', method='POST', body={'name': '测试模型', 'filename': '模型.obj', 'content': OBJ})
        self.assertEqual(status, 201)
        model = result['model']
        self.assertEqual(self.api(model['url'])[1]['triangleCount'], 1)
        other = StudioServer(self.root, self.workspace, quiet=True)
        self.assertEqual(other.media.read_model(model['id'])['name'], '测试模型')
        self.assertEqual(self.api('/api/assets')[1]['models'][-1]['id'], model['id'])
        self.assertEqual(self.api('/api/models/../../draft.json')[0], 403)
        for text in ['v 0 0 0\nf 1 1 1', 'v NaN 0 0', OBJ.replace('f 1 2 3', 'f 0 2 3')]:
            with self.assertRaises(StudioError):
                parse_obj(text)
        negative = OBJ.replace('f 1 2 3', 'f -3 -2 -1')
        self.assertEqual(parse_obj(negative), parse_obj(OBJ))

    def test_static_files_and_request_security(self):
        for path in ['/', '/app.js', '/theme.css', '/lib/validation.mjs']:
            status, headers, data = self.request(path)
            self.assertEqual(status, 200)
            self.assertIn('Content-Security-Policy', headers)
            status, head_headers, empty = self.request(path, 'HEAD')
            self.assertEqual(status, 200)
            self.assertEqual(empty, b'')
            self.assertEqual(head_headers['Content-Length'], str(len(data)))
        for path in ['/server.py', '/studio/store.py', '/workspace/draft.json', '/lib/store.mjs']:
            self.assertEqual(self.request(path)[0], 404)
        for path in ['/%2e%2e/src/crops.cpp', '/%5cserver.py']:
            self.assertEqual(self.request(path)[0], 403)
        self.assertEqual(self.request('/%xy')[0], 400)
        self.assertEqual(self.request('/', headers={'Host': 'evil.example'})[0], 403)
        for site in ['none', 'cross-site']:
            self.assertEqual(self.request('/', headers={'Sec-Fetch-Site': site})[0], 200)
        self.assertEqual(self.api(method='POST', body={}, headers={'Origin': 'http://evil.example'})[0], 403)
        self.assertEqual(self.api(method='POST', body={}, headers={'Sec-Fetch-Site': 'cross-site'})[0], 403)
        self.assertEqual(self.request('/api/reset', 'POST', {}, {'Content-Type': 'text/plain'})[0], 415)
        self.assertEqual(self.request('/api/reset', 'POST', {}, {'Content-Length': str(MAX_BODY_BYTES+1)})[0], 413)

    def test_validation_structure_references_and_diff(self):
        baseline = load_catalog(self.root)
        project = copy.deepcopy(baseline)
        project['items'][0]['stack'] = True
        self.assertTrue(any(row['severity'] == 'error' for row in validate_project(project, baseline)))
        self.assertEqual(self.api(method='POST', body={'project': project, 'revision': self.api()[1]['revision']})[0], 422)
        project = copy.deepcopy(baseline)
        project['recipes'][0]['ingredients'][0]['item'] = 'missing_item'
        self.assertTrue(any(row['severity'] == 'error' for row in validate_project(project, baseline)))
        with self.assertRaises(StudioError):
            make_cpp_proposal(project, baseline)
        project = copy.deepcopy(baseline)
        project['items'][0]['name'] += '新版'
        self.assertEqual(len(diff_projects(baseline, project)), 1)

    def test_network_configuration_and_dual_listener(self):
        for value in ['0.0.0.0/0x', '10.8.0.2/24', '::1/128']:
            with self.assertRaises(ValueError):
                parse_allowed_network(value)
        for address in ['0.0.0.0', '127.0.0.1', 'localhost']:
            with self.assertRaises(ValueError):
                StudioServer(self.root, self.workspace, network_address=address)
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.bind(('10.8.0.2', 0))
        except OSError:
            self.skipTest('本机未连接 Work VPN')
        finally:
            probe.close()
        network = StudioServer(self.root, self.workspace, network_address='10.8.0.2', allowed_network='10.8.0.0/24', quiet=True)
        network.start(0)
        self.addCleanup(network.close)
        old_port, self.port = self.port, network.servers[0].server_address[1]
        self.assertEqual(self.api(host='10.8.0.2')[0], 200)
        self.assertEqual(self.api()[0], 200)
        network.allowed_network = parse_allowed_network('192.0.2.0/24')
        self.assertEqual(self.api(host='10.8.0.2')[0], 403)
        self.assertEqual(self.api()[0], 200)
        self.port = old_port

    def test_json_rejects_nonfinite(self):
        for value in ['NaN', 'Infinity', '1e999']:
            with self.assertRaises(ValueError):
                loads(value)

    def test_node_migration_golden_contract(self):
        """迁移前保存的 Node 输出；此测试及 Python 服务本身均不调用 Node。"""
        reference = loads((TOOL / 'test_python/fixtures/node-validation-parity.json').read_text(encoding='utf-8'))
        baseline = reference['baseline']
        # 元数据中的生成时间与文件行顺序不属于语义内容。
        current = load_catalog(self.root)
        self.assertEqual(current, baseline)
        for case in reference['cases']:
            with self.subTest(case=case['name']):
                project = {**copy.deepcopy(baseline), **case['overrides']}
                self.assertEqual(validate_project(project, baseline), case['issues'])
                if case['changes'] is not None:
                    self.assertEqual(diff_projects(baseline, project), case['changes'])
        self.assertEqual(parse_obj(reference['obj']), reference['objMesh'])
        self.assertEqual(make_cpp_proposal(baseline, baseline), reference['cpp'])


if __name__ == '__main__':
    unittest.main()
