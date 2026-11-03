"""Paired full-C graph timing, never an ARM/PL throughput measurement.

Both entries use the same scalar convolution implementation and full postprocess.
The reference materializes float tensors and full-resolution attributes; the
typed entry uses the deployed layout/fused attributes. Verification and input
preparation are outside each timed call. Only the benchmark harness is new.
"""
from pathlib import Path
from datetime import datetime, timezone
import ctypes as C, hashlib, json, os, platform, statistics, subprocess, time

ROOT=Path(__file__).resolve().parents[1]
from release_environment import tool

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def aligned(n):
    backing=C.create_string_buffer(n+64)
    address=(C.addressof(backing)+63)&~63
    return backing,C.c_void_p(address)

def synthetic(frame):
    """Deterministic finite model-format tensors; no accuracy claim or real scene."""
    counts=[3*81920,3*81920,4*81920,81920,81920,34*5120]
    arrays=[(C.c_float*n)() for n in counts]
    for i,a in enumerate(arrays):
        for j in range(len(a)):
            value=((j*37+frame*53+i*101)%251)/250.0
            if i==2:value=(value*48.0 if j<81920 else value)
            a[j]=value
    return arrays

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='benchmark'
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    core=ROOT/'reference/compute/reference/v351a'
    gcc=tool('host')
    flags=['-shared','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-ffp-contract=off',
        '-fno-fast-math','-fno-tree-vectorize','-msse2','-mfpmath=sse',
        '-DR14_HOST_REFERENCE=1','-DR14_POST_VALIDATION_EXPORTS=1','-DR14_USE_Q40=1',
        '-I'+str(core),'-include',str(core/'r14_host_math.h')]
    units=['r14_graph_coop.c','retained_model_plan.c','ps_filters_v2.c','r14_host_math.c']
    dll=out/'graph_benchmark.dll'
    cmd=[str(gcc),*flags,*[str(core/n) for n in units],'-o',str(dll)]
    with (out/'compile.log').open('wb') as log:
        subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    dll_dir=os.add_dll_directory(str(gcc.parent));lib=C.CDLL(str(dll))
    assert lib.r14_host_math_init()==0
    # Both functions share the same frozen nonlinear/filter math in this DLL.
    if hasattr(lib,'retained_final_lut_init'):assert lib.retained_final_lut_init()==0
    for n in ('retained_model_workspace_bytes','r14_graph_workspace_bytes'):
        getattr(lib,n).restype=C.c_size_t
    sizes=[lib.retained_model_workspace_bytes(),lib.r14_graph_workspace_bytes()]
    spaces=[aligned(n) for n in sizes]
    outputs=[(C.c_float*81920)(),(C.c_float*81920)()]
    baseline=lib.retained_model_forward;current=lib.r14_graph_typed_forward
    baseline.argtypes=[C.c_void_p]*8+[C.c_size_t]+[C.c_void_p]*2
    current.argtypes=[C.c_void_p]*8+[C.c_size_t]+[C.c_void_p]*4
    fixture=os.environ.get('SNAP_BENCH_INPUTS')
    records=[]
    result={'status':'RUNNING','recorded_utc':datetime.now(timezone.utc).isoformat(),
        'host':platform.processor(),'compiler_sha256':sha(gcc),'compile_command':cmd,
        'source_files':{p.name:sha(p) for p in core.iterdir() if p.is_file()},
        'workspace_bytes':dict(reference=sizes[0],typed=sizes[1]),'runs':records,
        'scope':'Computer single-core, complete synchronous C entries; same scalar convolution and complete postprocessing. Prepared model inputs through padded output, excluding camera/SGM production, allocation, verification, two-core scheduling, DMA and PL acceleration.',
        'ARM_measured':False,'PL_measured':False,'board_speedup_established':False,
        'timer':'perf_counter_ns; one function call per sample; AB/BA ordering',
        'input_kind':'saved development tensors' if fixture else 'deterministic synthetic finite tensors'}
    if fixture:
        import struct
        file=Path(fixture).resolve(strict=True)
        with file.open('rb') as f:magic,frames,n=struct.unpack('<III',f.read(12))
        assert (magic,n)==(0x52474638,81920)
        result['input_file']={'name':file.name,'sha256':sha(file),'frames':frames}
    else:frames=3
    counts=[3*81920,3*81920,4*81920,81920,81920,34*5120]
    def save(): (out/'benchmark.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    save()
    for frame in range(frames):
        if fixture:
            with file.open('rb') as f:
                f.seek(12+frame*sum(counts)*4)
                arrays=[(C.c_float*n).from_buffer_copy(f.read(n*4)) for n in counts]
        else:arrays=synthetic(frame)
        pointers=[C.cast(a,C.c_void_p) for a in arrays]
        def call(version):
            args=[*pointers,C.cast(outputs[version],C.c_void_p),spaces[version][1],sizes[version],None,None]
            if version:args += [None,None]
            t=time.perf_counter_ns();rc=(current if version else baseline)(*args);elapsed=(time.perf_counter_ns()-t)/1e6
            assert rc==0,(version,frame,rc)
            return elapsed
        call(0);call(1)
        assert bytes(outputs[0])==bytes(outputs[1]),('warm output mismatch',frame)
        for repeat in range(3):
            order=[0,1] if (frame+repeat)%2==0 else [1,0]
            times={v:call(v) for v in order}
            assert bytes(outputs[0])==bytes(outputs[1]),('timed output mismatch',frame,repeat)
            records.append(dict(frame=frame,repeat=repeat,order=order,reference_ms=times[0],typed_ms=times[1],
                speedup=times[0]/times[1],bit_mismatches=0,
                output_sha256=hashlib.sha256(bytes(outputs[0])).hexdigest()))
            save()
        print('FRAME '+str(frame)+' '+json.dumps(records[-1]),flush=True)
    result.update(status='PASS',frames=frames,comparisons=len(records),output_values_checked=len(records)*81920,
        summary={name:{'median_ms':statistics.median(r[name+'_ms'] for r in records),
                       'min_ms':min(r[name+'_ms'] for r in records),'max_ms':max(r[name+'_ms'] for r in records)} for name in ('reference','typed')},
        median_paired_speedup=statistics.median(r['speedup'] for r in records),dll_sha256=sha(dll))
    save();dll_dir.close();print(json.dumps({k:result[k] for k in ('status','summary','median_paired_speedup','comparisons')}))

if __name__=='__main__':main()
