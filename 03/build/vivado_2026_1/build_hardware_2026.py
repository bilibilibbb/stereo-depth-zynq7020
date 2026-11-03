"""Sequential fresh hardware rebuild; each EDA stage uses run_2026's guarded Job."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parent
STAGES=('nn_core','nn_loader','nn_writer','nn','sgm_volume','camera','compute','sgm','platform','preflight','place','route','bitstream')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vivado',type=Path,required=True)
    p.add_argument('--platform',type=Path,required=True)
    p.add_argument('--plan',type=Path,required=True)
    p.add_argument('--start',choices=STAGES,default='nn_core')
    args=p.parse_args()
    plan=(ROOT/args.plan).resolve(strict=True)
    assert plan.is_relative_to(ROOT/'build2026')
    out=ROOT/'build2026'/('hardware_sequence_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    out.mkdir()
    r={'status':'RUNNING','plan':plan.relative_to(ROOT).as_posix(),'platform':str(args.platform),'steps':[],'board_operations':0}
    def save():(out/'sequence.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        for stage in STAGES[STAGES.index(args.start):]:
            print('START '+stage,flush=True)
            command=[sys.executable,'-X','utf8',str(ROOT/'run_2026.py'),'hardware-stage',
                     '--stage',stage,'--plan',str(plan),'--vivado',str(args.vivado)]
            if stage in ('platform','preflight','place'):command+=['--platform',str(args.platform)]
            log=out/(stage+'.log')
            with log.open('wb') as f:
                done=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
            item={'stage':stage,'exit_code':done.returncode,'log':log.relative_to(ROOT).as_posix()}
            r['steps'].append(item);save()
            if done.returncode:raise RuntimeError('Stage '+stage+' stopped; inspect '+str(log))
            result=json.loads(log.read_text(encoding='utf-8').strip().splitlines()[-1])
            assert result['status']=='PASS'
            item['guard_output']=result['output'];save()
            print('PASS '+stage,flush=True)
        r['status']='HARDWARE_BITSTREAM_BUILT_NOT_DEPLOYED'
    except BaseException as exc:
        r.update(status='STOPPED_WITH_EVIDENCE',error=repr(exc));raise
    finally:save()
    print(json.dumps({'status':r['status'],'receipt':str(out/'sequence.json')}))
if __name__=='__main__':main()
