"""Blender：作者器物适配与原有定制家具精修，输出真实GLB与编辑源，不改占格/配方。"""
import bpy,bmesh,json,pathlib,sys,math
from mathutils import Vector
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/content-studio'))
from studio.catalog import parse_initializer
OUT=ROOT/'build/prepared-furniture';OUT.mkdir(parents=True,exist_ok=True)
ORIGINAL=ROOT/'assets/art/models/furniture-scene-20261027-v2'
EXTERNAL=ROOT/'assets/art/external/polyhaven/20261027'
DEFS={}
for entry in parse_initializer((ROOT/'src/house_template.cpp').read_text(encoding='utf8'),'DEFS','src/house_template.cpp'):
    v=[node.value for node in entry.value];DEFS[v[0]]=dict(name=v[1],size=[v[2],v[3],v[4]])
SURFACES=[node.value for node in parse_initializer((ROOT/'src/shared_looks.cpp').read_text(encoding='utf8'),'surfaces','src/shared_looks.cpp')]
AUTHORED={'bed':'chinese_sofa','table':'chinese_tea_table','bench':'painted_wooden_bench','chest':'chinese_cabinet','vat':'ceramic_pot','bin':'wicker_basket_02','altar':'chinese_console_table','shelf':'painted_wooden_shelves','sacks':'compost_bag_02'}

def bounds(objects):
    points=[obj.matrix_world@v.co for obj in objects for v in obj.data.vertices]
    return Vector([min(p[k] for p in points)for k in range(3)]),Vector([max(p[k] for p in points)for k in range(3)])

def fit(objects,size,center=False):
    lo,hi=bounds(objects);target=Vector((size[0],size[1],size[2]));span=hi-lo;middle=(lo+hi)*.5
    for obj in objects:
        matrix=obj.matrix_world.copy()
        for vertex in obj.data.vertices:
            point=matrix@vertex.co;point.x=(point.x-middle.x)*target.x/span.x;point.y=(point.y-middle.y)*target.y/span.y;point.z=(point.z-lo.z)*target.z/span.z-(target.z*.5 if center else 0);vertex.co=point
        obj.parent=None;obj.matrix_world.identity()
        if obj.data.has_custom_normals:obj.data.normals_split_custom_set([(0,0,0)]*len(obj.data.loops))

def project_material(index,color,rough,infos):
    name='ZZ_project_'+str(index);mat=bpy.data.materials.get(name)
    if mat is None:
        mat=bpy.data.materials.new(name);mat.use_nodes=True;node=mat.node_tree.nodes.get('Principled BSDF');node.inputs['Base Color'].default_value=(*color,1);node.inputs['Roughness'].default_value=rough
    infos[name]=dict(asset=SURFACES[index],color=color,roughness=rough,metallic=.8 if index==12 else 0)
    return mat

def native(kind,infos,selected=None):
    data=json.loads((ORIGINAL/(kind+'.json')).read_text(encoding='utf8'));objects=[]
    for i,part in enumerate(data['parts']):
        if selected is not None and int(part['materialIndex']) not in selected:continue
        vertices=[(part['positions'][at],-part['positions'][at+2],part['positions'][at+1]) for at in range(0,len(part['positions']),3)]
        faces=[tuple(part['indices'][at:at+3])for at in range(0,len(part['indices']),3)]
        mesh=bpy.data.meshes.new(kind+'_'+str(i));mesh.from_pydata(vertices,[],faces);mesh.update()
        for key,label in [('uvs','Metric'),('uv2s','SurfaceMode')]:
            if len(part.get(key,[]))!=len(vertices)*2:continue
            uv=mesh.uv_layers.new(name=label)
            for loop in mesh.loops:uv.data[loop.index].uv=(part[key][loop.vertex_index*2],1-part[key][loop.vertex_index*2+1])
        mesh.materials.append(project_material(int(part['materialIndex']),part['color'],part['roughness'],infos))
        ob=bpy.data.objects.new(mesh.name,mesh);bpy.context.scene.collection.objects.link(ob);objects.append(ob)
        bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000001);bm.to_mesh(mesh);bm.free();mesh.update()
    return objects

