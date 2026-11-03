"""Link the actual CPU0 entry/SD/sensor/runtime plus private CPU1 into ELF.

An executable is still an undeployed candidate. Real calibration, matching
RTL/PS/FSBL and deployment admission are required; this script never downloads.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess
from import_platform_sdk import current_sdk
ROOT=Path(__file__).resolve().parents[1]
OUT=Path(os.environ['SNAP_BUILD_DIR'])/'firmware'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    from release_environment import tool
    gcc=tool('arm')
    runtime=OUT.parent/'runtime';r=json.loads((runtime/'result.json').read_text(encoding='utf-8'));assert r['status']=='PASS'
    combined=runtime/'snapshot_runtime_arm.o';assert sha(combined)==r['combined_sha256']
    OUT.mkdir();env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env['PATH']
    sdk=current_sdk()
    assert r['current_sdk_manifest_sha256']==sha(sdk/'IMPORT.json')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard',
           '-ffp-contract=off','-fno-fast-math','-ffunction-sections','-fdata-sections','-fstack-usage','-DR14_USE_Q40=1',
           '-DPROCESSOR_LITTLE_ENDIAN','-Ifirmware/network','-Ifirmware','-Icommon','-Ireference/compute/src',
           '-Ireference/compute/reference/v351a','-Ireference/network/gem','-Ireference/lwip/include',f'-I{sdk}/include']
    sources=['firmware/main.c','firmware/boot_config.c','firmware/boot_files.c','reference/compute/reference/v351a/retained_peripherals.c']
    asm=['boot.S','asm_vectors.S','cpu_init.S','xil-crt0.S']
    report={'status':'RUNNING','scope':'Full actual application ELF, real frozen compute and SDK/lwIP; no test inputs or measurement/replay replacement. Compilation/link/static-layout proof only, not board/physical/numerical acceptance.',
            'steps':[],'sources':[],'new_board_operations':0,'deployable_admission':False,'real_calibration_supplied':False,
            'current_sdk_manifest_sha256':sha(sdk/'IMPORT.json')}
    def save():(OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    with (OUT/'compile.log').open('wb') as log:
        def run(command):
            v=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
            report['steps'].append({'command':list(map(str,command)),'exit_code':v.returncode});save()
            if v.returncode:raise RuntimeError('firmware link failed; '+str(OUT/'compile.log'))
        try:
            objects=[combined]
            for i,s in enumerate(sources):
                obj=OUT/f'app_{i}.o';run([gcc,*flags,'-c',s,'-o',obj]);objects.append(obj)
                report['sources'].append({'source':s,'sha256':sha(ROOT/s)})
            for i,name in enumerate(('ff.c','diskio.c')):
                source=ROOT/'reference/fatfs'/name;obj=OUT/f'fatfs_{i}.o'
                run([gcc,*[f for f in flags if f not in ('-Wextra','-Werror')],'-c',source,'-o',obj]);objects.append(obj)
                report['sources'].append({'source':source.relative_to(ROOT).as_posix(),'sha256':sha(source)})
            aflags=['-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard','-DENABLE_ARM_ERRATA=1','-DSIM_MODE=0',f'-I{sdk}/include']
            for i,name in enumerate(asm):
                source=ROOT/'reference/compute/reference/a9_measure_boot/cpu0'/name
                obj=OUT/f'cpu0_{i}.o';run([gcc,*aflags,'-c',source,'-o',obj]);objects.append(obj)
                report['sources'].append({'source':source.relative_to(ROOT).as_posix(),'sha256':sha(source)})
            elf=OUT/'snapshot11459_UNDEPLOYED.elf'
            run([gcc,'-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard',f'-specs={sdk}/Xilinx.spec',
                 '-Wl,-T,firmware/boot/snapshot.ld,--gc-sections',f'-Wl,-Map,{OUT}/snapshot.map',*objects,
                 '-Wl,--start-group',sdk/'lib/libxil.a','-lm','-lc','-lgcc','-Wl,--end-group','-o',elf])
            nm=gcc.with_name('arm-none-eabi-nm.exe');size=gcc.with_name('arm-none-eabi-size.exe')
            undef=subprocess.check_output([nm,'-u',elf],env=env,text=True);assert not undef.strip(),undef
            defined=subprocess.check_output([nm,'--defined-only',elf],env=env,text=True)
            sym={v.split()[-1]:int(v.split()[0],16) for v in defined.splitlines() if len(v.split())==3}
            required=['main','ss_boot_files','r14_camera_open','ss_board_step','ss_gem_service','ss_ps_key1',
                      'ss_lg_worker_step','ss_cpu1_boot','ss_secondary_main','_start','MMUTable','XTime_SetTime','f_read','f_write']
            for name in required:assert name in sym,name
            assert sym['_vector_table']==0x02000000 and sym['__bss_end']<0x04000000
            assert sym['__stack']==0x04a00000 and sym['ss_cpu1_stack']==0x04c00000 and sym['ss_cpu1_undef_stack']<=0x04d00000
            assert not sym['MMUTable']%16384
            assert not re.search(r'\b(?:fw_host_|fw_am_|fw_color_test_device|layout_probe)',defined)
            info=subprocess.check_output([size,elf],env=env,encoding='utf-8',errors='backslashreplace')
            headers=subprocess.check_output([gcc.with_name('arm-none-eabi-readelf.exe'),'-h','-l',elf],env=env,text=True)
            (OUT/'symbols.txt').write_text(defined,encoding='utf-8');(OUT/'elf_headers.txt').write_text(headers,encoding='utf-8')
            (OUT/'size.txt').write_text(info,encoding='utf-8')
            report.update(status='PASS',elf_sha256=sha(elf),runtime_sha256=sha(combined),libxil_sha256=sha(sdk/'lib/libxil.a'),
                          required_real_entries=required,size=info,entry=sym['_vector_table'],bss_end=sym['__bss_end'],
                          complete_executable=True,not_a_deployment_pass=True)
            save();print('PASS: actual full CPU0/CPU1 small-model Ethernet ELF linked; UNDEPLOYED, no board proof',flush=True)
        except BaseException as exc:report.update(status='FAILED',error=repr(exc));save();raise
if __name__=='__main__':main()
