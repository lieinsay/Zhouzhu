"""将已审图的女式工作服v2登记并通过独立模型管理应用，保留v1。"""
import argparse,copy,hashlib,json,pathlib,shutil,sys
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools/content-studio'))
from studio.common import atomic_json,timestamp
from studio.models import ModelLibrary
def info(p):return dict(path=p.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',required=True);args=p.parse_args();review=ROOT/args.review
    report=json.loads((review/'report.json').read_text(encoding='utf8'))
    if not report['passed'] or report['renderer']=='headless':raise ValueError('需要已审看的真实GPU蒙皮与造型检查')
    registry=json.loads((ROOT/'assets/art/model-registry.json').read_text(encoding='utf8'));keys=['actor.young_female','actor.mature_female']
    for key in keys:
        asset=next(a for a in registry['assets'] if a['id']==key)
        if any(v['version']=='v2' for v in asset['versions']):raise ValueError('不覆盖已有v2 '+key)
    folder=ROOT/'assets/art/models/actors/female/v2';runtime=ROOT/'game/art/models/actors/female/v2/model.glb'
    folder.mkdir(parents=True,exist_ok=True);blend=folder/'source.blend'
    if blend.exists():raise ValueError('编辑源版本已存在')
    shutil.copyfile(ROOT/'build/prepared-workwear-v2/female.blend',blend)
    preparation=json.loads((ROOT/'build/prepared-workwear-v2/female.json').read_text(encoding='utf8'));atomic_json(folder/'fitting.json',preparation)
    deps=json.loads((ROOT/'build/prepared-workwear-v2/dependencies.json').read_text(encoding='utf8'))
    for index,key in enumerate(keys):
        age='young' if index==0 else 'mature';asset=next(a for a in registry['assets'] if a['id']==key);parent=asset['versions'][-1];value=copy.deepcopy(parent)
        render=folder/(age+'-render.png');preview=runtime.parent/(age+'-preview.png');im=Image.open(review/f'actor_{index+2}_idle.png')
        if im.mode!='RGBA' or im.getchannel('A').getextrema()!=(0,255):raise ValueError('必须为真实透明预览')
        shutil.copyfile(review/f'actor_{index+2}_idle.png',render);im.convert('RGBa').resize((512,512),Image.Resampling.LANCZOS).convert('RGBA').save(preview)
        value.update(version='v2',generatedAt=timestamp(),editableSource=info(blend),runtime={**info(runtime),'dependencies':[info(runtime.parent/d['path']) for d in deps]},preview=info(preview),render=info(render),derivation=dict(operation=preparation['operation'],parentVersion='v1',parentSource=parent['editableSource'],fitting=info(folder/'fitting.json'),tools=['tools/art-models/fit_workwear.py','tools/art-models/externalize_textures.py','tools/art-models/publish_workwear.py']),agentReview=dict(status='accepted',reviewer='Codex',reviewedAt=timestamp(),evidence=args.review,scope='真实造型、原蒙皮、动作和透明边缘；自然NPC复验另记'),userConfirmation=None)
        asset['versions'].append(value)
    atomic_json(ROOT/'assets/art/model-registry.json',registry)
    library=ModelLibrary(ROOT,ROOT/'tools/content-studio/workspace/npc-visual-20261028');state=library.snapshot();pending=state['pending'].copy()
    for key in keys:pending[key]='v2'
    staged=library.stage(dict(revision=state['revision'],bindings=pending));result=library.apply(dict(revision=staged['revision']))
    print('正式应用2个女式v2身份；实体总数',len(result['active']['bindings']))
if __name__=='__main__':main()