def bevel(ob,width=.012):
    bpy.context.view_layer.objects.active=ob;modifier=ob.modifiers.new('Controlled edge finishing','BEVEL');modifier.width=width;modifier.segments=3;modifier.limit_method='ANGLE';modifier.angle_limit=.7
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    normal=ob.modifiers.new('Weighted construction normals','WEIGHTED_NORMAL');normal.keep_sharp=True;bpy.ops.object.modifier_apply(modifier=normal.name)

def box(name,size,at,material):
    bpy.ops.mesh.primitive_cube_add(size=1,location=at);ob=bpy.context.object;ob.name=name;ob.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);ob.data.materials.append(material);bevel(ob,min(.008,min(size)*.2));return ob

def boolean(ob,cutter):
    bpy.context.view_layer.objects.active=ob;modifier=ob.modifiers.new('Functional opening','BOOLEAN');modifier.operation='DIFFERENCE';modifier.solver='EXACT';modifier.object=cutter;bpy.ops.object.modifier_apply(modifier=modifier.name);bpy.data.objects.remove(cutter,do_unlink=True)

def detail_native(kind,objects,infos):
    if kind=='stove':
        clay=objects[0]
        for x,r in [(-.4,.195),(.35,.14)]:
            bpy.ops.mesh.primitive_cylinder_add(vertices=40,radius=r,depth=.34,location=(x,-.10,.84));boolean(clay,bpy.context.object)
        bpy.ops.mesh.primitive_cube_add(size=1,location=(0,-.49,.24));cutter=bpy.context.object;cutter.dimensions=(.48,.36,.22);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);boolean(clay,cutter)
        for obj in objects:bevel(obj,.016)
    if kind=='loom':
        wood=project_material(3,[.5,.36,.22],.85,infos)
        # 原框架上的榫接销；不增加动力、织造机构或生产数据。
        for x in [-.70,.70]:
            for y in [-.52,.52]:
                for z in [.18,1.12]:
                    bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.014,depth=.018,location=(x,y-.008,z),rotation=(math.pi*.5,0,0));pin=bpy.context.object;pin.name='FrameJoinPeg';pin.data.materials.append(wood);bevel(pin,.002);objects.append(pin)
    if kind=='drum':
        for obj in objects:bevel(obj,.006)
    if kind=='mat':
        straw=project_material(8,[.80,.68,.4],1.0,infos)
        # 边缝位于既定2x1.4占地内，厚度仍约3cm。
        for x in [-.988,.988]:objects.append(box('WovenBinding',(.012,1.388,.018),(x,0,.018),straw))

def uv_density(objects):
    data={}
    for obj in objects:
        uv=obj.data.uv_layers.active
        if not uv:continue
        obj.data.calc_loop_triangles()
        for triangle in obj.data.loop_triangles:
            name=obj.data.materials[triangle.material_index].name
            a,b,c=[obj.matrix_world@obj.data.vertices[v].co for v in triangle.vertices]
            u,v,w=[uv.data[i].uv for i in triangle.loops];area=abs((v.x-u.x)*(w.y-u.y)-(v.y-u.y)*(w.x-u.x))*.5
            row=data.setdefault(name,[0,0]);row[0]+=(b-a).cross(c-a).length*.5;row[1]+=area
    return {key:math.sqrt(value[0]/value[1]) for key,value in data.items() if value[1]>1e-8}

