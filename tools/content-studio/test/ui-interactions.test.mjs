import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {loadCatalog} from '../lib/catalog.mjs';
import {validateProject,diffProjects,ROW_SHAPES} from '../lib/validation.mjs';
import * as media from '../public/media.js';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../..');
const baseline=await loadCatalog(root);
const app=await readFile(resolve(root,'tools/content-studio/public/app.js'),'utf8');
const source=app.slice(0,app.lastIndexOf('try{applyWorkspace')).replace(/^import[^\n]+\n/,'');
const library=media.normalizeLibrary({
  thumbnails:baseline.items.map(row=>({id:row.id,itemId:row.id,name:row.name,url:`/assets/thumbnails/${row.id}-v1.png`})),
  models:[{id:'prop:workbench',name:'工作台',kind:'game',usage:'placed',format:'mesh-json',url:'/assets/models/prop-workbench.json',collection:'items',recordIds:['workbench'],itemIds:['workbench']}],
});

// DOM recreation deliberately resets scroll/focus. Regressions exercise the
// production handlers across that boundary; actual browser checks complement it.
function frontend(){
  const nodes=new Map(), fields=new Map(), handlers=new Map(), requests=[];
  const stats={pageRenders:0,editorRenders:0};
  const win={scrollX:0,scrollY:0,addEventListener(){},scrollTo({left,top}){this.scrollX=left;this.scrollY=top;}};
  const doc={activeElement:null,querySelector(selector){return nodes.get(selector)||fields.get(selector)||null;},querySelectorAll(selector){if(selector==='.record')return nodes.get('#record-list')?.buttons||[];if(selector==='#editor details')return nodes.get('#editor')?.details||[];return [];},addEventListener(name,callback){handlers.set(name,callback);}};
  const decode=value=>String(value??'').replace(/&quot;/g,'"').replace(/&#39;/g,"'").replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&');
  class Element {
    constructor(tag='DIV',id=''){this.tagName=tag;this.id=id;this.dataset={};this.value='';this.textContent='';this.className='';this.scrollTop=0;this.checked=false;this.open=false;this.buttons=[];this.details=[];this._html='';this.selectionStart=0;this.selectionEnd=0;this.classList={toggle:()=>{}};}
    addEventListener(){} setAttribute(key,value){this[key]=value;} getAttribute(key){return this[key]??null;}
    focus(){doc.activeElement=this;} setSelectionRange(start,end){this.selectionStart=start;this.selectionEnd=end;}
    showModal(){this.open=true;}close(){this.open=false;}
    querySelector(selector){if(this.id==='modal')return this.actions[selector];return null;}
    get innerHTML(){return this._html;}
    set innerHTML(value){
      this._html=value;this.scrollTop=0;
      if(this.id==='app'){
        stats.pageRenders++;win.scrollY=0;doc.activeElement=null;
        for(const [key,node] of nodes)if(!['#app','#toast','#modal','#import-file','#model-file'].includes(key))nodes.delete(key);
        for(const match of value.matchAll(/id="([\w-]+)"/g))if(!nodes.has('#'+match[1]))nodes.set('#'+match[1],new Element('DIV',match[1]));
        if(nodes.has('#record-list'))nodes.get('#record-list').innerHTML=value;
        if(nodes.has('#editor'))nodes.get('#editor').innerHTML=value;
      }else if(this.id==='editor'){
        stats.editorRenders++;fields.clear();
        for(const match of value.matchAll(/<(input|select|textarea)\b([^>]+)>/g)){
          const node=new Element(match[1].toUpperCase());
          const attrs=[...match[2].matchAll(/([\w-]+)="([^"]*)"/g)];
          for(const [,key,data] of attrs){if(key.startsWith('data-')){const name=key.slice(5).replace(/-([a-z])/g,(_,letter)=>letter.toUpperCase());node.dataset[name]=decode(data);}else node[key]=decode(data);}
          node.readOnly=/\breadonly\b/.test(match[2]);
          if(/\bdata-viewer-wire\b/.test(match[2]))node.dataset.viewerWire='';
          const keys=['field','visual','axis','ingredient','key'];
          const selector=keys.filter(key=>node.dataset[key]!==undefined).map(key=>`[data-${key}="${node.dataset[key]}"]`).join('');
          if(selector)fields.set(selector,node);
          if('viewerWire'in node.dataset)nodes.set('[data-viewer-wire]',node);
        }
        this.details=value.includes('<details')?[new Element('DETAILS')]:[];
        if(value.includes('thumbnail-mini-grid'))nodes.set('#editor .thumbnail-mini-grid',new Element());
        if(value.includes('id="model-canvas"'))nodes.set('#model-canvas',new Element('CANVAS','model-canvas'));else nodes.delete('#model-canvas');
      }else if(this.id==='record-list'){
        this.buttons=[...value.matchAll(/<button class="record [^"]*"[^>]*data-index="(\d+)"/g)].map(match=>{const node=new Element('BUTTON');node.dataset.index=match[1];return node;});
      }else if(this.id==='modal'){
        this.actions={'[data-modal="cancel"]':new Element('BUTTON'),'[data-modal="confirm"]':new Element('BUTTON')};
      }
    }
  }
  for(const id of ['app','toast','modal','import-file','model-file'])nodes.set('#'+id,new Element('DIV',id));
  class Viewer {
    constructor(){this.prepared=null;this.disposed=false;this.wireframe=false;}
    async load(){this.prepared={};this.yaw=.72;this.pitch=.45;this.distance=5;}
    dispose(){this.disposed=true;} setWireframe(value){this.wireframe=value;}schedule(){}setTransform(){}
  }
  const context=vm.createContext({structuredClone,validateProject,diffProjects,ROW_SHAPES,document:doc,window:win,setTimeout:()=>0,clearTimeout(){},injectedMedia:media,injectedLibrary:library,injectedViewer:Viewer,initialWorkspace:{baseline,project:baseline,revision:'initial'},fetch(path,options){if(path==='/api/assets')return Promise.resolve({ok:false,status:503,json:async()=>({error:'资产服务暂不可用'})});return new Promise(resolveRequest=>requests.push({path,body:JSON.parse(options.body),resolveRequest}));}});
  vm.runInContext(source,context);
  const run=text=>vm.runInContext(text,context);
  run('mediaToolkit=injectedMedia;assetLibrary=injectedLibrary;assetsReady=true;viewerClass=injectedViewer;applyWorkspace(initialWorkspace)');
  return {run,read:text=>structuredClone(run(text)),nodes,fields,doc,win,stats,handlers,requests,
    input(node,value){node.value=value;handlers.get('input')({target:node});},
    async action(action,data={}){await run(`performAction({dataset:${JSON.stringify({action,...data})}})`);},
    completeSave(){const r=requests.at(-1);r.resolveRequest({ok:true,json:async()=>({baseline,project:structuredClone(r.body.project),revision:'saved'})});},
    async flush(){await Promise.resolve();await Promise.resolve();},
  };
}

test('切换清单物品保持外观页签、原列表节点和滚动位置，重复点击不重建编辑器',async()=>{
  const f=frontend();f.run("navigate('items');state.tab='visual';refreshEditor()");
  const list=f.nodes.get('#record-list');list.scrollTop=880;f.win.scrollY=340;
  const renders=f.stats.pageRenders;
  await f.action('select',{index:'31'});
  assert.equal(f.read('state.selected'),'workbench');assert.equal(f.read('state.tab'),'visual');
  assert.equal(f.nodes.get('#record-list'),list);assert.equal(list.scrollTop,880);assert.equal(f.win.scrollY,340);
  assert.equal(f.stats.pageRenders,renders);
  const editorRenders=f.stats.editorRenders;await f.action('select',{index:'31'});
  assert.equal(f.stats.editorRenders,editorRenders);
});

test('编辑定义及新ID不会重置清单滚动，标题ID即时更新',()=>{
  const f=frontend();f.run("navigate('items');newRecord()");
  const list=f.nodes.get('#record-list');list.scrollTop=700;f.win.scrollY=220;
  const id=f.fields.get('[data-field="id"]');f.input(id,'fresh_item');
  assert.equal(list.scrollTop,700);assert.equal(f.win.scrollY,220);
  assert.equal(f.nodes.get('#editor-id').textContent,'fresh_item');
  assert.equal(f.read('selected().id'),'fresh_item');
});

test('保存后保留页签、输入焦点和选区，以及页面和清单滚动',async()=>{
  const f=frontend();f.run("navigate('items','workbench')");
  const field=f.fields.get('[data-field="name"]');field.focus();field.setSelectionRange(1,3);
  f.nodes.get('#record-list').scrollTop=420;f.win.scrollY=680;
  const pending=f.run('save()');f.completeSave();await pending;
  assert.equal(f.doc.activeElement,f.fields.get('[data-field="name"]'));
  assert.notEqual(f.doc.activeElement,field);assert.equal(f.doc.activeElement.selectionStart,1);assert.equal(f.doc.activeElement.selectionEnd,3);
  assert.equal(f.nodes.get('#record-list').scrollTop,420);assert.equal(f.win.scrollY,680);
  assert.equal(f.read('state.tab'),'edit');
});

test('选择缩略图后保持展开的图库、图库滚动和同一模型视角/线框',async()=>{
  const f=frontend();f.run("navigate('items','workbench');state.tab='visual';refreshEditor()");await f.flush();
  f.run('modelViewer.yaw=1.2;modelViewer.pitch=.2;modelViewer.distance=8;modelViewer.wireframe=true');
  f.nodes.get('#editor').details[0].open=true;f.nodes.get('#editor .thumbnail-mini-grid').scrollTop=280;
  f.nodes.get('#record-list').scrollTop=600;f.win.scrollY=730;
  await f.action('visual-pick',{thumbnail:'timber'});await f.flush();
  assert.equal(f.nodes.get('#editor').details[0].open,true);assert.equal(f.nodes.get('#editor .thumbnail-mini-grid').scrollTop,280);
  assert.deepEqual(f.read('[modelViewer.yaw,modelViewer.pitch,modelViewer.distance,modelViewer.wireframe]'),[1.2,.2,8,true]);
  assert.equal(f.nodes.get('[data-viewer-wire]').checked,true);assert.equal(f.nodes.get('#record-list').scrollTop,600);assert.equal(f.win.scrollY,730);
});

test('跨集合保留适用的外观页签，返回后恢复用户选过的页签',async()=>{
  const f=frontend();f.run("navigate('items')");await f.action('tab',{tab:'visual'});
  f.run("navigate('modules')");assert.equal(f.read('state.tab'),'visual');
  f.run("navigate('recipes')");assert.equal(f.read('state.tab'),'edit');
  f.run("navigate('items')");assert.equal(f.read('state.tab'),'visual');
  await f.action('select',{index:'10'});assert.equal(f.read('state.tab'),'visual');
});

test('筛选配方会选择筛选内的条目并保留当前页签，不重建整页',async()=>{
  const f=frontend();f.run("state.tab='source';refreshEditor()");
  const renders=f.stats.pageRenders;f.nodes.get('#record-list').scrollTop=140;f.win.scrollY=300;
  await f.action('filter',{filter:'KILN'});
  assert.equal(f.read('selected().station'),'KILN');assert.equal(f.read('state.tab'),'source');
  assert.equal(f.nodes.get('#record-list').scrollTop,0);assert.equal(f.win.scrollY,300);assert.equal(f.stats.pageRenders,renders);
});

test('图库支持名称和ID搜索，资产服务失败时不误报刷新成功',async()=>{
  const f=frontend();f.run("navigate('items');state.itemView='gallery';render()");
  const search=f.nodes.get('#gallery-search');search.tagName='INPUT';search.id='gallery-search';
  f.input(search,'workbench');const html=f.nodes.get('#gallery-grid').innerHTML;
  assert.match(html,/工作台/);assert(!html.includes('data-id="millet"'));
  await f.action('assets-refresh');
  assert.match(f.nodes.get('#toast').textContent,/刷新失败/);assert.match(f.nodes.get('#toast').className,/error/);
});

test('确认删除期间保存替换对象后仍删除原目标，且模态框内不触发全局保存快捷键',async()=>{
  const f=frontend();f.run("navigate('items','bean')");
  const pending=f.run('save()');await f.action('delete');
  const modal=f.nodes.get('#modal');assert.equal(modal.open,true);
  f.handlers.get('keydown')({ctrlKey:true,key:'s',target:{tagName:'INPUT'},preventDefault(){}});
  assert.equal(f.requests.length,1);
  f.completeSave();await pending;
  await modal.actions['[data-modal="confirm"]'].onclick();
  assert(!f.read('project.items').some(row=>row.id==='bean'));
  assert.equal(f.read('project.items.at(-1).id'),'ladder');assert.equal(f.read('project.items.length'),48);
});
