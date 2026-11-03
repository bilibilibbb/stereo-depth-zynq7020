"""Small actual-master check, called inside run_platform's original Job guard."""
from pathlib import Path
import hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check(out):
    out.mkdir()
    from release_environment import tool
    tools={name:{'path':str(tool(name))} for name in ('iverilog','vvp')}
    fixture=ROOT/'reference/rtl_checks/fw_nn_dma_axi_fixture.sv'
    test=ROOT/'tests/short_burst_dma_tb.sv'
    sources=[fixture,test,ROOT/'scripts/check_short_burst.py']
    result=dict(status='RUNNING',scope=__doc__,cases=[],board_operations=0)
    try:
        for name,relative,defines in [
            ('legacy_dual_hp','reference/legacy_v279/depth_dma_burst_pingpong_v1.v',['-DLEGACY_DMA','-DDMA_MODULE=depth_dma_burst_pingpong_v1']),
            ('compute','reference/compute/src/fw_shared_dma_backend.sv',['-DCOMPUTE_DMA','-DDMA_MODULE=fw_shared_dma_backend']),
            ('camera','reference/camera_rtl/depth_dma.v',['-DDMA_MODULE=depth_dma'])]:
            source=ROOT/relative;sources.append(source)
            text=source.read_text(encoding='utf-8');compact=re.sub(r'\s+','',text)
            assert 'n=left>16?16:left;if(n>boundary)n=boundary;burst_size=n[4:0];' in compact
            assert 'boundary=(13\'d4096-{1\'b0,addr[11:0]})>>3;' in compact
            assert "cmd_beats==0||cmd_beats>4096||cmd_address[2:0]!=0" in compact
            binary=out/(name+'.vvp')
            commands=[('compile',[tools['iverilog']['path'],'-g2012','-Wall','-s','short_burst_dma_tb',*defines,'-o',str(binary),str(source),str(fixture),str(test)]),
                      ('simulate',[tools['vvp']['path'],str(binary)])]
            for stage,command in commands:
                run=subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,encoding='utf-8',errors='replace',timeout=40)
                (out/(name+'_'+stage+'.log')).write_text(run.stdout,encoding='utf-8')
                assert run.returncode==0,(name,stage,run.stdout[-3000:])
            match=re.search(r'SHORT_BURST_PASS cases=(\d+) checks=(\d+) read_bursts=(\d+) write_bursts=(\d+) cycles=(\d+) poison=1',run.stdout)
            assert match,run.stdout
            values=dict(zip(('cases','checks','read_bursts','write_bursts','cycles'),map(int,match.groups())))
            assert values['cases']==26 and values['checks']==9216
            result['cases'].append(dict(name=name,**values))
        result['status']='PASS'
    except Exception as exc:
        result.update(status='FAILED',error=repr(exc));raise
    finally:
        result['sources']={p.relative_to(ROOT).as_posix():sha(p) for p in sources}
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
