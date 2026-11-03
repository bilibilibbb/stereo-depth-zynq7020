"""Dynamic protection for this project's owned Windows job, not other apps.

U is max(current tree private commit, summed current working sets). Require
U < current physical available and U < current commit headroom. For a further
allocation d, U+d must remain below R-d, so d < (R-U)/2. This derives the job
commit cap from live headroom; it is not a fixed memory budget or percentage.
Windows enforces the commit cap between samples. External allocations and
working-set changes still require sampling; instantaneous global guarantees
are not possible. Historical peaks are diagnostic only.
"""
import ctypes
from ctypes import wintypes as w
import mmap
import subprocess
import time

from run_guarded import memory_snapshot
from windows_job import ExtendedLimits

MODE = 'owned_tree_below_live_available_v1'
POLL_SECONDS = 0.25


class MemoryPressure(RuntimeError):
    pass


def limits_for(snapshot, usage=None):
    usage = usage or {'private_bytes': 0, 'working_set_bytes': 0}
    private = int(usage['private_bytes'])
    resident = int(usage['working_set_bytes'])
    used = max(private, resident)
    available = int(snapshot['available_bytes'])
    committed = int(snapshot['commit_bytes'])
    ceiling = int(snapshot['commit_limit_bytes'])
    if min(private, resident, available, committed) < 0 or ceiling <= 0 or committed >= ceiling:
        raise MemoryPressure('Memory information invalid or commit headroom exhausted')
    remaining = min(available, ceiling - committed)
    if used >= remaining:
        raise MemoryPressure('Owned tree usage is not below current available memory/commit headroom')
    # The extra allocation raises usage and lowers available memory together.
    # Strict inequality, rounded down to an OS page; no fixed GiB thresholds.
    cap = (private + (remaining - used - 1) // 2) // mmap.PAGESIZE * mmap.PAGESIZE
    if cap <= private:
        raise MemoryPressure('No positive page-sized growth margin remains')
    return {'process_bytes': cap, 'tree_bytes': cap,
            'used_bytes': used, 'available_bytes': available,
            'commit_headroom_bytes': ceiling - committed,
            'margin_bytes': remaining - used}


def admit(policy, snapshot):
    if policy.get('mode') != MODE:
        raise MemoryPressure('Dynamic memory policy missing or unsupported')
    return limits_for(snapshot)


class ProcessMemory(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('faults', w.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            'peak_working_set', 'working_set', 'peak_paged_pool', 'paged_pool',
            'peak_nonpaged_pool', 'nonpaged_pool', 'pagefile', 'peak_pagefile', 'private')]


def current_tree_usage(child):
    api = child.api
    api.IsProcessInJob.argtypes = [w.HANDLE, w.HANDLE, ctypes.POINTER(w.BOOL)]
    api.IsProcessInJob.restype = w.BOOL
    api.K32GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(ProcessMemory), w.DWORD]
    api.K32GetProcessMemoryInfo.restype = w.BOOL
    private = resident = 0
    members = []
    for member in child.members():
        pid = member['pid']
        handle = api.OpenProcess(0x101000, False, pid)  # query-limited + synchronize
        if not handle:
            error = ctypes.get_last_error()
            if error == 87:  # Process exited after enumeration.
                continue
            raise ctypes.WinError(error)
        try:
            if api.WaitForSingleObject(handle, 0) == 0:
                continue
            belongs = w.BOOL()
            if not api.IsProcessInJob(handle, child.job, ctypes.byref(belongs)):
                raise ctypes.WinError(ctypes.get_last_error())
            if not belongs.value:  # PID reused outside our job; never operate on it.
                continue
            info = ProcessMemory()
            info.cb = ctypes.sizeof(info)
            if not api.K32GetProcessMemoryInfo(handle, ctypes.byref(info), info.cb):
                if api.WaitForSingleObject(handle, 0) == 0:
                    continue
                raise ctypes.WinError(ctypes.get_last_error())
            private += info.private
            resident += info.working_set
            members.append(pid)
        finally:
            api.CloseHandle(handle)
    return {'private_bytes': private, 'working_set_bytes': resident, 'members': members}


def apply_limits(child, limits):
    info = ExtendedLimits()
    api = child.api
    if not api.QueryInformationJobObject(child.job, 9, ctypes.byref(info), ctypes.sizeof(info), None):
        raise ctypes.WinError(ctypes.get_last_error())
    required = 0x10 | 0x100 | 0x200 | 0x2000
    if info.basic.flags & required != required or info.basic.flags & (0x800 | 0x1000):
        raise MemoryPressure('Owned job lost affinity/memory/kill-on-close/no-breakaway protection')
    info.process_memory = limits['process_bytes']
    info.job_memory = limits['tree_bytes']
    if not api.SetInformationJobObject(child.job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise ctypes.WinError(ctypes.get_last_error())


def sample(child, snapshot_provider=memory_snapshot):
    usage = current_tree_usage(child)
    snapshot = snapshot_provider()
    row = {'usage': usage, 'system': snapshot}
    try:
        row['limits'] = limits_for(snapshot, usage)
    except MemoryPressure as exc:
        exc.sample = row
        raise
    apply_limits(child, row['limits'])
    return row


def wait_guarded(child, report, *, timeout_seconds=None, snapshot_provider=memory_snapshot):
    start = time.monotonic()
    history = report.setdefault('memory_watch', {'mode': MODE, 'samples': 0})
    while True:
        try:
            row = sample(child, snapshot_provider)
            limit = row['limits']
            history['samples'] += 1
            history.setdefault('first', row)
            history['last'] = row
            for name, value, choose in (
                ('max_tree_used_bytes', limit['used_bytes'], max),
                ('min_margin_bytes', limit['margin_bytes'], min),
                ('min_limit_bytes', limit['tree_bytes'], min),
                ('max_limit_bytes', limit['tree_bytes'], max)):
                history[name] = choose(history.get(name, value), value)
        except Exception as exc:
            report['stop_reason'] = 'dynamic memory protection: ' + str(exc)
            history['failed_sample'] = getattr(exc, 'sample', None)
            child.terminate(124)
            child.wait(5)
            return 124
        if timeout_seconds is not None and time.monotonic() - start > timeout_seconds:
            report['stop_reason'] = 'bounded-check diagnostic timeout'
            child.terminate(124)
            child.wait(5)
            return 124
        try:
            return child.wait(POLL_SECONDS)
        except subprocess.TimeoutExpired:
            pass
