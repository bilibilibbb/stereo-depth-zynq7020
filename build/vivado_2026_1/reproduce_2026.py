"""Rebuild with Vivado 2026.1 and the explicitly retained, qualified ARM software.

The 2018.3 reproduce.py is unchanged. Every EDA operation below runs through
run_2026.py's dynamic Job protection; this entry never accesses a board.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, json, subprocess, sys

ROOT=Path(__file__).resolve().parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('verify','rebuild'))
    parser.add_argument('--vivado',type=Path,help='Vivado 2026.1 bin/vivado.bat')
    args=parser.parse_args()
    if args.action=='verify':
        return subprocess.call([sys.executable,'-X','utf8',str(ROOT/'run_2026.py'),'verify'],cwd=ROOT)
    if not args.vivado:parser.error('rebuild requires --vivado')
    out=ROOT/'build2026'/('reproduction_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    out.mkdir(parents=True)
    state={'status':'RUNNING','hardware_tool':'Vivado 2026.1',
           'arm_software':'Qualified SDK 2018.3 GCC 7.3.1 artifacts; sources and old rebuild entry included',
           'steps':[],'board_operations':0,'full_build_verified':False}
    def save():
        (out/'result.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def invoke(label,arguments):
        print('START '+label,flush=True)
        log=out/(label+'.log')
        with log.open('wb') as stream:
            result=subprocess.run([sys.executable,'-X','utf8']+list(map(str,arguments)),
                                  cwd=ROOT,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT)
        step={'name':label,'exit_code':result.returncode,'log':log.relative_to(ROOT).as_posix()}
        state['steps'].append(step);save()
        if result.returncode:raise RuntimeError(label+' failed; see '+str(log))
        receipt=json.loads(log.read_text(encoding='utf-8').strip().splitlines()[-1])
        step['result']=receipt;save()
        print('PASS '+label,flush=True)
        return receipt
    def guarded(action,*more):
        value=invoke(action,[ROOT/'run_2026.py',action,'--vivado',args.vivado,*more])
        if value['status']!='PASS':raise RuntimeError('Guard did not pass: '+action)
        path=Path(value['output']).resolve(strict=True)
        if not path.is_relative_to(ROOT/'build2026'):raise RuntimeError('Unexpected stage output')
        return path
    save()
    try:
        guarded('doctor')
        platform=guarded('platform')/'platform_result.json'
        plan=guarded('hardware-plan','--platform',platform)/'hardware_plan.json'
        hardware=invoke('hardware',[ROOT/'build_hardware_2026.py','--vivado',args.vivado,
                                   '--platform',platform,'--plan',plan])
        if hardware['status']!='HARDWARE_BITSTREAM_BUILT_NOT_DEPLOYED':
            raise RuntimeError('Complete fresh hardware build missing')
        boot=guarded('boot-image','--plan',plan)
        result=json.loads((boot/'boot_image_result.json').read_text(encoding='utf-8'))
        if result['status']!='BOOT_2026_HARDWARE_WITH_ACCEPTED_ARM_NOT_DEPLOYED':
            raise RuntimeError('Source-bound boot image missing')
        state.update(status='SOURCE_TO_BOOT_BUILT_NOT_DEPLOYED',full_build_verified=True,
                     boot_output=boot.relative_to(ROOT).as_posix())
    except BaseException as exc:
        state.update(status='STOPPED_WITH_EVIDENCE',error=repr(exc));raise
    finally:save()
    print(json.dumps({'status':state['status'],'receipt':str(out/'result.json'),
                      'boot_output':state['boot_output'],'board_operations':0}))
    return 0

if __name__=='__main__':raise SystemExit(main())
