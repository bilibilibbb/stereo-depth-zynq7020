"""Isolated Vivado 2026.1 migration commands; retain the original guarded flow."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, json, os, sys

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'modern'))
from run_guarded import memory_snapshot,child_environment
from guard_queue import queued_worker
from windows_job import JobProcess
from memory_guard import admit,wait_guarded
from port_support import verify_sources,find_vivado,read,write

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('verify','doctor','platform','hardware-plan','hardware-stage','boot-image'))
    parser.add_argument('--vivado',type=Path,help='Vivado 2026.1 bin/vivado.bat; alternatively set VIVADO_2026_PATH')
    parser.add_argument('--platform',type=Path,help='Fresh 2026.1 platform_result.json')
    parser.add_argument('--plan',type=Path,help='Fresh 2026.1 hardware_plan.json')
    parser.add_argument('--reuse-plan',type=Path,help='Reuse matching successful computational stages from an earlier 2026.1 plan')
    parser.add_argument('--route-seed',type=Path,help='Completed but timing-unmet route receipt for source-bound post-route optimization')
    parser.add_argument('--stage',choices=('nn_core','nn_loader','nn_writer','nn','sgm_volume','camera','compute','sgm','platform','preflight','place','route','bitstream'))
    args=parser.parse_args()
    if args.route_seed and not (args.action=='hardware-plan' and args.reuse_plan):
        parser.error('--route-seed requires hardware-plan with --reuse-plan')
    if args.action=='hardware-plan' and not args.platform:parser.error('hardware-plan requires --platform')
    if args.action=='boot-image' and not args.plan:parser.error('boot-image requires --plan')
    if args.action=='hardware-stage' and not (args.plan and args.stage):parser.error('hardware-stage requires --plan and --stage')
    if args.action=='hardware-stage' and args.stage in ('platform','preflight','place') and not args.platform:
        parser.error('This stage requires --platform')
    count=verify_sources(ROOT)
    if args.action=='verify':
        print(json.dumps({'status':'PORT_SOURCES_VERIFIED','files':count,'vivado_2026_executed':False}))
        return 0
    launcher=find_vivado(args.vivado)
    if launcher is None:
        print(json.dumps({'status':'VIVADO_2026_1_NOT_INSTALLED','action':args.action,
            'required':'Provide --vivado <Vivado 2026.1>/bin/vivado.bat',
            'eda_started':False,'board_operations':0}))
        return 2
    if os.name!='nt':raise RuntimeError('This port retains the qualified Windows process-tree guard.')
    out=ROOT/'build2026'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out.mkdir(parents=True)
    record={'status':'ADMISSION','action':args.action,'target_version':'2026.1',
            'source_files':count,'board_operations':0,'full_build_verified':False}
    code=1
    try:
        with queued_worker():
            verify_sources(ROOT)
            record['before']=memory_snapshot()
            limits=admit(read(ROOT/'config/memory_policy.json'),record['before'])
            env=child_environment()
            env.update(SNAP2026_GUARDED=args.action,SNAP2026_BUILD_DIR=str(out),VIVADO_2026_PATH=str(launcher))
            env.update(SNAP_RELEASE_GUARDED=args.action,SNAP_BUILD_DIR=str(out))
            for name in ('platform','plan','stage','reuse_plan','route_seed'):
                value=getattr(args,name)
                if value:
                    if name!='stage':
                        value=(ROOT/value).resolve(strict=True)
                        if not value.is_relative_to(ROOT/'build2026'):raise ValueError('Use this port\'s fresh artifacts')
                    env['SNAP_RELEASE_'+name.upper()]=str(value)
            worker={'hardware-plan':'hardware_plan.py','hardware-stage':'rebuild_hardware.py','boot-image':'boot_worker.py'}.get(args.action,'platform_worker.py')
            with (out/'console.log').open('wb') as log:
                with JobProcess([sys.executable,'-X','utf8',str(ROOT/'modern'/worker)],
                    cwd=ROOT,env=env,stdout=log,process_bytes=limits['process_bytes'],
                    job_bytes=limits['tree_bytes']) as child:
                    code=wait_guarded(child,record,timeout_seconds=1800 if args.stage=='route' else 600)
                    record['job']=child.stats()
            record.update(status='PASS' if code==0 else 'FAILED',exit_code=code,after=memory_snapshot())
    except Exception as exc:
        record.update(status='ERROR',error=str(exc))
    write(out/'guard.json',record)
    print(json.dumps({'status':record['status'],'action':args.action,'output':str(out),'exit_code':code}))
    return code

if __name__=='__main__':raise SystemExit(main())
