"""登记已审看的真实家具/货箱GLB、编辑源和同源图片；拒绝覆盖历史版本。"""
import argparse,hashlib,json,pathlib,shutil,sys,re
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/content-studio'))
from studio.common import atomic_json,timestamp
from studio.catalog import parse_initializer
def info(path):
    return dict(path=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',required=True);p.add_argument('--model-version',default='v1');p.add_argument('--image-version',default='v3');a=p.parse_args()
    if not all(re.fullmatch(r'v[1-9][0-9]*',v) for v in [a.model_version,a.image_version]):raise ValueError('版本须为vN')
    review=ROOT/a.review;report=json.loads((review/'report.json').read_text(encoding='utf8'))
    if not report['passed'] or report['renderer']=='headless' or len(report['models'])!=14:raise ValueError('需要14个真实GPU模型验收记录')
    models=json.loads((ROOT/'assets/art/model-registry.json').read_text(encoding='utf8'));images=json.loads((ROOT/'assets/art/registry.json').read_text(encoding='utf8'))
    trial=json.loads((ROOT/'game/art/model-bindings-review.json').read_text(encoding='utf8'))
    names={n.value[0].value:n.value[1].value for n in parse_initializer((ROOT/'src/house_template.cpp').read_text(encoding='utf8'),'DEFS','src/house_template.cpp')}
    names['cargo_crate']='统一货箱'
    # 在创建任何文件之前检查全部版本；错误的版本号不能将v1实体登记成v2。
    for key in report['models']:
        entry=trial['bindings'][key]
        if entry['version']!=a.model_version or pathlib.Path(entry['path']).parent.name!=a.model_version:raise ValueError('试接入运行文件与发布版本不一致 '+key)
        for registry,version in [(models,a.model_version),(images,a.image_version)] if key.startswith('furniture.') else [(models,a.model_version)]:
            asset=next((r for r in registry['assets'] if r['id']==key),None)
            if asset and any(v['version']==version for v in asset['versions']):raise ValueError('正式版本已经存在 '+key+'/'+version)
    for key,row in report['models'].items():
        namespace,kind=key.split('.');entry=trial['bindings'][key];runtime=ROOT/'game'/entry['path'].removeprefix('res://')
        if info(runtime)['sha256']!=entry['sha256'] or row['path']!=entry['path'] or row.get('sha256')!=entry['sha256']:raise ValueError('GPU来源模型指纹变化 '+key)
        preparation=json.loads((ROOT/f'build/prepared-furniture/{kind}-runtime.json').read_text(encoding='utf8'))
        target=ROOT/'assets/art/models'/namespace/kind/a.model_version;target.mkdir(parents=True,exist_ok=True)
        blend=target/'source.blend';render=target/'render.png';preview=runtime.parent/'preview.png'
        if any(f.exists() for f in [blend,render,preview]):raise ValueError('版本文件已存在 '+key)
        source_image=review/(key+'.png');im=Image.open(source_image)
        if im.size!=(512,512) or im.mode!='RGBA' or im.getchannel('A').getextrema()!=(0,255):raise ValueError('需要512真实透明PNG '+key)
        shutil.copyfile(ROOT/f'build/prepared-furniture/{kind}.blend',blend);shutil.copyfile(source_image,render);shutil.copyfile(render,preview)
        source=preparation['sourceAsset']
        if source:
            folder=ROOT/'assets/art/external/polyhaven/20261027'/source;meta=json.loads((folder/'info.json').read_text(encoding='utf8'))
            original=folder/(source+'_1k.gltf');author=dict(name='Poly Haven / '+', '.join(meta.get('authors',{}).keys()),license='CC0-1.0',licenseUrl='https://polyhaven.com/license',sourceUrl='https://polyhaven.com/a/'+source)
        else:
            original=ROOT/'assets/art/models/furniture-scene-20261027-v2'/(kind+'.json');author=dict(name='州渚项目原模型与Blender精修',license='project-owned')
        evidence=dict(status='accepted',reviewer='Codex',reviewedAt=timestamp(),evidence=a.review,scope='实际GLB生产着色器、占地、透明边缘、主体语义与接合像素；真实场景与碰撞另验')
        dependencies=[info(runtime.parent/item['path']) for item in preparation['dependencies']]
        version=dict(version=a.model_version,origin=preparation['origin'],styleId='painted-jrpg-3d-v1',productionStatus='produced',generatedAt=timestamp(),original=info(original),editableSource=info(blend),runtime={**info(runtime), 'dependencies':dependencies},preview=info(preview),render=info(render),author=author,binding={k:v for k,v in entry.items() if k not in ['version','path','sha256']},derivation=dict(preparation=preparation,tools=['tools/art-models/prepare_furniture.py','tools/art-models/externalize_textures.py'],sourceImages='原作者UV贴图原字节保留；颜色/粗糙度/材质映射由生产着色器控制',modificationsLicense='项目改编不自动转授CC0'),agentReview=evidence,userConfirmation=None)
        asset=next((r for r in models['assets'] if r['id']==key),None)
        if asset is None:asset=dict(id=key,name=names[kind],definition='src/house_template.cpp' if namespace=='furniture' else 'src/player.cpp / src/ship.cpp',useSites=['Settlement','InteriorOverlay','content-studio:models'] if namespace=='furniture' else ['Player:Crate','Ship:Cargo'],versions=[]);models['assets'].append(asset)
        if any(v['version']==a.model_version for v in asset['versions']):raise ValueError('模型版本重复 '+key)
        asset['versions'].append(version)
        if source:version['sourceArchiveManifest']=info(folder/'source.json')
        atomic_json(target/'preparation.json',preparation)
        if namespace=='furniture':
            asset=next(r for r in images['assets'] if r['id']==key)
            if any(v['version']==a.image_version for v in asset['versions']):raise ValueError('图片版本重复 '+key)
            original_preview=ROOT/'assets/art/previews/furniture'/kind/a.image_version/'render.png';original_preview.parent.mkdir(parents=True,exist_ok=True)
            if original_preview.exists():raise ValueError('历史原图已存在')
            shutil.copyfile(render,original_preview)
            derived=ROOT/'game/art/previews/furniture'/kind/(a.image_version+'.png');derived.parent.mkdir(parents=True,exist_ok=True)
            im.convert('RGBa').resize((256,256),Image.Resampling.LANCZOS).convert('RGBA').save(derived)
            asset['versions'].append(dict(version=a.image_version,origin='actual-authored-model-render',styleId='painted-jrpg-3d-v1',productionStatus='produced',generatedAt=timestamp(),original=info(original_preview),runtime=info(derived),derivation=dict(operation='Godot production model/shader orthographic render, premultiplied-alpha resize',modelId=key,modelVersion=a.model_version,model=info(runtime),sourceBounds=row['bounds'],camera=[1.1,.85,1.35],projection='orthographic',fit=.8,lightRotation=[-45,-30,0],ambient=.65,tools=['game/tests/furniture_models_e2e.gd']),agentReview=evidence,userConfirmation=None))
    atomic_json(ROOT/'assets/art/model-registry.json',models);atomic_json(ROOT/'assets/art/registry.json',images)
    print('登记14个实体与13个同源家具预览；尚未自动应用')
if __name__=='__main__':main()
