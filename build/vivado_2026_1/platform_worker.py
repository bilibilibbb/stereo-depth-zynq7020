"""Run the actual modern platform generator inside the parent's guarded Job."""
from pathlib import Path
from datetime import datetime,timezone
import os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from ascii_project_path import temporary_ascii_drive
from port_support import read,write,sha,verify_sources,require_version,engine_for,inspect_xsa

def main():
    action=os.environ.get('SNAP2026_GUARDED')
    if action not in ('doctor','platform'):raise RuntimeError('Use run_2026.py')
    out=Path(os.environ['SNAP2026_BUILD_DIR']).resolve(strict=True)
    if not out.is_relative_to(ROOT/'build2026'):raise RuntimeError('Unexpected build directory')
    verify_sources(ROOT)
    launcher=Path(os.environ['VIVADO_2026_PATH']).resolve(strict=True)
    engine=engine_for(launcher)
    identity={'launcher_path':str(launcher),'launcher_sha256':sha(launcher),
              'engine_path':str(engine),'engine_sha256':sha(engine)}
    result={'status':'RUNNING','action':action,'checked_utc':datetime.now(timezone.utc).isoformat(),
        'tool':identity,'source_manifest_sha256':sha(ROOT/'modern/source_manifest.json'),
        'baseline_manifest_sha256':sha(ROOT/'source_manifest.json'),
        'hardware_contract_sha256':sha(ROOT/'hardware/contract.json'),
        'synthesis_run':False,'physical_pass':False,'full_build_verified':False,'board_operations':0}
    try:
        with temporary_ascii_drive(ROOT,out/'path_alias.json') as alias:
            work=alias/out.relative_to(ROOT)
            with (work/'version.log').open('wb') as log:
                probe=subprocess.run([str(launcher),'-version'],cwd=work,stdin=subprocess.DEVNULL,
                    stdout=log,stderr=subprocess.STDOUT,timeout=90)
            # The Windows version-only launcher may return a nonzero code after
            # printing its complete banner. Preserve it; require a real Tcl
            # session with exit 0 below instead of accepting the banner alone.
            identity['version_probe_exit_code']=probe.returncode
            identity['version']=require_version((out/'version.log').read_text(errors='replace'))
            write(out/'tool_identity.json',identity)
            if action=='doctor':
                with (work/'doctor.stdout.log').open('wb') as log:
                    done=subprocess.run([str(launcher),'-mode','batch','-nojournal',
                        '-source',str(alias/'modern/doctor_2026.tcl'),'-log','doctor.log'],
                        cwd=work,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=180)
                doctor_log=(out/'doctor.stdout.log').read_text(errors='replace')
                if done.returncode or 'ERROR:' in doctor_log or 'SNAPSHOT_2026_DOCTOR_PASS' not in doctor_log:
                    raise RuntimeError('Actual Vivado Tcl/device/IP check failed; see doctor.stdout.log')
                result['doctor_exit_code']=done.returncode
                result['inventory_sha256']=sha(out/'installed_inventory.tsv')
            if action=='platform':
                env=os.environ.copy();env['SNAPSHOT_2026_PLATFORM_ADMITTED']='1'
                with (work/'vivado.stdout.log').open('wb') as log:
                    done=subprocess.run([str(launcher),'-mode','batch','-nojournal',
                        '-source',str(alias/'modern/platform_2026.tcl'),'-log','vivado.log'],
                        cwd=work,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=480)
                log=(out/'vivado.stdout.log').read_text(errors='replace')
                if done.returncode or 'ERROR:' in log or 'CRITICAL WARNING:' in log:
                    raise RuntimeError('Review preserved Vivado platform log before proceeding')
                if 'SNAPSHOT_2026_PLATFORM_XSA_GENERATED' not in log:raise RuntimeError('Platform completion marker missing')
                result['xsa']=inspect_xsa(out/'snapshot.xsa')
                required=['snapshot.xsa','snapshot_bd_export.tcl','platform_ps.tsv','address_map.tsv',
                          'clock_reset_map.tsv','resolved_ips.tsv','tool_version.txt',
                          'synthesis_compile_order.tsv','simulation_compile_order.tsv']
                generated=[out/n for n in required]
                generated+=list(out.rglob('snapshot_bd_wrapper.v'))
                if len(generated)<10:raise RuntimeError('Platform HDL wrapper not generated')
                for p in generated:
                    if not p.is_file() or p.stat().st_size==0:raise RuntimeError('Missing platform artifact: '+str(p))
                result['generated']={p.relative_to(out).as_posix():sha(p) for p in generated}
                bd=out/'project/snapshot.gen/sources_1/bd/snapshot_bd'
                if not (bd/'synth/snapshot_bd.v').is_file():raise RuntimeError('Missing actual generated BD RTL')
                result['generated_bd_directory']=bd.relative_to(out).as_posix()
                result['generated_ip_sources']={p.relative_to(out).as_posix():sha(p) for p in bd.rglob('*')
                    if p.is_file() and p.suffix.lower() in ('.v','.sv','.vhd','.vh','.xdc','.xci','.tcl','.mem','.coe')}
        if sha(launcher)!=identity['launcher_sha256'] or sha(engine)!=identity['engine_sha256']:
            raise RuntimeError('Installed tool changed during this operation')
        result['status']='VIVADO_2026_1_IDENTIFIED' if action=='doctor' else 'PLATFORM_XSA_GENERATED_NOT_SYNTHESIZED'
    except BaseException as exc:
        result.update(status='FAILED',error=repr(exc));raise
    finally:
        write(out/('environment_result.json' if action=='doctor' else 'platform_result.json'),result)
    print(__import__('json').dumps(result))

if __name__=='__main__':main()
