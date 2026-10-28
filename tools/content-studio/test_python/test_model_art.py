"""实体绑定、图片/玩法隔离及不可冒充三维模型的真实HTTP校验。"""
import copy,hashlib,json,struct,unittest
from pathlib import Path
import test_art as fixtures
from studio.common import atomic_json

class ModelArtTests(unittest.TestCase):
    request=fixtures.ArtTests.request
    def setUp(self):
        fixtures.ArtTests.setUp(self)
        self.model=copy.deepcopy(self.asset);self.model['id']='actor.young_male'
        self.model['versions']=self.model['versions'][:1];value=self.model['versions'][0]
        file=self.root/'game/art/models/actor/test/v1/model.glb';file.parent.mkdir(parents=True,exist_ok=True)
        encoded=b'{"asset":{"version":"2.0"},"scenes":[{}],"scene":0}';encoded+=b' '*(-len(encoded)%4)
        file.write_bytes(struct.pack('<IIIII',0x46546c67,2,20+len(encoded),len(encoded),0x4e4f534a)+encoded)
        value['preview']=copy.deepcopy(value['runtime']);value['runtime']=dict(path=file.relative_to(self.root).as_posix(),sha256=hashlib.sha256(file.read_bytes()).hexdigest(),dependencies=[])
        value['author']=dict(name='Fixture author',license='CC0-1.0');value['binding']=dict(scale=1.0)
        value['skeleton']=dict(bones=80,clips={key:{'source':key} for key in ['idle','walk','run']})
        atomic_json(self.root/'assets/art/model-registry.json',dict(schemaVersion=1,assets=[self.model]))
        atomic_json(self.root/'game/art/model-slots.json',dict(schemaVersion=1,slots={'actor.young_male':{}}))

    def test_entity_apply_preserves_image_and_content_drafts(self):
        sentinel=self.workspace/'draft.json';sentinel.write_bytes(b'user content draft')
        image_binding=self.root/'game/art/bindings.json';atomic_json(image_binding,dict(schemaVersion=1,bindings={}))
        before=image_binding.read_bytes();_,state=self.request('/api/art/models')
        version=self.model['versions'][0]['version'];key=self.model['id']
        self.assertEqual(self.request('/api/art/models/image/'+key+'/'+version)[0],200)
        status,staged=self.request('/api/art/models/stage',dict(revision=state['revision'],bindings={key:version}));self.assertEqual(status,200)
        status,result=self.request('/api/art/models/apply',dict(revision=staged['revision']));self.assertEqual(status,200)
        self.assertEqual(result['active']['bindings'][key]['scale'],1.0)
        self.assertEqual(sentinel.read_bytes(),b'user content draft');self.assertEqual(image_binding.read_bytes(),before)
        self.assertEqual(len(list((self.root/'assets/art/model-applications').glob('*.json'))),1)

    def test_reject_flat_asset_and_dependency_hash_changes(self):
        file=self.root/self.model['versions'][0]['runtime']['path'];file.write_bytes(b'PNG is not a rigged model')
        version=self.model['versions'][0];version['runtime']['sha256']=hashlib.sha256(file.read_bytes()).hexdigest();atomic_json(self.root/'assets/art/model-registry.json',dict(schemaVersion=1,assets=[self.model]))
        _,state=self.request('/api/art/models')
        self.assertEqual(self.request('/api/art/models/stage',dict(revision=state['revision'],bindings={self.model['id']:version['version']}))[0],422)

    def test_dependency_change_between_stage_and_apply_rejected(self):
        value=self.model['versions'][0];file=self.root/'game/art/models/actor/test/v1/fabric.png';file.write_bytes(b'controlled texture bytes')
        value['runtime']['dependencies']=[dict(path=file.relative_to(self.root).as_posix(),sha256=hashlib.sha256(file.read_bytes()).hexdigest())]
        atomic_json(self.root/'assets/art/model-registry.json',dict(schemaVersion=1,assets=[self.model]));_,state=self.request('/api/art/models')
        status,staged=self.request('/api/art/models/stage',dict(revision=state['revision'],bindings={self.model['id']:value['version']}));self.assertEqual(status,200)
        file.write_bytes(b'changed after staged')
        self.assertEqual(self.request('/api/art/models/apply',dict(revision=staged['revision']))[0],422)
        self.assertFalse((self.root/'game/art/model-bindings.json').exists())

    def test_model_revision_and_namespace_guard(self):
        _,state=self.request('/api/art/models');key=self.model['id'];version=self.model['versions'][0]['version']
        self.assertEqual(self.request('/api/art/models/stage',dict(revision='stale',bindings={key:version}))[0],409)
        self.assertEqual(self.request('/api/art/models/stage',dict(revision=state['revision'],bindings={'actor.unknown':version}))[0],422)
        self.assertEqual(self.request('/api/art/models/apply',dict(revision=state['revision'],project={}))[0],400)

    def test_model_license_and_review_gate(self):
        self.model['versions'][0]['author']['license']='no-redistribution';atomic_json(self.root/'assets/art/model-registry.json',dict(schemaVersion=1,assets=[self.model]));_,state=self.request('/api/art/models')
        self.assertEqual(self.request('/api/art/models/stage',dict(revision=state['revision'],bindings={self.model['id']:self.model['versions'][0]['version']}))[0],422)
