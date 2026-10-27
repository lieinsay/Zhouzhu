"""美术真实 HTTP 接入、冲突与内容隔离，使用临时仓库和草稿。"""
import copy
import hashlib
import http.client
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

TOOL=Path(__file__).resolve().parents[1]
ROOT=TOOL.parents[1]
sys.path.insert(0,str(TOOL))
from server import StudioServer
from studio.catalog import SOURCE_PATHS
from studio.common import atomic_json


class ArtTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=TOOL/'.cache')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'repo'
        for name in SOURCE_PATHS:
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
        registry=json.loads((ROOT/'assets/art/registry.json').read_text(encoding='utf-8'))
        registry['assets']=copy.deepcopy(registry['assets'][:1])
        self.asset=registry['assets'][0]
        for version in self.asset['versions']:
            for key in ('original','runtime'):
                name=version[key]['path'];target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
        atomic_json(self.root/'assets/art/registry.json',registry)
        self.workspace=Path(self.temp.name)/'workspace'
        self.workspace.mkdir(parents=True)
        self.server=StudioServer(self.root,self.workspace,quiet=True)
        self.server.start(0);self.addCleanup(self.server.close)
        self.port=self.server.servers[0].server_address[1]

    def request(self,path,body=None):
        connection=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        headers={'Origin':f'http://127.0.0.1:{self.port}','Content-Type':'application/json'}
        connection.request('POST' if body is not None else 'GET',path,json.dumps(body) if body is not None else None,headers)
        response=connection.getresponse();payload=response.read();connection.close()
        return response.status, json.loads(payload) if 'image' not in path else payload

    def test_apply_images_without_content_draft(self):
        sentinel=self.workspace/'draft.json';sentinel.write_bytes(b'user draft sentinel')
        before={name:hashlib.sha256((self.root/name).read_bytes()).hexdigest() for name in SOURCE_PATHS}
        status,state=self.request('/api/art');self.assertEqual(status,200)
        key=self.asset['id'];version=self.asset['versions'][0]['version']
        status,png=self.request('/api/art/image/'+key+'/'+version)
        self.assertEqual(status,200);self.assertTrue(png.startswith(b'\x89PNG'))
        status,staged=self.request('/api/art/stage',dict(revision=state['revision'],bindings={key:version}))
        self.assertEqual(status,200);self.assertTrue(staged['hasDraft'])
        self.assertFalse((self.root/'game/art/bindings.json').exists())
        status,active=self.request('/api/art/apply',dict(revision=staged['revision']))
        self.assertEqual(status,200);self.assertEqual(active['active']['bindings'][key]['version'],version)
        self.assertTrue(active['active']['bindings'][key]['path'].startswith('res://art/'))
        self.assertEqual(sentinel.read_bytes(),b'user draft sentinel')
        self.assertEqual(before,{name:hashlib.sha256((self.root/name).read_bytes()).hexdigest() for name in SOURCE_PATHS})
        self.assertEqual(len(list((self.root/'assets/art/applications').glob('*.json'))),1)
        self.assertFalse((self.workspace/'art-draft.json').exists())

    def test_stale_revision_hash_change_and_unreviewed_rejected(self):
        _,state=self.request('/api/art');key=self.asset['id'];version=self.asset['versions'][0]['version']
        self.assertEqual(self.request('/api/art/stage',dict(revision='stale',bindings={key:version}))[0],409)
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={'item.missing':version}))[0],422)
        _,staged=self.request('/api/art/stage',dict(revision=state['revision'],bindings={key:version}))
        self.assertEqual(self.request('/api/art/apply',dict(revision=state['revision']))[0],409)
        target=self.root/self.asset['versions'][0]['runtime']['path'];target.write_bytes(b'changed')
        self.assertEqual(self.request('/api/art/apply',dict(revision=staged['revision']))[0],422)
        self.assertFalse((self.root/'game/art/bindings.json').exists())
        registry_file=self.root/'assets/art/registry.json';registry=json.loads(registry_file.read_text(encoding='utf-8'))
        registry['assets'][0]['versions'][0]['agentReview']['status']='pending';atomic_json(registry_file,registry)
        _,state=self.request('/api/art')
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={key:version}))[0],422)

    def test_namespace_and_path_boundary(self):
        _,state=self.request('/api/art')
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={'furniture.chest':'v2'}))[0],422)
        self.assertEqual(self.request('/api/art/image/item.unknown/v2')[0],422)
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={},project={}))[0],400)
        file=self.root/'assets/art/registry.json';registry=json.loads(file.read_text(encoding='utf-8'))
        registry['assets'][0]['versions'][0]['runtime']['path']='../outside.png';atomic_json(file,registry)
        _,state=self.request('/api/art')
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={self.asset['id']:self.asset['versions'][0]['version']}))[0],403)

    def test_fixture_material_cannot_be_applied(self):
        file=self.root/'assets/art/registry.json';registry=json.loads(file.read_text(encoding='utf-8'))
        asset=registry['assets'][0];asset['id']='material.iron';asset['versions'][0]['bindingPolicy']='fixture_only'
        atomic_json(file,registry)
        _,state=self.request('/api/art')
        status,result=self.request('/api/art/stage',dict(revision=state['revision'],bindings={'material.iron':asset['versions'][0]['version']}))
        self.assertEqual(status,422);self.assertIn('受控样件',result['error'])
        self.assertFalse((self.root/'game/art/bindings.json').exists())

    def test_character_uses_explicit_runtime_slots(self):
        atomic_json(self.root/'game/art/scene-slots.json',{'characters':{'face_young_male':{}}})
        file=self.root/'assets/art/registry.json';registry=json.loads(file.read_text(encoding='utf8'))
        registry['assets'][0]['id']='character.face_young_male';atomic_json(file,registry)
        _,state=self.request('/api/art')
        version=self.asset['versions'][0]['version']
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={'character.face_young_male':version}))[0],200)
        _,state=self.request('/api/art')
        self.assertEqual(self.request('/api/art/stage',dict(revision=state['revision'],bindings={'character.undefined_face':version}))[0],422)

    def test_model_versions_fingerprint_and_path_boundary(self):
        model='assets/art/previews/building/house/v9/model.json'
        atomic_json(self.root/model,{'schemaVersion':1,'parts':[]})
        file=self.root/'assets/art/registry.json';registry=json.loads(file.read_text(encoding='utf8'))
        value=registry['assets'][0]['versions'][0]
        value['model']={'path':model,'sha256':hashlib.sha256((self.root/model).read_bytes()).hexdigest()}
        atomic_json(file,registry)
        url='/api/art/model/'+self.asset['id']+'/'+value['version']
        self.assertEqual(self.request(url)[0],200)
        (self.root/model).write_bytes(b'changed')
        self.assertEqual(self.request(url)[0],422)
        value['model']['path']='../outside.json';atomic_json(file,registry)
        self.assertEqual(self.request(url)[0],422)

    def test_current_snapshot_is_versioned_and_verified(self):
        directory='assets/art/models/test-snapshot';filename='module-bridge.json'
        atomic_json(self.root/directory/filename,{'schemaVersion':1,'parts':[]})
        atomic_json(self.root/directory/'manifest.json',{'schemaVersion':1,'models':[{'url':'/assets/models/'+filename,'assetSha256':hashlib.sha256((self.root/directory/filename).read_bytes()).hexdigest()}]})
        atomic_json(self.root/'assets/art/model-library.json',{'schemaVersion':1,'snapshot':directory})
        url='/api/art/snapshot/'+filename
        self.assertEqual(self.request(url)[0],200)
        self.assertEqual(self.request('/api/art/snapshot/not-registered.json')[0],404)
        (self.root/directory/filename).write_bytes(b'changed')
        self.assertEqual(self.request(url)[0],422)


if __name__=='__main__':unittest.main()
