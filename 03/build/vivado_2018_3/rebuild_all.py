"""Sequential source-to-BOOT.BIN rehearsal; each step owns its guarded job.

No board connection, download, SD access or network changes are performed.
Failure stops the sequence and preserves every completed/failed stage.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parent

def main():
    required=('ARM_PATH','MAKE_PATH','HOST_PATH','VIVADO_PATH','HSI_PATH','IVERILOG_PATH','VVP_PATH','BOOTGEN_PATH')
    for name in required:
        if not os.environ.get(name) or not Path(os.environ[name]).is_file():raise RuntimeError('Configure '+name+' before starting')
    out=ROOT/'build'/('rehearsal_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    out.mkdir(parents=True);receipt={'status':'RUNNING','steps':[],'board_operations':0}
    def save():(out/'rebuild_result.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    def run(action,*args):
        print('Starting '+action+' '+' '.join(args),flush=True)
        command=[sys.executable,'-X','utf8',str(ROOT/'run.py'),action,*args]
        p=subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8')
        log=out/(str(len(receipt['steps']))+'_'+action+'.log');log.write_text(p.stdout,encoding='utf-8')
        item={'command':command[4:],'exit_code':p.returncode,'log':log.relative_to(ROOT).as_posix()}
        receipt['steps'].append(item);save()
        if p.returncode:raise RuntimeError('Stage failed: '+str(log))
        result=json.loads(p.stdout.strip().splitlines()[-1]);assert result['status']=='PASS',result
        item['output']=Path(result['output']).relative_to(ROOT).as_posix();save()
        print('Passed '+action+' '+item['output'],flush=True)
        return item['output']
    try:
        run('lifecycle');run('benchmark');run('bsp')
        fw=run('firmware')+'/comparison.json'
        platform=run('platform')+'/platform_result.json'
        fsbl=run('fsbl','--platform',platform)+'/fsbl_result.json'
        plan=run('hardware-plan')+'/hardware_plan.json'
        for stage in ('nn','sgm_volume','camera','compute','sgm','platform','preflight','place','route','bitstream'):
            args=['--plan',plan,'--stage',stage]
            if stage in ('platform','preflight','place'):args+=['--platform',platform]
            run('hardware-stage',*args)
        bit=str(Path(plan).parent/'stages/bitstream/stage_result.json')
        boot=run('boot-image','--bitstream',bit,'--fsbl',fsbl,'--firmware',fw)
        receipt.update(status='BOOT_BUILT_NOT_DEPLOYED',boot_receipt=boot+'/boot_image_result.json')
    except BaseException as e:
        receipt.update(status='FAILED_PRESERVED',error=repr(e));raise
    finally:save()
    print(json.dumps({'status':receipt['status'],'receipt':str(out/'rebuild_result.json')}))

if __name__=='__main__':main()
