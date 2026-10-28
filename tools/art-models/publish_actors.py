"""登记已完成自查的作者人物模型版本与源文件。已有正式版本不可覆盖。"""
from pathlib import Path
import json,hashlib,shutil,argparse,sys,zipfile
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/content-studio'))
from studio.common import atomic_json,timestamp

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--version',default='v1');p.add_argument('--review',required=True);args=p.parse_args()
    import re
    if not re.fullmatch(r'v[1-9][0-9]*',args.version):raise ValueError('版本须为vN')
    review=ROOT/args.review;report=json.loads((review/'report.json').read_text(encoding='utf8'))
    if not report.get('passed') or report.get('renderer')=='headless':raise ValueError('须有真实GPU人物自查输出')
    registry_file=ROOT/'assets/art/model-registry.json';registry=json.loads(registry_file.read_text(encoding='utf8')) if registry_file.exists() else dict(schemaVersion=1,styleId='painted-jrpg-3d-v1',assets=[])
    active=json.loads((ROOT/'game/art/model-bindings.json').read_text(encoding='utf8'))
    for variant,key in enumerate(['actor.young_male','actor.mature_male','actor.young_female','actor.mature_female']):
        gender='male' if variant<2 else 'female';age='mature' if variant&1 else 'young'
        asset=next((a for a in registry['assets'] if a['id']==key),None)
        if asset is None:asset=dict(id=key,name=('成熟' if variant&1 else '青年')+('成年男性' if variant<2 else '成年女性'),definition='game/art/model-slots.json',useSites=['player' if variant==0 else 'villager cosmetic variant','content-studio:formal-models'],versions=[]);registry['assets'].append(asset)
        if any(v['version']==args.version for v in asset['versions']):raise ValueError('正式版本已存在，不覆盖 '+key)
        binding=active['bindings'][key];runtime=ROOT/'game'/binding['path'].removeprefix('res://')
        if sha(runtime)!=binding['sha256']:raise ValueError('当前绑定指纹漂移 '+key)
        folder=ROOT/'assets/art/models/actors'/gender/args.version;folder.mkdir(parents=True,exist_ok=True)
        blend=folder/'source.blend';prepared=ROOT/'build/prepared-actors'/f'{gender}.blend'
        if blend.exists():
            if sha(blend)!=sha(prepared):raise ValueError('编辑源已存在且指纹不同')
        else:shutil.copyfile(prepared,blend)
        source=ROOT/'assets/art/external/vroid-cc0/20261027'/(gender+'-original.zip')
        preview_source=review/f'actor_{variant}_idle.png';preview=folder/(age+'-render.png');derived=runtime.parent/(age+'-preview.png')
        if preview.exists() or derived.exists():raise ValueError('预览已存在，不覆盖')
        image=Image.open(preview_source).convert('RGBA')
        if image.getchannel('A').getextrema()!=(0,255):raise ValueError('预览必须真实透明')
        shutil.copyfile(preview_source,preview);image.convert('RGBa').resize((512,512),Image.Resampling.LANCZOS).convert('RGBA').save(derived)
        preparation=json.loads((ROOT/'build/prepared-actors'/f'{gender}.json').read_text(encoding='utf8'))
        source_entry=dict(path=source.relative_to(ROOT).as_posix(),sha256=sha(source))
        author=dict(name='VRoid Project / pixiv',license='CC0-1.0',licenseUrl='https://vroid.pixiv.help/hc/en-us/articles/'+('360014788554-Sakurada-Fumiriya' if gender=='male' else '360013482714-Sendagaya-Shino'),motionAuthor='Quaternius',motionLicense='CC0-1.0',motionSource='assets/art/external/quaternius/ual-standard-20250610')
        dependencies=[]
        for item in binding['dependencies']:
            path=runtime.parent/item['path'];dependencies.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path)))
        version=dict(version=args.version,origin='external-model-adaptation',styleId='painted-jrpg-3d-v1',productionStatus='produced',generatedAt=timestamp(),original=source_entry,editableSource=dict(path=blend.relative_to(ROOT).as_posix(),sha256=sha(blend)),runtime=dict(path=runtime.relative_to(ROOT).as_posix(),sha256=sha(runtime),dependencies=dependencies),preview=dict(path=derived.relative_to(ROOT).as_posix(),sha256=sha(derived)),render=dict(path=preview.relative_to(ROOT).as_posix(),sha256=sha(preview)),binding={k:v for k,v in binding.items() if k not in ['path','sha256','version','dependencies']},author=author,skeleton=preparation,derivation=dict(operation='author topology/UV/skin retained; adult workwear adaptation, bone-length fitted pants, hidden-body removal; exact CC0 animation retarget; texture bytes externalized',tools=['tools/art-models/prepare_actors.py','tools/art-models/externalize_textures.py']),agentReview=dict(status='accepted',reviewer='Codex',reviewedAt=timestamp(),evidence=args.review,scope='真实蒙皮/动作/透明人物样件与职业握点；世界可见性、PCK另记'),userConfirmation=None)
        asset['versions'].append(version)
    atomic_json(registry_file,registry);print('登记4个成年外观模型身份，使用2套作者派生网格；原图、编辑源、贴图与作者许可分别保留')

if __name__=='__main__':main()
