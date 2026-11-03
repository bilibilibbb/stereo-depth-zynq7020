"""Package-local reproduction checks. Board and network configuration stay untouched."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def hardware_sources():
    r=json.loads((ROOT/'hardware/contract.json').read_text(encoding='utf-8'))
    for name,digest in r['sources'].items():
        p=(ROOT/name).resolve(strict=True)
        if not p.is_relative_to(ROOT) or sha(p)!=digest:raise RuntimeError('Hardware source mismatch: '+name)
    if sha(ROOT/'hardware/generated_contract.tcl')!=r['generated_tcl_sha256']:raise RuntimeError('Generated contract mismatch')
    return r
def prepare_desktop(out):
    here=ROOT/'desktop_app';stage=here/'stage'
    native=stage/'native';assets=stage/'assets'
    native.mkdir(parents=True,exist_ok=True);assets.mkdir(exist_ok=True)
    compiler=Path(os.environ['WINDIR'])/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    subprocess.run([str(compiler),'/nologo','/target:exe','/platform:anycpu','/optimize+',
        '/reference:System.Management.dll','/reference:System.Web.Extensions.dll','/reference:System.Windows.Forms.dll',
        '/out:'+str(native/'NetworkSetup.exe'),str(here/'NetworkSetup.cs')],check=True,timeout=60)
    html=(ROOT/'common/viewer.html').read_text(encoding='utf-8')
    html=html.replace('</head>','<style>#preview{transform:rotate(180deg)}</style></head>')
    (assets/'viewer.html').write_text(html,encoding='utf-8');shutil.copyfile(ROOT/'common/viewer.js',assets/'viewer.js')
    return here,stage,native,assets
def main():
    action=os.environ.get('SNAP_RELEASE_GUARDED')
    if action not in ('short-burst','desktop-check','desktop-build'):raise RuntimeError('Use run.py')
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    if not out.is_relative_to(ROOT/'build'):raise RuntimeError('Unexpected output directory')
    if action=='short-burst':
        hardware_sources()
        from check_short_burst import check
        check(out/'short_burst')
        return
    here,stage,native,assets=prepare_desktop(out)
    sys.path[:0]=[str(here),str(ROOT/'common')]
    if action=='desktop-check':
        from self_test import run
        result=run()
        if result['status']!='PASS' or result['new_board_operations'] or result['network_changes']:raise RuntimeError('Desktop offline check failed')
        (out/'desktop_check.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(result));return
    import PyInstaller.__main__
    PyInstaller.__main__.run([str(here/'app.py'),'--noconfirm','--onedir','--windowed','--noupx',
        '--name','stereo_viewer','--paths',str(ROOT/'common'),'--paths',str(here),
        '--add-data',str(assets)+os.pathsep+'assets','--add-binary',str(native/'NetworkSetup.exe')+os.pathsep+'native',
        '--hidden-import','self_test','--hidden-import','app','--exclude-module','numpy','--exclude-module','matplotlib',
        '--distpath',str(out/'dist'),'--workpath',str(out/'pyinstaller_work'),'--specpath',str(out)])
    exe=out/'dist/stereo_viewer/stereo_viewer.exe'
    # Exercise the frozen binary from an unrelated working directory without
    # Python environment paths. This does not change any adapter settings.
    env=os.environ.copy();env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
    subprocess.run([str(exe),'--self-test',str(out/'frozen_self_test.json')],cwd=out,env=env,check=True,timeout=45)
    r=json.loads((out/'frozen_self_test.json').read_text(encoding='utf-8'))
    if r['status']!='PASS' or r['network_changes'] or r['new_board_operations']:raise RuntimeError('Frozen check failed')
    import importlib.metadata as metadata
    result={'status':'PASS','scope':'Source rebuild and frozen loopback self-test; no new-computer or privileged network validation.',
        'python':sys.version,'pillow':metadata.version('pillow'),'pyinstaller':metadata.version('pyinstaller'),
        'exe_sha256':sha(exe),'app_sources':{p.relative_to(ROOT).as_posix():sha(p) for p in [here/'app.py',here/'network.py',here/'render.py',here/'self_test.py',here/'NetworkSetup.cs']},
        'network_changes':0,'new_board_operations':0}
    (out/'desktop_build.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