def main():
    for kind in [*DEFS,'cargo_crate']:
        bpy.ops.wm.read_factory_settings(use_empty=True);infos={};source=AUTHORED.get(kind)
        if kind=='cargo_crate':source='wooden_crate_02'
        size=DEFS[kind]['size'] if kind in DEFS else [.8,.7,.6]
        appearance=dict(color=[.5,.36,.22],roughness=.88,normalStrength=.18,tileMetres=.5,projectMaterials=infos)
        if source:
            path=EXTERNAL/source/(source+'_1k.gltf');bpy.ops.import_scene.gltf(filepath=str(path))
            objects=[o for o in bpy.context.scene.objects if o.type=='MESH' and not o.hide_render]
            if kind=='vat':
                # 作者模型的盖盘/盖钮是独立连通片，按源文件米制高度移除；水缸保留真实腹体、口沿和把手。
                bpy.ops.object.select_all(action='DESELECT')
                for obj in objects:obj.select_set(True)
                bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.separate(type='LOOSE');bpy.ops.object.mode_set(mode='OBJECT')
                objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
                for obj in list(objects):
                    lo,hi=bounds([obj])
                    if lo.z>.30:bpy.data.objects.remove(obj,do_unlink=True);objects.remove(obj)
                # 合并原UV接缝后加内壁与真实口沿，避免从上方看穿单面壳；不添加水位或储量数据。
                bpy.ops.object.select_all(action='DESELECT')
                for obj in objects:obj.select_set(True)
                bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join();objects=[bpy.context.object]
                bm=bmesh.new();bm.from_mesh(objects[0].data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00003);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(objects[0].data);bm.free()
                modifier=objects[0].modifiers.new('Water jar inner wall','SOLIDIFY');modifier.thickness=.007;modifier.offset=-1;bpy.ops.object.modifier_apply(modifier=modifier.name)
            fit(objects,size,kind=='cargo_crate')
            if kind=='vat':appearance.update(projectMaterial='material.terracotta',color=[.60,.40,.28],tileMetres=.5)
            if kind=='bin':appearance.update(projectMaterial='material.straw',color=[.80,.68,.40],tileMetres=.5);objects+=native(kind,infos,{9})
            if kind=='sacks':appearance.update(projectMaterial='material.hemp',color=[.66,.60,.50],tileMetres=.25)
            if kind=='bed':
                layers={}
                for obj in objects:
                    for poly in obj.data.polygons:
                        if poly.normal.z>.85:
                            z=round((obj.matrix_world@poly.center).z,2);layers[z]=layers.get(z,0)+poly.area
                seat=max(layers,key=layers.get) if layers else .33
                cloth=native(kind,infos,{7});lo,hi=bounds(cloth)
                for obj in cloth:
                    for vertex in obj.data.vertices:vertex.co.z+=seat-lo.z+.005
                objects+=cloth
            if kind=='altar':
                wood=project_material(3,[.50,.36,.22],.85,infos)
                for x in [-.48,0,.48]:
                    objects.append(box('TabletBase',(.20,.10,.045),(x,.16,size[2]+.0225),wood));objects.append(box('ExistingAncestralTablet',(.14,.045,.42),(x,.16,size[2]+.25),wood))
        else:
            objects=native(kind,infos);detail_native(kind,objects,infos)
        appearance['projectMaterials']=infos;appearance['uvMetresPerUnit']=uv_density(objects)
        for obj in objects:
            obj['art_definition_id']='prop.cargo_crate' if kind=='cargo_crate' else 'furniture.'+kind
            obj['art_source']='Poly Haven / CC0' if source else 'Zhouzhu current mesh / project-owned refinement'
        bpy.ops.object.select_all(action='DESELECT')
        for obj in objects:obj.select_set(True)
        bpy.context.view_layer.objects.active=objects[0]
        options=dict(filepath=str(OUT/(kind+'.glb')),export_format='GLB',use_selection=True,export_animations=False,export_skins=False,export_extras=True,export_yup=True,export_texcoords=True)
        if 'export_all_uvs' in bpy.ops.export_scene.gltf.get_rna_type().properties:options['export_all_uvs']=True
        bpy.ops.export_scene.gltf(**options)
        for image in bpy.data.images:
            if image.has_data and image.name!='Render Result':image.pack()
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(kind+'.blend')))
        lo,hi=bounds(objects);report=dict(kind=kind,sourceAsset=source,origin='author-model-adaptation' if source else 'project-mesh-blender-refinement',appearance=appearance,sourceDefinition='src/house_template.cpp',expectedWidthDepthHeight=[size[0],size[1],size[2]],actualBoundsZUp=dict(min=list(lo),max=list(hi)),triangles=sum(sum(len(p.vertices)-2 for p in obj.data.polygons)for obj in objects))
        (OUT/(kind+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        print('PREPARED_PROP',kind,report['origin'],report['triangles'],flush=True)

if __name__=='__main__':main()
