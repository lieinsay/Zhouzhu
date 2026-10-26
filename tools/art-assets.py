"""美术原图登记与受控派生。仅制作人员需要 Pillow；工坊运行仍只需标准库。"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/content-studio'))
from studio.common import atomic_json, read_json, timestamp

REGISTRY = ROOT / 'assets/art/registry.json'


def fingerprint(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metrics(path):
    from PIL import Image
    image = Image.open(path).convert('RGBA')
    alpha = image.getchannel('A')
    histogram = alpha.histogram()
    box = alpha.point(lambda value: 255 if value > 8 else 0).getbbox()
    if image.width != image.height or histogram[0] == 0 or not box or histogram[255] == 0:
        raise ValueError(f'{path}: 需要有可见主体和真实透明像素的方形 PNG')
    return dict(width=image.width, height=image.height, alphaBounds=list(box),
                transparentPixels=histogram[0], visiblePixels=image.width*image.height-histogram[0])


def derive(source, target):
    from PIL import Image
    image = Image.open(source).convert('RGBA')
    # 只裁透明边距，不重绘内容。统一长边78%，预乘透明缩放防止暗边。
    box = image.getchannel('A').getbbox()
    crop = image.crop(box)
    factor = 200/max(crop.size)
    size = tuple(max(1, round(edge*factor)) for edge in crop.size)
    crop = crop.convert('RGBa').resize(size, Image.Resampling.LANCZOS).convert('RGBA')
    output = Image.new('RGBA', (256, 256))
    output.paste(crop, ((256-size[0])//2, (256-size[1])//2))
    target.parent.mkdir(parents=True, exist_ok=True)
    output.save(target)
    return dict(operation='alpha_bbox_center_resize', size=256, subjectLongEdge=200,
                filter='Lanczos premultiplied alpha', sourceBounds=list(box))


def register(args):
    if not re.fullmatch(r'item\.[a-z][a-z0-9_]*', args.asset) or not re.fullmatch(r'v[1-9][0-9]*', args.version):
        raise ValueError('只接受已定义 item.<id> 与 vN 版本')
    from studio.catalog import load_catalog
    item_id = args.asset[5:]
    item = next((i for i in load_catalog(ROOT)['items'] if i['id'] == item_id), None)
    if item is None:
        raise ValueError('不能通过美术登记新增游戏物品')
    registry = read_json(REGISTRY, dict(schemaVersion=1, styleId='painted-jrpg-3d-v1', assets=[]))
    asset = next((a for a in registry['assets'] if a['id'] == args.asset), None)
    if asset is None:
        asset = dict(id=args.asset, name=item['name'], definition='src/crops.cpp',
                     useSites=['content-studio', 'InventoryPanel:pack/container/cursor/recipes', 'HotbarBar'], versions=[])
        registry['assets'].append(asset)
    if any(v['version'] == args.version for v in asset['versions']):
        raise ValueError('版本已存在；不得覆盖历史原图或登记')
    source = Path(args.source).resolve()
    info = metrics(source)
    if args.legacy:
        if not source.is_relative_to(ROOT / 'tools/content-studio/public/assets/thumbnails'):
            raise ValueError('旧图必须来自现有缩略图库')
        original = source
        old = read_json(ROOT/'tools/content-studio/public/assets/thumbnails/manifest.json')['thumbnails']
        row = next(v for v in old if v['itemId'] == item_id)
        if fingerprint(source) != row['sha256']:
            raise ValueError('旧图不符合历史清单指纹')
        prompt, references = row['prompt'], []
    else:
        original = ROOT / 'assets/art/items' / item_id / args.version / 'original.png'
        if original.exists():
            raise ValueError('原图文件已存在；不能覆盖')
        original.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, original)
        prompts = read_json(Path(args.prompts))
        prompt = prompts['prompts'][item_id]
        references = [dict(path=ref, role='style only', sha256=fingerprint(ROOT/ref)) for ref in args.reference]
    target = ROOT / 'game/art/items' / item_id / (args.version+'.png')
    if target.exists():
        raise ValueError('派生文件已存在；不能覆盖')
    derivation = derive(original, target)
    value = dict(version=args.version, styleId=registry['styleId'], origin='legacy-imagegen-reused' if args.legacy else 'built-in-imagegen',
                 productionStatus='produced', generatedAt=timestamp(), generatorModel='内置工具未暴露具体模型与参数',
                 original=dict(path=original.relative_to(ROOT).as_posix(), sha256=fingerprint(original), **info),
                 runtime=dict(path=target.relative_to(ROOT).as_posix(), sha256=fingerprint(target), **metrics(target)),
                 prompt=prompt, references=references, derivation=derivation,
                 generationSource=str(source), userConfirmation=None,
                 agentReview=dict(status='pending', note='尚未完成主体、透明边缘及32/48/64审图'))
    asset['versions'].append(value)
    atomic_json(REGISTRY, registry)
    print(f"登记 {args.asset}/{args.version} → {target.relative_to(ROOT)}")


def review(args):
    registry = read_json(REGISTRY)
    asset = next(a for a in registry['assets'] if a['id'] == args.asset)
    value = next(v for v in asset['versions'] if v['version'] == args.version)
    for key in ('original', 'runtime'):
        if fingerprint(ROOT/value[key]['path']) != value[key]['sha256']:
            raise ValueError('文件指纹变化，不能审图通过')
    value['agentReview'] = dict(status='accepted' if args.accept else 'rejected', reviewedAt=timestamp(),
                                reviewer='Codex', note=args.note, userConfirmed=False)
    atomic_json(REGISTRY, registry)


def register_material(args):
    from PIL import Image
    key = args.asset.removeprefix('material.')
    if args.asset != 'material.'+key or key not in ('wood','rammed_earth','terracotta','iron') or not re.fullmatch(r'v[1-9][0-9]*',args.version) or not 0<args.tile_metres<=16:
        raise ValueError('材质身份、版本或米制尺度不合法')
    registry=read_json(REGISTRY)
    asset=next((a for a in registry['assets'] if a['id']==args.asset),None)
    if asset is None:raise ValueError('先登记材质用途与实际入口，再增加版本')
    if any(v['version']==args.version for v in asset['versions']):raise ValueError('版本已存在，不覆盖历史')
    plan=read_json(Path(args.prompts))
    prompt=next(row['prompt'] for row in plan['materials'] if row['id']==key)
    original=ROOT/'assets/art/materials'/key/args.version/'original.png'
    candidate=key=='iron'
    target=(ROOT/'assets/art/materials'/key/args.version/'basecolor.png') if candidate else (ROOT/'game/art/materials'/key/(args.version+'.png'))
    if original.exists() or target.exists():raise ValueError('原图或派生文件已存在')
    image=Image.open(args.source).convert('RGB')
    if image.width!=image.height:raise ValueError('基础材质需要方形图片')
    original.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(args.source,original)
    image=image.resize((512,512),Image.Resampling.LANCZOS);target.parent.mkdir(parents=True,exist_ok=True);image.save(target)
    linear=[(n/255)/12.92 if n/255<=.04045 else ((n/255+.055)/1.055)**2.4 for n in range(256)]
    means=[sum(count*linear[n] for n,count in enumerate(image.getchannel(channel).histogram()))/(512*512) for channel in ('R','G','B')]
    version=dict(version=args.version,styleId=registry['styleId'],origin='built-in-imagegen',prompt=prompt,references=[],generationSource=str(Path(args.source).resolve()),
        original=dict(path=original.relative_to(ROOT).as_posix(),sha256=fingerprint(original)),runtime=dict(path=target.relative_to(ROOT).as_posix(),sha256=fingerprint(target)),
        derivation=dict(operation='Lanczos resize RGB to512; no repainting'),surface=dict(tileMetres=args.tile_metres,meanLinear=means,channels=['baseColor'],mapping='object-local metric triplanar'),
        productionStatus='controlled_sample_only' if candidate else 'produced',agentReview=dict(status='pending',note='待原图、重复和实际样件验收'),userConfirmation=None)
    if candidate:version['bindingPolicy']='fixture_only'
    asset['versions'].append(version);atomic_json(REGISTRY,registry)
    print(f'登记 {args.asset}/{args.version}，尚未应用到游戏')


def sheet(args):
    from PIL import Image, ImageDraw
    registry = read_json(REGISTRY)
    rows = [(a, v) for a in registry['assets'] for v in a['versions'] if not args.version or v['version'] == args.version]
    output = ROOT / args.output
    output.mkdir(parents=True, exist_ok=True)
    for background in ('light', 'dark'):
        page = Image.new('RGB', (720, max(1, len(rows))*110), '#eeeeea' if background=='light' else '#252b32')
        draw = ImageDraw.Draw(page)
        for n, (asset, version) in enumerate(rows):
            image = Image.open(ROOT/version['runtime']['path']).convert('RGBA')
            draw.text((8,n*110+8),asset['id']+'/'+version['version'],fill='#1b2630' if background=='light' else 'white')
            x = 260
            for size in (32,48,64):
                small=image.convert('RGBa').resize((size,size),Image.Resampling.LANCZOS).convert('RGBA')
                page.paste(small,(x,n*110+15),small)
                draw.text((x,n*110+84),str(size),fill='#1b2630' if background=='light' else 'white')
                x += 120
        page.save(output/(background+'.png'))
    print(f'{len(rows)} 个版本小尺寸审图表：{output}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    entry=commands.add_parser('register');entry.set_defaults(run=register)
    entry.add_argument('--asset',required=True);entry.add_argument('--version',required=True);entry.add_argument('--source',required=True)
    entry.add_argument('--legacy',action='store_true');entry.add_argument('--prompts',default=str(ROOT/'docs/art-assets/first-batch-prompts.json'))
    entry.add_argument('--reference',action='append',default=[])
    entry=commands.add_parser('review');entry.set_defaults(run=review)
    entry.add_argument('--asset',required=True);entry.add_argument('--version',required=True);entry.add_argument('--accept',action='store_true');entry.add_argument('--note',required=True)
    entry=commands.add_parser('register-material');entry.set_defaults(run=register_material)
    entry.add_argument('--asset',required=True);entry.add_argument('--version',required=True);entry.add_argument('--source',required=True)
    entry.add_argument('--tile-metres',type=float,required=True);entry.add_argument('--prompts',default=str(ROOT/'assets/art/materials/source-prompts-v1.json'))
    entry=commands.add_parser('sheet');entry.set_defaults(run=sheet)
    entry.add_argument('--version');entry.add_argument('--output',default='output/art-production-20261026/review')
    args=parser.parse_args();args.run(args)


if __name__=='__main__':
    main()
