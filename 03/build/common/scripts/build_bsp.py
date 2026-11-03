"""Rebuild libxil from the generated BSP with unmodified vendor recipes."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
from release_environment import tool
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    if os.environ.get('SNAP_RELEASE_GUARDED')!='bsp':raise RuntimeError('Use run.py bsp')
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    if not out.is_relative_to(ROOT/'build'):raise RuntimeError('Unexpected output directory')
    src=ROOT/'reference/platform_bsp_source';manifest=json.loads((src/'source_manifest.json').read_text())
    for item in manifest['files']:
        p=(src/item['path']).resolve(strict=True)
        if not p.is_relative_to(src) or sha(p)!=item['sha256']:raise RuntimeError('BSP source mismatch')
    gcc=tool('arm');make=tool('make');work=out/'bsp'
    shutil.copytree(src,work)
    processor=work/'ps7_cortexa9_0';(processor/'lib').mkdir(exist_ok=True)
    env=os.environ.copy();env['PATH']=os.pathsep.join((str(gcc.parent),str(make.parent),env['PATH']))
    directories=[]
    for m in sorted((processor/'libsrc').glob('*/src/Makefile')):
        text=m.read_text(encoding='utf-8')
        if 'LIB=libxil.a' in text.replace(' ',''):
            if not m.resolve().is_relative_to(work):raise RuntimeError('Recipe escapes output')
            directories.append(m.parent)
    if not directories:raise RuntimeError('No actual libxil recipes found')
    flags=['SHELL=cmd','COMPILER=arm-none-eabi-gcc','ARCHIVER=arm-none-eabi-ar','COMPILER_FLAGS=-O2 -c',
           'EXTRA_COMPILER_FLAGS=-mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -nostartfiles -g -Wall -Wextra']
    with (out/'bsp_make.log').open('wb') as log:
        for action in ('include','libs'):
            for d in directories:
                # No clean target is called. Some vendor libs recipes remove
                # their freshly generated .o files only, within this new tree.
                subprocess.run([str(make),'-j1','-s','-C',str(d),action,*flags],env=env,cwd=work,
                               stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    library=processor/'lib/libxil.a'
    members=subprocess.check_output([str(gcc.with_name('arm-none-eabi-ar.exe')),'t',str(library)],env=env,text=True).splitlines()
    expected=subprocess.check_output([str(gcc.with_name('arm-none-eabi-ar.exe')),'t',str(ROOT/'reference/platform_sdk/lib/libxil.a')],env=env,text=True).splitlines()
    if sorted(members)!=sorted(expected):raise RuntimeError('BSP archive membership changed')
    result={'status':'PASS','scope':'Generated BSP libxil source rebuild only; not a firmware or new board validation.',
        'modules':[d.parent.name for d in directories],'archive_members':len(members),'same_member_names':True,
        'libxil_sha256':sha(library),'accepted_libxil_sha256':sha(ROOT/'reference/platform_sdk/lib/libxil.a'),
        'new_board_operations':0}
    (out/'bsp_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
