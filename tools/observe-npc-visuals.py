"""正常时间/天气下观察真实NPC。仅隔离验收文件和移动观察者，不改NPC或玩法数据。"""
import argparse,pathlib,subprocess,json,time,re,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scene',choices=['seed42','main','flat'],default='seed42');p.add_argument('--duration',type=float,default=180);p.add_argument('--visible',action='store_true');p.add_argument('--output',required=True);p.add_argument('--village',type=int)
    a=p.parse_args()
    if not 10<=a.duration<=1800:p.error('观察时长10～1800秒，按正常时间运行')
    output=(ROOT/a.output).resolve()
    if not output.is_relative_to(ROOT/'output'):p.error('验收输出必须在项目output内，避免覆盖存档')
    if (output/'report.json').exists():p.error('验收目录已有报告，使用新目录保留历史')
    output.mkdir(parents=True,exist_ok=True)
    rider=(ROOT/'.run/zhouzhu.run.xml').read_text(encoding='utf8');engine=re.search(r'RUN_PATH="([^"]+)"',rider).group(1)
    cmd=[engine,'--path',str(ROOT/'game')]
    if not a.visible:cmd.append('--headless')
    cmd+=['res://tests/npc_visual_observer.tscn','--quit-after','216000','--',f'--scene={a.scene}',f'--duration={a.duration}',f'--output={output.as_posix()}']
    if a.village is not None:
        if a.scene!='seed42':p.error('村ID仅用于seed42实际源聚落')
        row=next((line.split('\t') for line in (ROOT/'game/planet/seed42/groups/6068/sites.tsv').read_text(encoding='utf8').splitlines() if line.startswith('village\t'+str(a.village)+'\t')),None)
        if row is None:p.error('村ID不在当前6068源表')
        cmd += [f'--observer-x={row[3]}',f'--observer-z={row[4]}']
    start=time.monotonic();timed_out=False;code=None
    with (output/'engine.log').open('wb') as stream:
        process=subprocess.Popen(cmd,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,creationflags=0 if a.visible else subprocess.CREATE_NO_WINDOW)
        try:code=process.wait(timeout=a.duration+90)
        except subprocess.TimeoutExpired:
            timed_out=True;process.kill();code=124;process.wait(timeout=10)
    log=(output/'engine.log').read_text(encoding='utf8',errors='replace');report=json.loads((output/'report.json').read_text(encoding='utf8')) if (output/'report.json').exists() else {}
    checked=code or (2 if 'ERROR:' in log or not report.get('done') else 0)
    result=dict(command=cmd,engineExit=code,checkedExit=checked,timedOut=timed_out,wallSeconds=round(time.monotonic()-start,2),scene=a.scene,observerVillage=a.village,physicsNormalTime=True,scope='普通引擎时间；未使用fixed-fps/快进/重启时钟/天气覆盖或NPC状态写入',coverage=report.get('counts',{}))
    (output/'run.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n');print(json.dumps(result,ensure_ascii=False));print(log[-500:]);return checked
if __name__=='__main__':sys.exit(main())
