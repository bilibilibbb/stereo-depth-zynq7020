"""Guard-owned ARM worker; no EDA, network settings or board operations."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    if os.environ.get('SNAP_RELEASE_GUARDED')!='firmware':raise RuntimeError('Use run.py firmware')
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    if not out.is_relative_to(ROOT/'build'):raise RuntimeError('Output outside the package build directory')
    for name in ('build_runtime.py','build_firmware.py'):
        subprocess.run([sys.executable,'-X','utf8',str(ROOT/'scripts'/name)],cwd=ROOT,check=True,timeout=300)
    expected=json.loads((ROOT/'reference/build/accepted_firmware.json').read_text(encoding='utf-8'))
    elf=out/'firmware/snapshot11459_UNDEPLOYED.elf';actual=sha(elf)
    result={'status':'BUILT','elf_sha256':actual,'accepted_elf_sha256':expected['elf_sha256'],
            'byte_identical_to_accepted_elf':actual==expected['elf_sha256'],
            'scope':'Independent package ARM compile/link only; no new board or hardware timing evidence.'}
    (out/'comparison.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))
if __name__=='__main__':main()
