"""Original memory, mutex and environment helpers; CLI task lists excluded."""
import ctypes, os
from ctypes import wintypes
from contextlib import contextmanager
from datetime import datetime, timezone
MUTEX_NAME = r'Local\StereoDepthConservativeWorker'
GUARD_ENV = 'STEREO_GUARDED_TASK'

class PerformanceInfo(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            'CommitTotal', 'CommitLimit', 'CommitPeak', 'PhysicalTotal',
            'PhysicalAvailable', 'SystemCache', 'KernelTotal', 'KernelPaged',
            'KernelNonpaged', 'PageSize')
    ] + [(name, wintypes.DWORD) for name in (
        'HandleCount', 'ProcessCount', 'ThreadCount')]


def memory_snapshot():
    if os.name != 'nt':
        raise RuntimeError('This resource check requires Windows.')
    api = ctypes.WinDLL('psapi', use_last_error=True).GetPerformanceInfo
    api.argtypes = [ctypes.POINTER(PerformanceInfo), wintypes.DWORD]
    api.restype = wintypes.BOOL
    info = PerformanceInfo()
    info.cb = ctypes.sizeof(info)
    if not api(ctypes.byref(info), info.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return {
        'utc': datetime.now(timezone.utc).isoformat(),
        'physical_total_bytes': info.PhysicalTotal * info.PageSize,
        'available_bytes': info.PhysicalAvailable * info.PageSize,
        'commit_bytes': info.CommitTotal * info.PageSize,
        'commit_limit_bytes': info.CommitLimit * info.PageSize,
        'commit_peak_since_boot_bytes': info.CommitPeak * info.PageSize,
        'process_count': info.ProcessCount,
    }


@contextmanager
def single_worker(name=MUTEX_NAME):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    api.CreateMutexW.restype = wintypes.HANDLE
    api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    api.WaitForSingleObject.restype = wintypes.DWORD
    api.ReleaseMutex.argtypes = [wintypes.HANDLE]
    api.ReleaseMutex.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    handle = api.CreateMutexW(None, False, name)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    owned = False
    try:
        status = api.WaitForSingleObject(handle, 0)
        if status == 0x102:
            raise RuntimeError('Another guarded task is active; run tasks serially.')
        if status not in (0, 0x80):
            raise ctypes.WinError(ctypes.get_last_error())
        owned = True
        yield
    finally:
        if owned:
            api.ReleaseMutex(handle)
        api.CloseHandle(handle)


def child_environment(task=None):
    env = os.environ.copy()
    for key in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS',
                'NUMEXPR_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS'):
        env[key] = '1'
    env['PYTHONUNBUFFERED'] = '1'
    env['PYTHONIOENCODING'] = 'utf-8'
    env.pop(GUARD_ENV, None)
    if task is not None:
        env[GUARD_ENV] = task
    return env
