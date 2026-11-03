"""Source-bound, dynamically guarded reproduction commands for this package."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,os,sys
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts'))
from run_guarded import memory_snapshot,child_environment
from guard_queue import queued_worker
from windows_job import JobProcess
from memory_guard import admit,wait_guarded
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def verify():
    records=json.loads((ROOT/'source_manifest.json').read_text(encoding='utf-8'))['files']
    for item in records:
        path=(ROOT/item['path']).resolve(strict=True)
        if not path.is_relative_to(ROOT):raise RuntimeError('Source escapes package')
        if sha(path)!=item['sha256']:raise RuntimeError('Source changed: '+item['path'])
    return len(records)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=('verify','firmware','short-burst','desktop-check','desktop-build','bsp','platform','pair-replay','fsbl','hardware-plan','hardware-stage','boot-image','benchmark','lifecycle'))
    parser.add_argument('--platform',help='Explicit successful package platform_result.json for fsbl')
    parser.add_argument('--plan',help='Package-local hardware_plan.json')
    parser.add_argument('--stage',choices=('nn','sgm_volume','camera','compute','sgm','platform','preflight','place','route','bitstream'))
    parser.add_argument('--bitstream',help='Successful bitstream stage_result.json')
    parser.add_argument('--fsbl',help='Successful fsbl_result.json')
    parser.add_argument('--firmware',help='Successful firmware comparison.json')
    parser.add_argument('--inputs',help='Optional saved benchmark tensor file; omitted uses deterministic synthetic inputs')
    args=parser.parse_args()
    if args.action=='fsbl' and not args.platform:parser.error('fsbl requires --platform')
    if args.action=='hardware-stage' and not (args.plan and args.stage):parser.error('hardware-stage requires --plan and --stage')
    if args.action=='hardware-stage' and args.stage in ('platform','preflight','place') and not args.platform:parser.error('This stage requires --platform')
    if args.action=='boot-image' and not all((args.bitstream,args.fsbl,args.firmware)):parser.error('boot-image requires --bitstream, --fsbl and --firmware')
    count=verify()
    if args.action=='verify':print(json.dumps({'status':'SOURCE_VERIFIED','files':count}));return 0
    if os.name!='nt':raise RuntimeError('This qualified guarded build requires Windows')
    policy=json.loads((ROOT/'config/memory_policy.json').read_text(encoding='utf-8'))
    out=ROOT/'build'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ');out.mkdir(parents=True)
    result={'status':'ADMISSION','action':args.action,'source_files':count,'new_board_operations':0}
    code=1
    try:
        with queued_worker():
            verify()
            result['before']=memory_snapshot();limits=admit(policy,result['before'])
            env=child_environment();env['SNAP_BUILD_DIR']=str(out);env['SNAP_RELEASE_GUARDED']=args.action
            if args.platform:env['SNAP_RELEASE_PLATFORM']=str((ROOT/args.platform).resolve(strict=True))
            for name in ('plan','stage','bitstream','fsbl','firmware'):
                if getattr(args,name):env['SNAP_RELEASE_'+name.upper()]=getattr(args,name)
            if args.inputs:env['SNAP_BENCH_INPUTS']=str(Path(args.inputs).resolve(strict=True))
            workers={'firmware':'build_candidate.py','bsp':'build_bsp.py','platform':'generate_platform.py','pair-replay':'replay_pair.py','fsbl':'build_fsbl.py','hardware-plan':'hardware_plan.py','hardware-stage':'rebuild_hardware.py','boot-image':'build_boot_image.py','benchmark':'benchmark_graph.py','lifecycle':'check_lifecycle.py'}
            with (out/'console.log').open('wb') as log:
                with JobProcess([sys.executable,'-X','utf8',str(ROOT/'scripts'/workers.get(args.action,'release_checks.py'))],cwd=ROOT,env=env,stdout=log,
                                process_bytes=limits['process_bytes'],job_bytes=limits['tree_bytes']) as child:
                    result['status']='RUNNING';code=wait_guarded(child,result,timeout_seconds=1800 if args.action=='hardware-stage' and args.stage=='route' else 600);result['job']=child.stats()
            result.update(status='PASS' if code==0 else 'FAILED',exit_code=code,after=memory_snapshot())
    except Exception as exc:result.update(status='ERROR',error=str(exc))
    (out/'guard.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'],'output':str(out),'exit_code':code}));return code
if __name__=='__main__':raise SystemExit(main())
