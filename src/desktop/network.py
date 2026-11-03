"""Read-only adapter discovery and a fixed, temporary, elevated peer lease."""
from pathlib import Path
import ctypes
from ctypes import wintypes as w
import ipaddress, json, os, subprocess, sys

BOARD = '192.168.1.10'
HOSTS = ('192.168.1.100','192.168.1.101')


def resources():
    return Path(sys._MEIPASS) if getattr(sys,'frozen',False) else Path(__file__).resolve().parent/'stage'


def inspect_adapters():
    helper=resources()/'native/NetworkSetup.exe'
    p=subprocess.run([str(helper),'--inspect'],capture_output=True,text=True,encoding='utf-8-sig',
        errors='replace',timeout=12,creationflags=subprocess.CREATE_NO_WINDOW)
    if p.returncode:raise RuntimeError('无法读取有线网卡：'+p.stderr.strip())
    data=json.loads(p.stdout)
    if data.get('schema')!='stereo_network_v1':raise RuntimeError('网络检测返回格式错误')
    return data['adapters']


def compatible_address(adapter):
    for item in adapter['ips']:
        address=ipaddress.IPv4Address(item['address'])
        if (address in ipaddress.IPv4Network('192.168.1.0/24') and
            str(address) not in (BOARD,'192.168.1.0','192.168.1.255') and item['dad']=='Preferred'):
            return str(address)
    return None


def choose_adapter(adapters):
    """Ambiguity requires a user selection, even if one NIC has a suitable IP."""
    up=[a for a in adapters if a['physical'] and a['up']]
    return up[0] if len(up)==1 else None


def available_host(adapters):
    used={p['address'] for a in adapters for p in a['ips']}
    used.update(ip for a in adapters for ip in a.get('reserved_ips',[]))
    return next((h for h in HOSTS if h not in used),None)


def creation_time():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=w.HANDLE
    kernel.GetProcessTimes.argtypes=[w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4
    values=[w.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(kernel.GetCurrentProcess(),*[ctypes.byref(v) for v in values]):
        raise ctypes.WinError(ctypes.get_last_error())
    return values[0].dwLowDateTime | values[0].dwHighDateTime<<32


class ElevatedLease:
    """The helper removes only its own IP/context and host route on app exit."""
    def __init__(self,adapter,host):
        if not getattr(sys,'frozen',False):raise RuntimeError('自动配网仅从已打包应用启动；源码运行只允许检测。')
        if host not in HOSTS:raise ValueError('host')
        import uuid
        guid=str(uuid.UUID(adapter['id']))
        class Info(ctypes.Structure):
            _fields_=[('cbSize',w.DWORD),('fMask',w.ULONG),('hwnd',w.HWND),
                ('lpVerb',w.LPCWSTR),('lpFile',w.LPCWSTR),('lpParameters',w.LPCWSTR),('lpDirectory',w.LPCWSTR),
                ('nShow',ctypes.c_int),('hInstApp',w.HINSTANCE),('lpIDList',ctypes.c_void_p),('lpClass',w.LPCWSTR),
                ('hkeyClass',w.HKEY),('dwHotKey',w.DWORD),('hIcon',w.HANDLE),('hProcess',w.HANDLE)]
        helper=resources()/'native/NetworkSetup.exe'
        args=subprocess.list2cmdline(['--lease',guid,str(os.getpid()),str(creation_time()),host])
        info=Info();info.cbSize=ctypes.sizeof(info);info.fMask=0x40;info.lpVerb='runas'
        info.lpFile=str(helper);info.lpParameters=args;info.lpDirectory=str(helper.parent);info.nShow=0
        shell=ctypes.WinDLL('shell32',use_last_error=True)
        shell.ShellExecuteExW.argtypes=[ctypes.POINTER(Info)];shell.ShellExecuteExW.restype=w.BOOL
        if not shell.ShellExecuteExW(ctypes.byref(info)):
            code=ctypes.get_last_error()
            if code==1223:raise RuntimeError('已取消 Windows 配网授权，可手工配置后重新检测。')
            raise ctypes.WinError(code)
        self.handle=info.hProcess
        self.kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        self.kernel.GetExitCodeProcess.argtypes=[w.HANDLE,ctypes.POINTER(w.DWORD)]
        self.kernel.CloseHandle.argtypes=[w.HANDLE]

    def running(self):
        code=w.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle,ctypes.byref(code)):raise ctypes.WinError(ctypes.get_last_error())
        return code.value==259

    def close_handle(self):
        if self.handle:self.kernel.CloseHandle(self.handle);self.handle=None
