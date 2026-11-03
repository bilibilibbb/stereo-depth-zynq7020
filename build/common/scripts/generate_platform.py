"""Generate unchanged BD/IP under run.py; never synthesize or access a board."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, subprocess
from ascii_project_path import temporary_ascii_drive
from check_short_burst import check as check_bursts

ROOT=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='platform','Use run.py platform'
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    assert out.is_relative_to(ROOT/'build')
    admission=read(ROOT/'reference/build/platform_admission.json')
    assert admission['allowed_action']=='platform_generation_only'
    assert sha(ROOT/'hardware/contract.json')==admission['hardware_contract_sha256']
    weights=admission['weights'];assert sha(ROOT/weights['path'])==weights['sha256']
    contract=read(ROOT/'hardware/contract.json')
    for rel,digest in contract['sources'].items():assert sha(ROOT/rel)==digest,rel
    assert sha(ROOT/'hardware/generated_contract.tcl')==contract['generated_tcl_sha256']
    spec=read(ROOT/'config/vivado.json')
    launcher=Path(os.environ[spec['environment']]).resolve(strict=True)
    assert sha(launcher)==spec['launcher_sha256']
    assert sha(launcher.parent/spec['engine_relative'])==spec['engine_sha256']
    check_bursts(out/'short_burst')
    assert read(out/'short_burst/result.json')['status']=='PASS'
    result={'status':'RUNNING','started_utc':datetime.now(timezone.utc).isoformat(),
            'hardware_contract_sha256':sha(ROOT/'hardware/contract.json'),'new_board_operations':0,
            'synthesis_run':False,'physical_pass':False,'admission_sha256':sha(ROOT/'reference/build/platform_admission.json')}
    try:
        with temporary_ascii_drive(ROOT,out/'path_alias.json') as alias:
            work=alias/out.relative_to(ROOT)
            env=os.environ.copy();env['SNAPSHOT_PLATFORM_ADMITTED']='1'
            (work/'vendor.stdin').write_bytes(b'')
            cmd=[str(launcher),'-mode','batch','-source',str(alias/'hardware/platform.tcl'),'-nojournal','-log','vivado.log']
            with (work/'vendor.stdin').open('rb') as inp,(work/'vendor.stdout.log').open('wb') as stdout,(work/'vendor.stderr.log').open('wb') as stderr:
                done=subprocess.run(cmd,cwd=work,env=env,stdin=inp,stdout=stdout,stderr=stderr,timeout=540)
            assert done.returncode==0,'Vendor generation failed'
        logs='\n'.join((out/n).read_text(errors='replace') for n in ('vendor.stdout.log','vendor.stderr.log'))
        assert 'SNAPSHOT_PLATFORM_GENERATED_NOT_SYNTHESIZED' in logs
        required=['snapshot.hwdef','snapshot_bd_export.tcl','platform_ps.tsv','address_map.tsv','clock_reset_map.tsv']
        generated=[out/n for n in required]
        generated+=list(out.rglob('ps7_init.c'))+list(out.rglob('ps7_init.tcl'))+list(out.rglob('snapshot_bd_wrapper.v'))
        assert len(generated)>=8
        result.update(status='PASS',generated={p.relative_to(out).as_posix():sha(p) for p in generated})
    finally:
        if result['status']=='RUNNING':result['status']='FAILED'
        (out/'platform_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))
if __name__=='__main__':main()
