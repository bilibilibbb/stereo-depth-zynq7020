"""Pack fresh 2026.1 hardware with the explicitly retained, accepted ARM software."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(1,str(ROOT/'scripts'))
import os,re,shutil,struct,subprocess,zipfile
from port_support import read,write,sha,verify_sources
from rebuild_hardware import local,predecessor,platform_result,check_files
from ascii_project_path import temporary_ascii_drive

def init_tables(text):
    result={}
    for name,body in re.findall(r'(ps7_\w+_\d_\d)\s*\[\]\s*=\s*\{(.*?)\};',text,re.S):
        assert name not in result
        body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
        # Preserve every operand and punctuation, including decimal delays.
        # Only comments and whitespace can differ between generated tables.
        result[name]=re.sub(r'\s+','',body)
    assert len(result)==21,'All PS initialization tables must be accounted for'
    return result

def main():
    assert os.environ['SNAP_RELEASE_GUARDED']=='boot-image'
    verify_sources(ROOT)
    out=local(os.environ['SNAP_BUILD_DIR'])
    planpath=local(os.environ['SNAP_RELEASE_PLAN']);plan=read(planpath)
    bitpath,bit=predecessor(planpath.parent,'bitstream')
    routepath,route=predecessor(planpath.parent,'route')
    assert route['internal_timing_pass'] and route['fully_routed'] and not route['routing_errors']
    assert bit['dependencies']['route']['sha256']==sha(routepath/'stage_result.json')
    platformpath,platform,bd=platform_result(plan['platform_receipt'])
    assert sha(platformpath)==plan['platform_receipt_sha256']
    software=ROOT/'modern/software_baseline';identity=read(software/'software_identity.json')
    check_files(software,identity['files'])
    assert identity['hardware_contract_sha256']==sha(ROOT/'hardware/contract.json')
    with zipfile.ZipFile(platformpath.parent/'snapshot.xsa') as z:
        new_init=z.read('ps7_init.c').decode('utf-8')
    old_init=(software/'ps7_init.c').read_text(encoding='utf-8')
    old_tables,new_tables=init_tables(old_init),init_tables(new_init)
    assert old_tables==new_tables,'New PS initialization differs; retained FSBL cannot be used'
    write(out/'ps_init_equivalence.json',dict(equal_tables=21,
        source_xsa_sha256=sha(platformpath.parent/'snapshot.xsa'),
        retained_ps7_init_sha256=sha(software/'ps7_init.c'),
        scope='Exact initialization opcodes/register values/delays for all three silicon revisions; not a board result.'))
    launcher=Path(os.environ['VIVADO_2026_PATH']).resolve(strict=True).with_name('bootgen.bat')
    engine=launcher.parent/'unwrapped/win64.o/bootgen.exe'
    tool={'launcher_sha256':sha(launcher),'engine_sha256':sha(engine),'path':str(launcher)}
    for name in ('application.elf','fsbl.elf','R14CAL.BIN','SNAPCFG.BIN'):shutil.copyfile(software/name,out/name)
    shutil.copyfile(bitpath/'engineering.bit',out/'engineering.bit')
    (out/'boot.bif').write_text('the_ROM_image:\n{\n  [bootloader] fsbl.elf\n  engineering.bit\n  application.elf\n}\n',encoding='ascii')
    with temporary_ascii_drive(ROOT,out/'path_alias.json') as alias:
        with (out/'bootgen.log').open('wb') as log:
            done=subprocess.run([str(launcher),'-arch','zynq','-image','boot.bif','-o','BOOT.BIN','-w','on'],
                cwd=alias/out.relative_to(ROOT),stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    log=(out/'bootgen.log').read_text(errors='replace')
    assert done.returncode==0 and 'ERROR' not in log.upper(),log
    blob=(out/'BOOT.BIN').read_bytes()
    assert len(blob)>2550000 and struct.unpack_from('<2I',blob,0x20)==(0xaa995566,0x584c4e58)
    assert tool['engine_sha256']==sha(engine) and tool['launcher_sha256']==sha(launcher)
    write(out/'boot_image_result.json',dict(status='BOOT_2026_HARDWARE_WITH_ACCEPTED_ARM_NOT_DEPLOYED',
        hardware_contract_sha256=sha(ROOT/'hardware/contract.json'),
        platform_receipt=platformpath.relative_to(ROOT).as_posix(),platform_receipt_sha256=sha(platformpath),
        bitstream_receipt=(bitpath/'stage_result.json').relative_to(ROOT).as_posix(),
        bitstream_receipt_sha256=sha(bitpath/'stage_result.json'),
        software_identity_sha256=sha(software/'software_identity.json'),arm_software_rebuilt_with_2026=False,
        bootgen=tool,ps_initialization_equivalent=True,
        artifacts={n:sha(out/n) for n in ('BOOT.BIN','engineering.bit','application.elf','fsbl.elf','R14CAL.BIN','SNAPCFG.BIN','boot.bif')},
        preserve_existing_BOOTSEQ_BIN=True,board_operations=0))
    print('BOOT image created; not downloaded or board-validated.')
if __name__=='__main__':main()
