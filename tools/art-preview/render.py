"""真实网格的确定性透明正交预览；原几何、米制尺寸和颜色不变。需要 numpy、Pillow。"""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/content-studio'))
from studio.catalog import load_catalog, parse_initializer
from studio.common import atomic_json,read_json,timestamp


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render(mesh,path):
    size=512
    positions=np.concatenate([np.asarray(p['positions']).reshape(-1,3) for p in mesh['parts']])
    camera=np.array([1.1,0.85,1.35]);camera/=np.linalg.norm(camera)
    right=np.cross([0,1,0],camera);right/=np.linalg.norm(right)
    up=np.cross(camera,right)
    basis=np.asarray([right,-up,camera])
    projected=positions@basis.T
    lo=projected[:,:2].min(axis=0);hi=projected[:,:2].max(axis=0)
    scale=(size*.8)/max(hi-lo);center=(lo+hi)/2
    zbuffer=np.full((size,size),-np.inf)
    pixels=np.zeros((size,size,4),dtype=np.float64)
    light=np.array([-0.5,0.85,0.65]);light/=np.linalg.norm(light)
    for part in mesh['parts']:
        vertices=np.asarray(part['positions']).reshape(-1,3)
        normals=np.asarray(part['normals']).reshape(-1,3)
        uvz=vertices@basis.T;uvz[:,:2]=(uvz[:,:2]-center)*scale+size/2
        base=np.tile(part.get('color',[.6,.6,.6])[:3],(len(vertices),1))
        if part.get('colors'):base*=np.asarray(part['colors']).reshape(-1,4)[:,:3]
        shade=.55+.45*np.maximum(0,normals@light)
        colors=np.clip(base*shade[:,None],0,1)*255
        for triangle in np.asarray(part['indices']).reshape(-1,3):
            points=uvz[triangle];col=colors[triangle]
            x0,y0=np.maximum(0,np.floor(points[:,:2].min(axis=0)).astype(int))
            x1,y1=np.minimum(size-1,np.ceil(points[:,:2].max(axis=0)).astype(int))
            if x1<x0 or y1<y0:continue
            a,b,c=points
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-9:continue
            xx,yy=np.meshgrid(np.arange(x0,x1+1)+.5,np.arange(y0,y1+1)+.5)
            w0=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
            w1=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
            w2=1-w0-w1
            depth=w0*a[2]+w1*b[2]+w2*c[2]
            region=zbuffer[y0:y1+1,x0:x1+1]
            mask=(w0>=-1e-8)&(w1>=-1e-8)&(w2>=-1e-8)&(depth>region)
            region[mask]=depth[mask]
            rgb=w0[:,:,None]*col[0]+w1[:,:,None]*col[1]+w2[:,:,None]*col[2]
            target=pixels[y0:y1+1,x0:x1+1];target[mask,:3]=rgb[mask];target[mask,3]=255
    image=Image.fromarray(pixels.astype('uint8'),'RGBA')
    path.parent.mkdir(parents=True,exist_ok=True);image.save(path)
    return dict(camera=[1.1,.85,1.35],projection='orthographic',fit=.8,light=[-.5,.85,.65],ambient=.55,
                background='transparent',sourceBounds=dict(min=positions.min(axis=0).tolist(),max=positions.max(axis=0).tolist()),
                rasterizer='tools/art-preview/render.py; z-buffer, original vertex normals/colors, no geometry alteration')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--furniture-directory',required=True)
    parser.add_argument('--model-directory',default='tools/content-studio/public/assets/models')
    parser.add_argument('--version',default='v1')
    parser.add_argument('--rendered-directory',help='使用scene_visuals_e2e在真实Godot着色器下渲染的512透明PNG，保留其像素')
    parser.add_argument('--namespace',choices=['building','module','furniture'],help='仅为受影响类别新增预览版本')
    args=parser.parse_args()
    catalog=load_catalog(ROOT)
    model_directory=ROOT/args.model_directory
    manifest=read_json(model_directory/'manifest.json')
    definitions={namespace+'.'+row['id']:row for namespace,collection in [('module','modules'),('building','buildings')] for row in catalog[collection]}
    source=ROOT/'src/house_template.cpp'
    for node in parse_initializer(source.read_text(encoding='utf-8'),'DEFS','src/house_template.cpp'):
        values=[field.value for field in node.value]
        definitions['furniture.'+values[0]]=dict(id=values[0],name=values[1],width=values[2],depth=values[3],height=values[4])
    registry=read_json(ROOT/'assets/art/registry.json')
    if args.namespace:definitions={key:value for key,value in definitions.items() if key.startswith(args.namespace+'.')}
    if not __import__('re').fullmatch(r'v[1-9][0-9]*',args.version):raise ValueError('版本必须为vN')
    existing={a['id']:a for a in registry['assets']}
    evidence=[]
    for key,definition in definitions.items():
        if key in existing and any(v['version']==args.version for v in existing[key]['versions']):raise ValueError('不能覆盖已有预览版本 '+key)
        namespace,id=key.split('.')
        if namespace=='furniture':
            mesh_path=Path(args.furniture_directory)/f'{id}.json'
            model=dict(id='furniture:'+id,source={'path':'src/building_parts.cpp','sha256':sha(ROOT/'src/building_parts.cpp')},
                note='直接调用当前furniture_parts与art_mesh_kit，完整家具曲面、原颜色和米制尺寸。')
        else:
            model=next(m for m in manifest['models'] if m['id']==namespace+':'+id)
            mesh_path=model_directory/Path(model['url']).name
            if sha(mesh_path)!=model['assetSha256']:raise ValueError('模型文件指纹变化 '+key)
            if sha(ROOT/model['source']['path'])!=model['source']['sha256']:raise ValueError('模型主要生成源码已经变化，请重新导出 '+key)
        mesh=read_json(mesh_path)
        original=ROOT/'assets/art/previews'/namespace/id/args.version/'render.png'
        if args.rendered_directory:
            import shutil
            rendered=ROOT/args.rendered_directory
            report=read_json(rendered/'report.json')
            if not report.get('passed') or not report.get('pixelCapture') or not report.get('models',{}).get(key,{}).get('pixels'):raise ValueError('真实GPU渲染未完成 '+key)
            if sha(Path(report['models'][key]['file']))!=sha(mesh_path):raise ValueError('截图来源与待登记的真实网格不一致 '+key)
            image=Image.open(rendered/(key+'.png'))
            if image.size!=(512,512) or image.mode!='RGBA' or image.getchannel('A').getextrema()!=(0,255):raise ValueError('预览需要512方形真实透明PNG '+key)
            if original.exists():raise ValueError('不能覆盖原始预览 '+key)
            original.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(rendered/(key+'.png'),original)
            parameters=dict(camera=[1.1,.85,1.35],projection='orthographic',fit=.8,keyLight=dict(rotationDegrees=[-45,-30,0],energy=1.1),ambient=.7,background='transparent',renderer='Godot4.4.1 / real production shaders and bindings',sourceBounds=report['models'][key]['bounds'],surfaceBindingsSha256=sha(ROOT/'game/art/bindings.json'))
        else:parameters=render(mesh,original)
        runtime=ROOT/'game/art/previews'/namespace/id/(args.version+'.png');runtime.parent.mkdir(parents=True,exist_ok=True)
        Image.open(original).convert('RGBa').resize((256,256),Image.Resampling.LANCZOS).convert('RGBA').save(runtime)
        model_copy=original.parent/'model.json';model_copy.write_bytes(mesh_path.read_bytes())
        record=dict(version=args.version,styleId=registry['styleId'],origin='model-render',productionStatus='produced',generatedAt=timestamp(),
            original=dict(path=original.relative_to(ROOT).as_posix(),sha256=sha(original),width=512,height=512),
            runtime=dict(path=runtime.relative_to(ROOT).as_posix(),sha256=sha(runtime),width=256,height=256),
            prompt='真实模型统一正交相机与柔光渲染；不使用AI重绘。',references=[],
            derivation=dict(operation='model-render-and-premultiplied-resize',parameters=parameters),
            model=dict(**model,path=model_copy.relative_to(ROOT).as_posix(),sha256=sha(model_copy)),
            sourceSnapshotManifest=(model_directory/'manifest.json').relative_to(ROOT).as_posix() if namespace!='furniture' else None,
            sourceFingerprints=[dict(path=str(p),sha256=sha(ROOT/p)) for p in ['src/house_template.cpp','src/building_parts.cpp','src/building_mesh.cpp','src/ship_layout.cpp','src/ship.cpp']],
            styleGap='已接入正式共享材质、构件UV和曲面；保留原尺寸与结构。仍为程序构件模型，后续可精修榫卯、曲线与手绘局部细节。' if args.rendered_directory else '当前程序模型主要为简化几何与纯色；尚缺精绘材质、细致接合和轮廓设计。此图仅准确预览现有模型，不标作风格化模型完成。',
            userConfirmation=None,agentReview=dict(status='pending',note='待核对完整主体、相机、尺寸及原模型来源'))
        if key in existing:existing[key]['versions'].append(record)
        else:registry['assets'].append(dict(id=key,name=definition['name'],definition='src/house_template.cpp' if namespace=='furniture' else 'src/ship_layout.cpp' if namespace=='module' else 'src/buildings.cpp',
            useSites=['content-studio:formal-art',namespace+' palette'],versions=[record]))
        evidence.append(dict(id=key,name=definition['name'],definition=definition,sourceModel=model,styleGap=record['styleGap']))
    atomic_json(ROOT/'assets/art/registry.json',registry)
    combined={row['id']:row for row in read_json(ROOT/'docs/art-assets/model-gaps.json',{}).get('previews',[])}
    combined.update({row['id']:row for row in evidence})
    atomic_json(ROOT/'docs/art-assets/model-gaps.json',dict(schemaVersion=1,previews=list(combined.values())))
    out=ROOT/('output/scene-art-20261027/model-previews-'+args.version if args.rendered_directory else 'output/art-production-20261026/model-previews');out.mkdir(parents=True,exist_ok=True)
    for page in range(0,len(evidence),8):
        sheet=Image.new('RGB',(960,500),'#cbd4db');draw=ImageDraw.Draw(sheet)
        for at,row in enumerate(evidence[page:page+8]):
            namespace,id=row['id'].split('.');image=Image.open(ROOT/'game/art/previews'/namespace/id/(args.version+'.png'))
            image.thumbnail((220,215));x=(at%4)*240;y=(at//4)*250
            sheet.paste(image,(x+10,y+10),image);draw.text((x+10,y+230),row['id'],fill='#202b33')
        sheet.save(out/f'page-{page//8+1}.png')
    print(str(len(evidence))+' actual model previews rendered; style gaps registered')


if __name__=='__main__':main()
