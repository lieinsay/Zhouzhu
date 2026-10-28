"""按原字节拆出GLB纹理，便于指纹与PCK依赖核对；几何、蒙皮和动画缓冲字节不变。"""
import json,struct,pathlib,sys,hashlib

def externalize(source,destination):
    source=pathlib.Path(source);destination=pathlib.Path(destination);data=source.read_bytes()
    magic,version,total=struct.unpack_from('<III',data)
    if magic!=0x46546c67 or version!=2 or total!=len(data):raise ValueError('无效GLB头')
    at=12;chunks={}
    while at<len(data):
        count,kind=struct.unpack_from('<II',data,at);chunks[kind]=data[at+8:at+8+count];at+=8+count
    model=json.loads(chunks[0x4e4f534a]);binary=chunks[0x004e4942];removed=set();records=[]
    destination.parent.mkdir(parents=True,exist_ok=True)
    for i,image in enumerate(model.get('images',[])):
        if 'bufferView' not in image:continue
        index=image.pop('bufferView');view=model['bufferViews'][index];offset=view.get('byteOffset',0);content=binary[offset:offset+view['byteLength']]
        suffix='.png' if image.get('mimeType')=='image/png' else '.jpg'
        relative='textures/image-'+str(i)+suffix;path=destination.parent/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(content)
        image['uri']=relative;removed.add(index);records.append(dict(path=relative,sha256=hashlib.sha256(content).hexdigest()))
    remap={old:new for new,old in enumerate(i for i in range(len(model['bufferViews'])) if i not in removed)}
    views=[];output=bytearray()
    for old,view in enumerate(model['bufferViews']):
        if old in removed:continue
        padding=-len(output)%4;output.extend(b'\0'*padding);view=dict(view);offset=view.get('byteOffset',0);view['byteOffset']=len(output)
        output.extend(binary[offset:offset+view['byteLength']]);views.append(view)
    def update(value):
        if isinstance(value,dict):
            if 'bufferView' in value:value['bufferView']=remap[value['bufferView']]
            for key,child in value.items():
                if key!='bufferViews':update(child)
        elif isinstance(value,list):
            for child in value:update(child)
    update(model);model['bufferViews']=views;model['buffers']=[dict(byteLength=len(output))]
    encoded=json.dumps(model,ensure_ascii=False,separators=(',',':')).encode('utf8');encoded+=b' '*(-len(encoded)%4);output+=b'\0'*(-len(output)%4)
    destination.write_bytes(struct.pack('<III',magic,version,12+16+len(encoded)+len(output))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(output),0x004e4942)+output)
    return records

if __name__=='__main__':
    print(json.dumps(externalize(sys.argv[1],sys.argv[2]),indent=2))
