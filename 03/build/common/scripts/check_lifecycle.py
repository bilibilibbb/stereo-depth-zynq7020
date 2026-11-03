"""Re-run original exporter and three-slot ownership tests on production C."""
from pathlib import Path
import hashlib,json,os,subprocess
from release_environment import tool
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='lifecycle'
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True);gcc=tool('host')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-fno-fast-math','-Ifirmware']
    sources=['firmware/pair_export.c','firmware/latest_pair.c','tests/export_fixture.c','tests/latest_checks.c']
    commands=[[str(gcc),*flags,sources[0],sources[2],'-o',str(out/'export_fixture.exe')],
              [str(gcc),*flags,*sources[:2],sources[3],'-o',str(out/'latest_checks.exe')],
              [str(out/'export_fixture.exe'),str(out/'synthetic_pair.sdp')],
              [str(out/'latest_checks.exe'),str(out/'synthetic_pair.sdp'),str(out/'synthetic_frozen_pair.sdp')]]
    with (out/'lifecycle.log').open('wb') as log:
        for cmd in commands:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=50)
    result={'status':'PASS','sources':{n:sha(ROOT/n) for n in sources},'commands':commands,
            'scope':'Original production exporter/three-slot state machine; synthetic fault injection, no board timing or full-model claim.',
            'new_board_operations':0,'synthetic':True}
    (out/'lifecycle_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
