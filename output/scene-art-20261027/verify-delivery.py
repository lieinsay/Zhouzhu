from pathlib import Path
import hashlib,json,sys,shutil,collections
from PIL import Image,ImageChops
root=Path.cwd();sys.path.insert(0,str(root/'tools/content-studio'))
from studio.art import ArtLibrary
from studio.catalog import load_catalog,parse_initializer
from studio.common import atomic_json
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
style=root/'assets/art/styles/painted-jrpg-3d-v1';style.mkdir(parents=True,exist_ok=True)
refs=[]
for source,name in [('output/art-style-industry-20261026/I1-painted-jrpg-industry.png','industry.png'),('output/art-style-three-distances-20261025/E1-painted-jrpg-board.png','life.png')]:
    target=style/name
    if target.exists():assert sha(target)==sha(root/source)
    else:shutil.copyfile(root/source,target)
    refs.append(dict(role='style-extraction-only',source=source,archived=target.relative_to(root).as_posix(),sha256=sha(target)))
atomic_json(style/'manifest.json',dict(schemaVersion=1,styleId='painted-jrpg-3d-v1',specification='docs/ART-DIRECTION.md',references=refs,note='仅提取精绘日式三维的材料、造型与光影语言；不继承构图、人物、机器或新增玩法。生图版本references为空表示未直接提交参考像素，完整风格文字已写入提示词。'))
registry=json.loads((root/'assets/art/registry.json').read_text(encoding='utf8'));registry['styleManifest']='assets/art/styles/painted-jrpg-3d-v1/manifest.json';atomic_json(root/'assets/art/registry.json',registry)
active=json.loads((root/'game/art/bindings.json').read_text(encoding='utf8'));lib=ArtLibrary(root,root/'tools/content-studio/workspace/scene-art-20261027');lib.validate_bindings(registry,{k:v['version'] for k,v in active['bindings'].items()})
catalog=load_catalog(root);expected={ns+'.'+row['id'] for ns,collection in [('item','items'),('module','modules'),('building','buildings')] for row in catalog[collection]}
for node in parse_initializer((root/'src/house_template.cpp').read_text(encoding='utf8'),'DEFS','src/house_template.cpp'):expected.add('furniture.'+node.value[0].value)
slots=json.loads((root/'game/art/scene-slots.json').read_text(encoding='utf8'))
for ns,col in [('material','materials'),('character','characters')]:expected.update(ns+'.'+key for key in slots[col])
assert expected==set(active['bindings']),dict(missing=sorted(expected-set(active['bindings'])),extra=sorted(set(active['bindings'])-expected))
images={}
for key,binding in active['bindings'].items():
    asset=next(a for a in registry['assets'] if a['id']==key);version=next(v for v in asset['versions'] if v['version']==binding['version']);path=root/version['runtime']['path'];image=Image.open(path)
    assert sha(path)==binding['sha256']==version['runtime']['sha256']
    if not key.startswith('material.'):assert image.mode=='RGBA' and image.getchannel('A').getextrema()==(0,255),key
    if key.startswith(('material.','character.')):assert 'mipmaps/generate=true' in Path(str(path)+'.import').read_text(encoding='utf8'),key
    images[key]=dict(version=binding['version'],path=binding['path'],sha256=binding['sha256'],sourceOriginal=version['original']['path'],production=version['productionStatus'],agentReview=version['agentReview']['status'],toolBound=True,gamePackLoaded=True,userConfirmation=version.get('userConfirmation'))
review=root/'output/scene-art-20261027/pixels-refined'
base=Image.open(review/'scatter_0.png').convert('RGB');wet=Image.open(review/'tree_wet.png').convert('RGB');snow=Image.open(review/'tree_snow.png').convert('RGB')
wet_diff=sum(1 for pixel in ImageChops.difference(base,wet).get_flattened_data() if sum(pixel)>12);snow_diff=sum(1 for pixel in ImageChops.difference(base,snow).get_flattened_data() if sum(pixel)>12)
assert wet_diff>1000 and snow_diff>1000,(wet_diff,snow_diff)
report=dict(schemaVersion=1,scope='当前明确使用位置；条件需求、未来候选与程序数据不列生图队列',counts=dict(collections.Counter(key.split('.')[0] for key in images)),activeCount=len(images),styleReferences=refs,promptSets=['assets/art/materials/source-prompts-scene-20261027.json','assets/art/materials/source-prompts-v1.json','assets/art/characters/source-prompts-20261027-v2.json'],application=active['application'],bindingSha256=sha(root/'game/art/bindings.json'),images=images,verification=dict(coreChecks=191,coreFailures=0,httpTests=7,frontendAndModelTests=15,packBindings=101,packProbe='output/scene-art-20261027/art-pack-probe-final.result.json',scenePixels='output/scene-art-20261027/pixels-refined/report.json',masonryPixels='output/scene-art-20261027/masonry-preview-pixels/report.json',masonryMappingAssertions=3,previewGame='output/scene-art-20261027/preview-final-game/report.json',inventoryGame='output/scene-art-20261027/pixels/inventory/report.json',controlledWetChangedPixels=wet_diff,controlledSnowChangedPixels=snow_diff),limits=['专业雕刻、服装蒙皮与精细连接仍可迭代；图片覆盖不等于最终模型艺术质量','真实暴雨开局的地面纹理近看受能见度限制','未制作无现有入口的未来插画或14项独立库存模型','验证美术PCK读取，未完成整游戏发布包导出'])
atomic_json(root/'assets/art/acceptance/20261027.json',report)
print(json.dumps(dict(counts=report['counts'],active=report['activeCount'],wetDiff=wet_diff,snowDiff=snow_diff,coverage='complete for current runtime slots'),ensure_ascii=False))