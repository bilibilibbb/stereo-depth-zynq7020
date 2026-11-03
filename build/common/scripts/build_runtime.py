"""Compile and relocatably link the real frozen compute + snapshot runtime.

No test stubs, board access or executable claim. Includes actual GEM/lwIP;
CPU0 application/CRT/SD/libc linkage is checked separately by build_firmware.
"""
from pathlib import Path
import hashlib,json,os,re,struct,subprocess
from import_platform_sdk import current_sdk

ROOT=Path(__file__).resolve().parents[1]
OUT=Path(os.environ['SNAP_BUILD_DIR'])/'runtime'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    from video_capture_source import verify as verify_video_capture
    verify_video_capture()
    from release_environment import tool
    gcc=tool('arm')
    OUT.mkdir();env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env['PATH']
    sdk=current_sdk()
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard',
           '-ffp-contract=off','-fno-fast-math','-ffunction-sections','-fdata-sections','-fstack-usage','-DR14_USE_Q40=1',
           '-DPROCESSOR_LITTLE_ENDIAN','-Ifirmware','-Icommon','-Ireference/compute/src','-Ireference/compute/reference/v351a',
           f'-I{sdk}/include',
           '-Ifirmware/legacy','-Ifirmware/network','-Ireference/network/gem','-Ireference/lwip/include','-Ireference/lwip/port']
    frozen=['src/fw_a9_platform.c','src/fw_observations.c','src/fw_sgm_driver.c',
            'reference/v351a/retained_camera_driver.c','reference/v351a/retained_image_lut_v1.c']
    legacy_names=['r14_gray_queue_v132a.c','r14_nn_coop.c','r14_integer_coop.c','r14_graph_device.c',
                  'r14_shared_compute.c','retained_model_plan.c','ps_filters_v2.c','r14_color_coop.c',
                  'r14_gray_coop.c','r14_field_coop.c','r14_filter_programs.c']
    legacy_sources=['reference/compute/reference/v351a/'+n for n in legacy_names]
    sources=['reference/compute/'+p for p in frozen]+legacy_sources+[
            'firmware/legacy/'+n for n in ('cpu_coordinator.c','field_stream_owned.c','stream_dispatch.c','backend.c')]+[
            'firmware/'+p for p in ('pair_export.c','latest_pair.c','small_model.c','snapshot_flow.c','camera_capture.c',
            'board_runtime.c','video_lane.c','video_capture.c','memory_layout.c','platform_a9.c','ps_io.c','network/gem_netif.c')]+['common/sdp_tcp.c']
    # Compile the exact retained legacy compute under its private symbol names;
    # the real camera keeps its rc_* ABI. Never link either former PL backend.
    report={'status':'RUNNING','scope':'Actual selected v351a/v279 production compute/camera/GEM/lwIP sources and private A9 cache unit; relocatable link only. No test fixtures, real inference, startup/ELF, RTL, clock or board proof.',
            'sources':[],'steps':[],'no_board_access':True,'complete_executable':False,'current_sdk_manifest_sha256':sha(sdk/'IMPORT.json')}
    # Preserve the two fixes already running on the real SD image. A fresh
    # build must not silently return to the older malloc LUT or incoherent
    # stream writer merely because these were originally linked as overrides.
    from stream_owned_source import verify as verify_owned
    verify_owned()
    production=json.loads((ROOT/'reference/build/runtime_fix_binding.json').read_text(encoding='utf-8'))
    assert production['status']=='PASS'
    for item in production['extra_dependencies']:assert sha(ROOT/item['path'])==item['sha256']
    report['extra_dependencies']=production['extra_dependencies']
    report['production_fix_reference']='reference/build/runtime_fix_binding.json'
    def save():(OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    with (OUT/'compile.log').open('wb') as log:
        def run(command):
            done=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
            report['steps'].append({'command':list(map(str,command)),'exit_code':done.returncode});save()
            if done.returncode:raise RuntimeError('actual runtime compile/link failed; see '+str(OUT/'compile.log'))
        try:
            objects=[]
            for i,source in enumerate(sources):
                obj=OUT/f'{i:02d}_{Path(source).stem}.o'
                print(f'ARM runtime {i+1}/{len(sources)} {source}',flush=True)
                extra=['-include','firmware/legacy/compute_namespace.h'] if source in legacy_sources else []
                if source=='firmware/legacy/cpu_coordinator.c':extra+=['-include','firmware/legacy/final_lut_static.h']
                run([str(gcc),*flags,*extra,'-c',source,'-o',str(obj)])
                if source in ('firmware/legacy/cpu_coordinator.c','firmware/legacy/field_stream_owned.c'):
                    original=next(x for x in production['sources'] if x['source']==source)
                    assert sha(obj)==original['object_sha256'],('Production fix changed',source)
                objects.append(obj);report['sources'].append({'source':source,'sha256':sha(ROOT/source),'object_sha256':sha(obj)})
            net_sources=sorted((ROOT/'reference/network/lwip').rglob('*.c'))+[
                ROOT/'reference/network/gem'/name for name in ('xemacps.c','xemacps_control.c','xemacps_bdring.c','xemacps_intr.c','xemacps_hw.c')]
            # Keep vendor warnings in the log; do not patch imported SDK code.
            net_flags=[f for f in flags if f not in ('-Wextra','-Werror')]
            for i,source in enumerate(net_sources):
                obj=OUT/f'net_{i:02d}_{source.stem}.o'
                print(f'ARM network {i+1}/{len(net_sources)} {source.name}',flush=True)
                run([str(gcc),*net_flags,'-c',str(source),'-o',str(obj)])
                objects.append(obj);report['sources'].append({'source':source.relative_to(ROOT).as_posix(),'sha256':sha(source),'object_sha256':sha(obj)})
            vendor=OUT/'sdk_cache_all.o';small=OUT/'sdk_cache.o'
            vendor_flags=[f for f in flags if f not in ('-Wextra','-Werror')]+['-DENABLE_ARM_ERRATA=1','-include','reference/compute/src/fw_color_bsp_rename.h']
            run([str(gcc),*vendor_flags,'-c',str(sdk/'xil_cache.c'),'-o',str(vendor)])
            run([str(gcc),'-nostdlib','-Wl,-r,--gc-sections','-Wl,-u,fw_color_bsp_Xil_DCacheFlushRange',
                 '-Wl,-u,fw_color_bsp_Xil_DCacheInvalidateRange',str(vendor),'-o',str(small)])
            # ONLY the secondary boot assembly uses the vendor's AMP switch
            # to skip shared SCU/L2 initialization. Runtime caches above never
            # use it; both cores retain locked PL310 maintenance.
            asm_flags=['-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard','-DENABLE_ARM_ERRATA=1','-DSIM_MODE=0',
                       f'-I{sdk}/include']
            rename=['-DUSE_AMP=1','-D_boot=ss_cpu1_vendor_boot','-D_prestart=ss_cpu1_vendor_prestart',
                    '-D_start=ss_secondary_main','-D_vector_table=ss_cpu1_vectors']
            for name in ('stack','irq_stack','supervisor_stack','abort_stack','fiq_stack','undef_stack'):
                rename.append(f'-D__{name}=ss_cpu1_{name}')
            boot_source='reference/compute/reference/a9_measure_boot/cpu1/boot.S'
            preprocessed=OUT/'secondary_vendor_preprocessed.S'
            run([str(gcc),*asm_flags,*rename,'-E','-P',boot_source,'-o',str(preprocessed)])
            asm=preprocessed.read_text(encoding='utf-8')
            assert not re.search(r'ldr\s+r[0-9]+,\s*=(?:L2CC|SLCR|0xf8f0000c)',asm,re.I), 'secondary would touch shared SCU/L2'
            assert re.search(r'b\s+ss_secondary_main',asm) and 'XTime_SetTime' not in asm and '__bss_start' not in asm
            assert re.search(r'orr\s+r0,\s*r0,\s*#\(0x01\s*\)',asm), 'maintenance broadcast omitted'
            for i,(source,extra) in enumerate([(boot_source,rename),('firmware/boot/secondary.S',[]),('firmware/boot/translation_table.S',[])]):
                obj=OUT/f'boot_{i}.o';run([str(gcc),*asm_flags,*extra,'-c',source,'-o',str(obj)]);objects.append(obj)
            table_bin=OUT/'mmu_table.bin'
            run([str(gcc.with_name('arm-none-eabi-objcopy.exe')),'-O','binary','-j','.mmu_tbl',str(objects[-1]),str(table_bin)])
            table=struct.unpack('<4096I',table_bin.read_bytes())
            assert table[0x70]==0x07011de2 and table[0x71]==0x07111de2 and table[0xfff]==0xfff11de2
            assert all(table[i]==(i<<20)|0x15de6 for i in range(0x100,0x1c0))
            assert table[0x43c]==0x43c00c02 and table[0xe00]==0xe0000c06 and table[0xf8f]==0xf8f00c06
            # An explicitly inert ELF checks the new linker MEMORY/stack
            # reservations. Never include this probe in the production link.
            probe=OUT/'layout_probe.o';probe_elf=OUT/'INERT_layout_probe_NOT_FIRMWARE.elf'
            run([str(gcc),*asm_flags,'-c','tests/layout_probe.S','-o',str(probe)])
            run([str(gcc),'-nostdlib','-Wl,-T,firmware/boot/snapshot.ld',str(probe),str(objects[-1]),'-o',str(probe_elf)])
            probe_symbols=subprocess.check_output([str(gcc.with_name('arm-none-eabi-nm.exe')),'--defined-only',str(probe_elf)],env=env,text=True)
            symbols={line.split()[-1]:int(line.split()[0],16) for line in probe_symbols.splitlines() if len(line.split())==3}
            assert symbols['_heap']==0x04000000 and symbols['_heap_end']==0x04800000
            assert symbols['_stack_end']==0x04800000 and symbols['__stack']==0x04a00000
            assert symbols['ss_cpu1_stack_end']==0x04a00000 and symbols['ss_cpu1_stack']==0x04c00000
            assert symbols['ss_cpu1_undef_stack']<=0x04d00000 and not symbols['MMUTable']%16384
            (OUT/'layout_probe_symbols.txt').write_text(probe_symbols,encoding='utf-8')
            report['secondary_boot']={'private_boot_only_USE_AMP':True,'runtime_cache_USE_AMP':False,
                'no_shared_L2_SCU_reset':True,'no_secondary_CRT_BSS_or_timer_reset':True,'one_shared_MMU_table':True,
                'actual_target_execution':False,'inert_linker_reservation_check':True}
            combined=OUT/'snapshot_runtime_arm.o'
            run([str(gcc),'-nostdlib','-Wl,-r',*map(str,objects),str(small),'-o',str(combined)])
            nm=gcc.with_name('arm-none-eabi-nm.exe')
            undefined=subprocess.check_output([str(nm),'-u',str(combined)],env=env,text=True)
            assert not re.search(r'\bSIM_MODE$',undefined,re.M), 'simulation-mode linker ambiguity'
            defined=subprocess.check_output([str(nm),'--defined-only',str(combined)],env=env,text=True)
            (OUT/'undefined_symbols.txt').write_text(undefined,encoding='utf-8')
            linker_stacks={f'ss_cpu1_{n}' for n in ('stack','irq_stack','supervisor_stack','abort_stack','fiq_stack','undef_stack')}
            missing=[line for line in undefined.splitlines() if re.search(r'\b(?:fw_|ss_|sdp_|r14_|retained_|rn_|rs_|rc_|__atomic_|__sync_)',line) and line.split()[-1] not in linker_stacks]
            if missing:raise RuntimeError('unresolved project/runtime symbols: '+repr(missing))
            required=['ss_board_step','ss_camera_step','ss_model_step','ss_lg_worker_step','r14_graph_coop_step','ss_lg_rc_poll','field_worker_step_v282','ss_lg_stream_worker_step','r14_cpu_worker_step_v179','fw_a9_clean','rc_poll','rs_start','sdp_tcp_step',
                      'ss_gem_init','ss_gem_service','ss_ps_prepare','ss_ps_key1','ss_ps_gem_clock','tcp_tmr','etharp_tmr','XEmacPs_BdRingFromHwRx',
                      'ss_memory_plan','ss_platform_prepare','ss_platform_release_cpu1','ss_secondary_main','ss_cpu1_boot','MMUTable']
            for symbol in required:
                if not re.search(r'\b'+symbol+r'$',defined,re.M):raise RuntimeError('missing real entry '+symbol)
            if re.search(r'\b(?:fw_color_test_device|fw_host_|fw_am_|unused_camera_checks)',defined):raise RuntimeError('test/measurement substitute entered runtime')
            size=subprocess.check_output([str(gcc.with_name('arm-none-eabi-size.exe')),str(combined)],env=env,encoding='utf-8',errors='backslashreplace')
            (OUT/'size.txt').write_text(size,encoding='utf-8')
            report.update(status='PASS',combined_sha256=sha(combined),remaining_dependencies=undefined.splitlines(),required_real_entries=required,size=size,
                          private_L2_cache_unit=True,USE_AMP=False)
            save();print('PASS: production compute/camera/GEM/lwIP runtime relocatable link; CPU0 entry/libc/startup checked separately.',flush=True)
        except BaseException as exc:
            report.update(status='FAILED',error=repr(exc));save();raise
if __name__=='__main__':main()
