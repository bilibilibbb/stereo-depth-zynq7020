"""Windows process-tree limits, applied before the first child instruction.

Only the newly created suspended process is assigned. The owner and other apps
are never members. CPython's own Windows process launcher handles the explicit
inherited-handle list; all job APIs use declared pointer-width-safe signatures.
"""
import ctypes
from ctypes import wintypes as w
import os
import subprocess


class BasicLimits(ctypes.Structure):
    _fields_ = [('process_time',ctypes.c_int64),('job_time',ctypes.c_int64),
        ('flags',w.DWORD),('min_ws',ctypes.c_size_t),('max_ws',ctypes.c_size_t),
        ('active_limit',w.DWORD),('affinity',ctypes.c_size_t),
        ('priority',w.DWORD),('scheduling',w.DWORD)]


class IoCounters(ctypes.Structure):
    _fields_ = [(n,ctypes.c_uint64) for n in ('read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes')]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [('basic',BasicLimits),('io',IoCounters),
        ('process_memory',ctypes.c_size_t),('job_memory',ctypes.c_size_t),
        ('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]


class Accounting(ctypes.Structure):
    _fields_ = [(n,ctypes.c_int64) for n in ('user_time','kernel_time','period_user','period_kernel')]+[
        (n,w.DWORD) for n in ('page_faults','total_processes','active_processes','terminated_processes')]


class JobProcess:
    """Own one process tree; close always terminates remaining members."""
    def __init__(self,command,*,cwd,env,stdout,process_bytes,job_bytes):
        if os.name!='nt' or process_bytes<=0 or job_bytes<=0:
            raise ValueError('Positive limits and Windows are required')
        import _winapi
        import msvcrt
        self.api=ctypes.WinDLL('kernel32',use_last_error=True)
        self.job=None;self.process=None;self.pid=None;self.returncode=None
        declarations={
            'CreateJobObjectW':([ctypes.c_void_p,w.LPCWSTR],w.HANDLE),
            'SetInformationJobObject':([w.HANDLE,ctypes.c_int,ctypes.c_void_p,w.DWORD],w.BOOL),
            'QueryInformationJobObject':([w.HANDLE,ctypes.c_int,ctypes.c_void_p,w.DWORD,ctypes.c_void_p],w.BOOL),
            'AssignProcessToJobObject':([w.HANDLE,w.HANDLE],w.BOOL),
            'TerminateJobObject':([w.HANDLE,w.UINT],w.BOOL),
            'TerminateProcess':([w.HANDLE,w.UINT],w.BOOL),
            'GetCurrentProcess':([],w.HANDLE),
            'GetProcessAffinityMask':([w.HANDLE,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)],w.BOOL),
            'ResumeThread':([w.HANDLE],w.DWORD),
            'CloseHandle':([w.HANDLE],w.BOOL),
            'OpenProcess':([w.DWORD,w.BOOL,w.DWORD],w.HANDLE),
            'WaitForSingleObject':([w.HANDLE,w.DWORD],w.DWORD),
            'QueryFullProcessImageNameW':([w.HANDLE,w.DWORD,w.LPWSTR,ctypes.POINTER(w.DWORD)],w.BOOL),
        }
        for name,(args,result) in declarations.items():
            api=getattr(self.api,name);api.argtypes=args;api.restype=result
        self.job=self.api.CreateJobObjectW(None,None)
        if not self.job:raise ctypes.WinError(ctypes.get_last_error())
        thread=None;copies=[]
        try:
            allowed,system=ctypes.c_size_t(),ctypes.c_size_t()
            if not self.api.GetProcessAffinityMask(self.api.GetCurrentProcess(),ctypes.byref(allowed),ctypes.byref(system)):
                raise ctypes.WinError(ctypes.get_last_error())
            info=ExtendedLimits()
            # affinity, process/job committed bytes, kill on owner close; no breakaway.
            info.basic.flags=0x10|0x100|0x200|0x2000
            info.basic.affinity=allowed.value & -allowed.value
            info.process_memory=process_bytes;info.job_memory=job_bytes
            if not self.api.SetInformationJobObject(self.job,9,ctypes.byref(info),ctypes.sizeof(info)):
                raise ctypes.WinError(ctypes.get_last_error())
            with open(os.devnull,'rb') as stdin:
                current=_winapi.GetCurrentProcess()
                for file in (stdin,stdout):
                    copies.append(_winapi.DuplicateHandle(current,msvcrt.get_osfhandle(file.fileno()),current,0,True,_winapi.DUPLICATE_SAME_ACCESS))
                startup=subprocess.STARTUPINFO()
                startup.dwFlags=subprocess.STARTF_USESTDHANDLES
                startup.hStdInput=copies[0];startup.hStdOutput=copies[1];startup.hStdError=copies[1]
                startup.lpAttributeList={'handle_list':copies}
                self.process,thread,self.pid,_=_winapi.CreateProcess(None,subprocess.list2cmdline([str(c) for c in command]),
                    None,None,True,0x4|subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW,env,str(cwd),startup)
            if not self.api.AssignProcessToJobObject(self.job,self.process):
                raise ctypes.WinError(ctypes.get_last_error())
            if self.api.ResumeThread(thread)==0xffffffff:
                raise ctypes.WinError(ctypes.get_last_error())
        except BaseException:
            # Also covers assignment failure: the child is still suspended and
            # might not belong to the job, so terminate this exact handle first.
            if self.process:
                self.api.TerminateProcess(self.process,125)
            self.close()
            raise
        finally:
            if thread:self.api.CloseHandle(thread)
            for handle in copies:_winapi.CloseHandle(handle)

    def wait(self,timeout):
        import _winapi
        status=_winapi.WaitForSingleObject(self.process,max(0,int(timeout*1000)))
        if status==0x102:raise subprocess.TimeoutExpired('guarded process',timeout)
        if status!=0:raise ctypes.WinError(ctypes.get_last_error())
        self.returncode=_winapi.GetExitCodeProcess(self.process)
        return self.returncode

    def stats(self):
        limits=ExtendedLimits();account=Accounting()
        for cls,value in ((9,limits),(1,account)):
            if not self.api.QueryInformationJobObject(self.job,cls,ctypes.byref(value),ctypes.sizeof(value),None):
                raise ctypes.WinError(ctypes.get_last_error())
        return {'process_limit_bytes':limits.process_memory,'job_limit_bytes':limits.job_memory,
            'peak_process_commit_bytes':limits.peak_process,'peak_job_commit_bytes':limits.peak_job,
            'affinity_mask':hex(limits.basic.affinity),'active_processes':account.active_processes,
            'total_processes':account.total_processes,'cpu_user_seconds':account.user_time/1e7,
            'cpu_kernel_seconds':account.kernel_time/1e7}

    def terminate(self,code=124):
        if self.job and not self.api.TerminateJobObject(self.job,code):
            raise ctypes.WinError(ctypes.get_last_error())

    def members(self):
        class PidList(ctypes.Structure):
            _fields_=[('assigned',w.DWORD),('count',w.DWORD),('pids',ctypes.c_size_t*128)]
        value=PidList()
        if not self.api.QueryInformationJobObject(self.job,3,ctypes.byref(value),ctypes.sizeof(value),None):
            raise ctypes.WinError(ctypes.get_last_error())
        result=[]
        for pid in value.pids[:value.count]:
            row={'pid':pid,'image':None};handle=self.api.OpenProcess(0x101000,False,pid)
            if handle:
                try:
                    if self.api.WaitForSingleObject(handle,0)==0:continue
                    size=w.DWORD(32768);path=ctypes.create_unicode_buffer(size.value)
                    if self.api.QueryFullProcessImageNameW(handle,0,path,ctypes.byref(size)):row['image']=path.value
                finally:self.api.CloseHandle(handle)
            result.append(row)
        return result

    def close(self):
        # Closing the non-inherited last job handle kills descendants, including
        # tools left behind when a subprocess timeout killed only their parent.
        if self.job:
            self.api.CloseHandle(self.job);self.job=None
        if self.process:
            self.api.CloseHandle(self.process);self.process=None

    def __enter__(self):return self
    def __exit__(self,*args):self.close()
