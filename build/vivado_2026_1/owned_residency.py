"""Release only the verified Vivado member's resident pages, never its data.

The handle and current-Job checks below reuse the deployed build helper.
Private commit, dynamic limits, CPU affinity and the parent guard are unchanged.
Paging may increase elapsed time. This cannot make a memory violation pass.
"""
from pathlib import Path
import ctypes
from ctypes import wintypes as w
import json, os, threading, time
from memory_guard import ProcessMemory
from run_guarded import memory_snapshot
from windows_job import ExtendedLimits
class PidList(ctypes.Structure):
    _fields_ = [('assigned', w.DWORD), ('count', w.DWORD),
                ('pids', ctypes.c_size_t * 128)]


def api_and_job():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    declarations = {
        'QueryInformationJobObject': ([w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.c_void_p], w.BOOL),
        'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        'CloseHandle': ([w.HANDLE], w.BOOL),
        'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
        'QueryFullProcessImageNameW': ([w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)], w.BOOL),
        'K32GetProcessMemoryInfo': ([w.HANDLE, ctypes.POINTER(ProcessMemory), w.DWORD], w.BOOL),
        'SetProcessWorkingSetSize': ([w.HANDLE, ctypes.c_size_t, ctypes.c_size_t], w.BOOL),
    }
    for name, (args, result) in declarations.items():
        function = getattr(api, name)
        function.argtypes, function.restype = args, result
    info = ExtendedLimits()
    if not api.QueryInformationJobObject(None, 9, ctypes.byref(info), ctypes.sizeof(info), None):
        raise ctypes.WinError(ctypes.get_last_error())
    required = 0x10 | 0x100 | 0x200 | 0x2000
    assert info.basic.flags & required == required
    assert not info.basic.flags & (0x800 | 0x1000)
    assert info.basic.affinity == 1 and info.process_memory > 0 and info.job_memory > 0
    assert os.getpid() in members(api), 'Not inside the guarded worker Job'
    return api


def members(api):
    value = PidList()
    # NULL queries the immediate Job of this guarded worker, not a global Job.
    if not api.QueryInformationJobObject(None, 3, ctypes.byref(value), ctypes.sizeof(value), None):
        raise ctypes.WinError(ctypes.get_last_error())
    assert value.count == value.assigned and value.count <= 128
    return set(value.pids[:value.count])


