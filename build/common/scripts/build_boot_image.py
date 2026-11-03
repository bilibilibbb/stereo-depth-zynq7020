"""Pack fresh, bound FSBL/bitstream/application into an undeployed BOOT.BIN."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
from ascii_project_path import temporary_ascii_drive
from rebuild_hardware import local,read,sha,check_files
ROOT=Path(__file__).resolve().parents[1]

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='boot-image'
    out=local(os.environ['SNAP_BUILD_DIR'])
    paths={name:local(os.environ['SNAP_RELEASE_'+name.upper()]) for name in ('bitstream','fsbl','firmware')}
    bit,fsbl,fw=[read(paths[n]) for n in ('bitstream','fsbl','firmware')]
    assert bit['status']=='PASS' and bit['stage']=='bitstream' and read(local(bit['guard']))['status']=='PASS'
    check_files(paths['bitstream'].parent,bit['artifacts'])
    assert fsbl['status']=='PASS' and read(paths['fsbl'].parent/'guard.json')['status']=='PASS'
    assert fw['status']=='BUILT' and read(paths['firmware'].parent/'guard.json')['status']=='PASS'
    assert fsbl['hardware_contract_sha256']==bit['hardware_contract_sha256']==sha(ROOT/'hardware/contract.json')
    source_files={'fsbl.elf':local(fsbl['elf']),'engineering.bit':paths['bitstream'].parent/'engineering.bit',
                  'application.elf':paths['firmware'].parent/'firmware/snapshot11459_UNDEPLOYED.elf'}
    assert sha(source_files['fsbl.elf'])==fsbl['elf_sha256']
    assert sha(source_files['application.elf'])==fw['elf_sha256']
    spec=read(ROOT/'config/bootgen.json');exe=Path(os.environ[spec['environment']]).resolve(strict=True)
    assert sha(exe)==spec['launcher_sha256'],'Use documented SDK bootgen launcher'
    assert sha(exe.parent/spec['engine_relative'])==spec['engine_sha256'],'SDK bootgen engine changed'
    for name,p in source_files.items():shutil.copyfile(p,out/name)
    (out/'boot.bif').write_text('the_ROM_image:\n{\n  [bootloader] fsbl.elf\n  engineering.bit\n  application.elf\n}\n',encoding='ascii')
    with temporary_ascii_drive(ROOT,out/'path_alias.json') as alias:
        with (out/'bootgen.log').open('wb') as log:
            subprocess.run([str(exe),'-arch','zynq','-image','boot.bif','-o','BOOT.BIN','-w','on'],
                cwd=alias/out.relative_to(ROOT),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    image=out/'BOOT.BIN';assert image.stat().st_size>1024
    result={'status':'PASS_UNDEPLOYED','hardware_contract_sha256':sha(ROOT/'hardware/contract.json'),
            'inputs':{n:{'receipt':p.relative_to(ROOT).as_posix(),'sha256':sha(p)} for n,p in paths.items()},
            'artifacts':{n:sha(out/n) for n in ('fsbl.elf','engineering.bit','application.elf','boot.bif','BOOT.BIN')},
            'boot_image_bytes':image.stat().st_size,'new_board_operations':0,
            'scope':'Locally rebuilt boot image. No SD write, target reset or board acceptance is performed by this command.'}
    (out/'boot_image_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))

if __name__=='__main__':main()
