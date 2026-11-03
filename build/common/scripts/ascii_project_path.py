"""Temporary unused DOS drive alias to this project for legacy Windows EDA.

The source tree is not copied or moved. Exact-target removal only removes the
mapping created here; existing drive mappings are never reused or overwritten.
"""
from contextlib import contextmanager
from pathlib import Path
import ctypes,json
from ctypes import wintypes as W

@contextmanager
def temporary_ascii_drive(root, receipt):
    root=Path(root).resolve(strict=True)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetLogicalDrives.argtypes=[];kernel.GetLogicalDrives.restype=W.DWORD
    kernel.QueryDosDeviceW.argtypes=[W.LPCWSTR,W.LPWSTR,W.DWORD];kernel.QueryDosDeviceW.restype=W.DWORD
    kernel.DefineDosDeviceW.argtypes=[W.DWORD,W.LPCWSTR,W.LPCWSTR];kernel.DefineDosDeviceW.restype=W.BOOL
    def query(name):
        buffer=ctypes.create_unicode_buffer(32768)
        n=kernel.QueryDosDeviceW(name,buffer,len(buffer))
        if not n:
            error=ctypes.get_last_error()
            if error==2:return None
            raise ctypes.WinError(error)
        return buffer.value
    drive=None
    for letter in 'ZYXWVUTSRQP':
        name=letter+':'
        if kernel.GetLogicalDrives()&(1<<(ord(letter)-ord('A'))) or query(name) is not None:continue
        drive=name;break
    if drive is None:raise RuntimeError('No unused drive letter for a temporary project alias')
    target='\\??\\'+str(root)
    record=dict(drive=drive,target=str(root),raw_target=target,created=False,removed=False)
    def save():Path(receipt).write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    save()
    if not kernel.DefineDosDeviceW(0x1|0x8,drive,target):raise ctypes.WinError(ctypes.get_last_error())
    record['created']=True;save()
    try:
        assert query(drive)==target,'Unexpected alias target'
        alias=Path(drive+'\\')
        assert (alias/'source_manifest.json').read_bytes()==(root/'source_manifest.json').read_bytes()
        yield alias
    finally:
        # Remove exactly this raw target, not whichever mapping is currently on
        # top. A concurrent unrelated mapping cannot be removed by this call.
        ok=kernel.DefineDosDeviceW(0x1|0x2|0x4|0x8,drive,target)
        record['removed']=bool(ok)
        if not ok:record['remove_error']=ctypes.get_last_error()
        save()
        if not ok:raise ctypes.WinError(record['remove_error'])