def process_memory(api, handle):
    info = ProcessMemory()
    info.cb = ctypes.sizeof(info)
    if not api.K32GetProcessMemoryInfo(handle, ctypes.byref(info), info.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(private_commit_bytes=info.private, working_set_bytes=info.working_set,
                page_faults=info.faults)


def reclaim_once(api, image):
    handles = []
    try:
        for pid in members(api):
            # Query-limited, synchronize and SET_QUOTA; no termination or VM write.
            handle = api.OpenProcess(0x101100, False, pid)
            if not handle:
                continue
            keep = False
            try:
                # Hold the process handle during both membership checks, avoiding
                # reuse of an exited PID to select a different application's process.
                if api.WaitForSingleObject(handle, 0) != 0x102 or pid not in members(api):
                    continue
                size = w.DWORD(32768)
                name = ctypes.create_unicode_buffer(size.value)
                if not api.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if Path(name.value).resolve() == image:
                    handles.append((pid, handle, name.value))
                    keep = True
            finally:
                if not keep:
                    api.CloseHandle(handle)
        # 2026.1 may run an additional Vivado compiler even when the separate
        # parallel_synth_helper launcher is disabled. Each held handle above
        # was matched to the exact installed engine and this owned Job.
        result = dict(current_job_verified=True, foreign_process_operations=0,
                      system_before=memory_snapshot(), processes=[])
        for pid, handle, name in handles:
            if pid not in members(api) or api.WaitForSingleObject(handle, 0) != 0x102:
                continue
            row = dict(pid=pid, image=name, before=process_memory(api, handle))
            minus_one = ctypes.c_size_t(-1).value
            if not api.SetProcessWorkingSetSize(handle, minus_one, minus_one):
                if api.WaitForSingleObject(handle, 0) == 0:
                    continue
                raise ctypes.WinError(ctypes.get_last_error())
            row['after']=process_memory(api, handle)
            result['processes'].append(row)
        result['system_after']=memory_snapshot()
        return result
    finally:
        for _, handle, _ in handles:
            api.CloseHandle(handle)


def with_residency(action, work, image, stage):
    api=api_and_job()
    phase={
        'place': ('Loading part xc7z020clg400-2', 'SNAPSHOT_PHYSICAL_PLACEMENT_STARTED',
                  'Phase 1.3 Build Placer Netlist Model', '# report_clocks -file clocks.rpt',
                  '# report_drc -file drc.rpt', '# write_checkpoint -force placed.dcp'),
        'route': ('Loading part xc7z020clg400-2', 'SNAPSHOT_PHYSICAL_ROUTE_STARTED',
                  'Phase 2.3 Pre Route Cleanup', 'Phase 2.5 Update Timing for Bus Skew',
                  '# report_clocks -file clocks.rpt', '# report_drc -file drc.rpt',
                  '# write_checkpoint -force routed.dcp'),
        'bitstream': ('Loading part xc7z020clg400-2', 'write_bitstream'),
    }.get(stage, ('Start RTL Elaboration', 'Loading part', 'Start RTL Component Statistics', 'Start Cross Boundary and Area Optimization', 'Finished Technology Mapping', 'Start IO Insertion'))
    result={'status':'MONITORING', 'guard_unchanged':True,
            'foreign_process_operations':0, 'phase_reclaims':[], 'pressure_reclaims':[]}
    stop=threading.Event()
    def save():
        (work/'owned_residency.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def usage():
        private=resident=0
        for pid in members(api):
            handle=api.OpenProcess(0x101000,False,pid)
            if not handle: continue
            try:
                if api.WaitForSingleObject(handle,0)!=0x102 or pid not in members(api): continue
                row=process_memory(api,handle)
                private+=row['private_commit_bytes']; resident+=row['working_set_bytes']
            finally: api.CloseHandle(handle)
        snap=memory_snapshot()
        margin=min(snap['available_bytes'],snap['commit_limit_bytes']-snap['commit_bytes'])-max(private,resident)
        return private,resident,margin
    def monitor():
        offset=0; tail=''; seen=set(); last_pressure=0.0
        try:
            while not stop.wait(.1):
                log=work/'vendor.stdout.log'
                if not log.exists(): continue
                with log.open('rb') as f:
                    f.seek(offset); chunk=f.read(); offset=f.tell()
                window=tail+chunk.decode('utf-8',errors='replace'); tail=window[-256:]
                pending=[m for m in phase if m not in seen and m in window]
                if pending:
                    private,resident,margin=usage()
                    if margin<=0: raise MemoryError('Live inequality violated; parent guard remains responsible for stopping the Job')
                    result['phase_reclaims'].append({'triggers':pending,'observation':reclaim_once(api,image)})
                    seen.update(pending);save()
                # Compiler helpers can grow between the parent's guard samples.
                # Reclaim earlier, at the guard cadence, while the live inequality
                # still holds. This never changes private commit or Job limits.
                if seen and time.monotonic()-last_pressure>=.25:
                    private,resident,margin=usage()
                    if margin<=0: raise MemoryError('Live inequality violated; no recovery of an over-limit Job')
                    if 0<margin<2*resident:
                        result['pressure_reclaims'].append({'live_margin_bytes':margin,
                            'owned_private_bytes':private,'owned_resident_bytes':resident,
                            'observation':reclaim_once(api,image)})
                        last_pressure=time.monotonic();save()
        except Exception as exc:
            result.update(status='MONITOR_ERROR',error=repr(exc))
        finally: save()
    observer=threading.Thread(target=monitor,name='owned-vivado-residency',daemon=True)
    observer.start()
    try: return action()
    finally:
        stop.set();observer.join(5)
        assert not observer.is_alive(),'Residency monitor did not retire'
        if result['status']!='MONITOR_ERROR': result['status']='MONITOR_RETIRED'
        save()
        assert result['status']!='MONITOR_ERROR',result.get('error')
