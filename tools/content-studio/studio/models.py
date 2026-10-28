"""真实三维实体的独立版本与游戏绑定；不应用图片或内容玩法草稿。"""
import copy,hashlib,math,re,uuid,struct
from .art import ArtLibrary,VERSION
from .common import StudioError,read_json,atomic_json,timestamp

MODEL_ID=r'(actor|item|module|building|furniture|prop)\.[a-z][a-z0-9_]*'

class ModelLibrary(ArtLibrary):
    def __init__(self,root,workspace):
        super().__init__(root,workspace)
        self.registry_file=self.root/'assets/art/model-registry.json'
        self.active_file=self.root/'game/art/model-bindings.json'
        self.draft_file=self.draft_file.with_name('model-art-draft.json')

    def registry(self):
        registry=read_json(self.registry_file,dict(schemaVersion=1,assets=[]))
        if registry.get('schemaVersion')!=1 or not isinstance(registry.get('assets'),list):raise StudioError(500,'实体模型登记格式错误')
        seen=set()
        for asset in registry['assets']:
            key=asset.get('id','')
            if not re.fullmatch(MODEL_ID,key) or key in seen:raise StudioError(500,'实体模型身份重复或错误')
            seen.add(key);versions=asset.get('versions',[])
            if len({v['version'] for v in versions})!=len(versions) or any(not re.fullmatch(VERSION,v['version']) for v in versions):raise StudioError(500,'模型版本重复或错误')
        return registry

    def validate_bindings(self,registry,bindings):
        if not isinstance(bindings,dict):raise StudioError(422,'模型bindings须为身份到版本的映射')
        slots=read_json(self.root/'game/art/model-slots.json',{}).get('slots',{})
        for key,version in bindings.items():
            if key not in slots:raise StudioError(422,'模型没有现有视觉使用槽 '+str(key))
            _,value=self.version(registry,key,version)
            if value.get('agentReview',{}).get('status')!='accepted':raise StudioError(422,'模型尚未通过代理审查 '+key)
            runtime=value.get('runtime',{});path=runtime.get('path','')
            if not path.startswith('game/art/models/') or not path.endswith('.glb'):raise StudioError(422,'运行模型须在game/art/models中使用GLB')
            model_file=self.file(path)
            if not model_file.is_file():raise StudioError(422,'模型原件不存在')
            with model_file.open('rb') as stream:header=stream.read(12)
            if len(header)!=12 or struct.unpack('<III',header)!=(0x46546c67,2,model_file.stat().st_size):raise StudioError(422,'实体必须是真实glTF2二进制模型，不能以图片或文本冒充')
            scale=value.get('binding',{}).get('scale',1)
            if type(scale) not in (float,int) or not math.isfinite(scale) or not 0<scale<=4:raise StudioError(422,'模型规范尺度错误')
            author=value.get('author',{})
            if author.get('license') not in ['CC0-1.0','project-owned'] or not author.get('name'):raise StudioError(422,'本流程仅接收已核对CC0来源或项目自有模型；其他许可需先完成相应处理')
            entries=[value.get('original',{}),runtime,value.get('preview',{})]+runtime.get('dependencies',[])
            for info in entries:
                file=self.file(info.get('path',''))
                if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest()!=info.get('sha256'):raise StudioError(422,'模型文件/依赖指纹变化 '+str(info.get('path')))
            if key.startswith('actor.'):
                skeleton=value.get('skeleton',{})
                if skeleton.get('bones',0)<50 or not all(skeleton.get('clips',{}).get(name) for name in ['idle','walk','run']):raise StudioError(422,'人物缺少真实骨骼和行走动作记录')

    def image(self,key,version):
        _,value=self.version(self.registry(),key,version)
        info=value['preview'];data=self.file(info['path']).read_bytes()
        if hashlib.sha256(data).hexdigest()!=info['sha256']:raise StudioError(422,'模型预览指纹变化')
        return data

    def model(self,key,version):
        _,value=self.version(self.registry(),key,version)
        info=value.get('model',{})
        if not info.get('path'):raise StudioError(422,'此版本没有静态形体检查网格；请看真实游戏渲染和GLB')
        data=self.file(info['path']).read_bytes()
        if hashlib.sha256(data).hexdigest()!=info.get('sha256'):raise StudioError(422,'检查网格指纹变化')
        return data

    def apply(self,data):
        with self.lock:
            self.active_file.parent.mkdir(parents=True,exist_ok=True);lock=self.active_file.parent/'.model-apply.lock'
            try:handle=lock.open('x')
            except FileExistsError as error:raise StudioError(409,'已有模型应用正在进行') from error
            try:
                state=self.snapshot()
                if not isinstance(data,dict) or set(data)!={'revision'}:raise StudioError(400,'模型应用格式为{revision}')
                if data['revision']!=state['revision'] or state['sourceChanged'] or not state['hasDraft']:raise StudioError(409,'请先刷新并独立暂存模型版本')
                self.validate_bindings(state['registry'],state['pending']);bindings={}
                for key,version in state['pending'].items():
                    _,value=self.version(state['registry'],key,version);runtime=value['runtime']
                    # 仅允许视觉绑定字段；源数据和玩法草稿不会进入这里。
                    entry={k:copy.deepcopy(v) for k,v in value.get('binding',{}).items() if k in ['scale','gripPose','animations','appearance','dimensions','origin','forward']}
                    entry.update(version=version,path='res://'+runtime['path'][5:],sha256=runtime['sha256'],author=value['author']['name'],license=value['author']['license'])
                    bindings[key]=entry
                application=dict(id=str(uuid.uuid4()),appliedAt=timestamp(),actor='content-studio-model-art',registrySha256=hashlib.sha256(self.registry_file.read_bytes()).hexdigest())
                active=dict(schemaVersion=1,bindings=bindings,application=application)
                atomic_json(self.root/'assets/art/model-applications'/(application['id']+'.json'),dict(application=application,previous=state['active'],next=active,note='仅实体模型与视觉参数；未应用图片或玩法草稿'))
                atomic_json(self.active_file,active);self.draft_file.unlink(missing_ok=True);return self.snapshot()
            finally:handle.close();lock.unlink(missing_ok=True)
