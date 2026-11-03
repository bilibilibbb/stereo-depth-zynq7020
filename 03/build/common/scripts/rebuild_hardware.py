"""Execute one source-bound hardware stage with fresh predecessor validation.

The parent run.py owns the Windows Job, live memory guard and global mutex.
Historical numerical evidence applies only to byte-identical production inputs;
capacity, placement, routing and output images always come from this rebuild.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib,json,os,re,shutil,subprocess
from ascii_project_path import temporary_ascii_drive
ROOT=Path(__file__).resolve().parents[1]
STAGES=('nn','sgm_volume','camera','compute','sgm','platform','preflight','place','route','bitstream')

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def local(p):
    q=Path(p);q=(q if q.is_absolute() else ROOT/q).resolve(strict=True)
    if not q.is_relative_to(ROOT):raise ValueError('Input must belong to this unpacked package')
    return q
def check_files(base,entries):
    for p,digest in entries.items():
        item=(base/p).resolve(strict=True)
        assert item.is_relative_to(base.resolve()) and sha(item)==digest,p
def tpath(p):return '[file join $root {'+p.relative_to(ROOT).as_posix()+'}]'
def evidence(plan):
    admission=read(ROOT/'reference/build/rebuild_admission.json')
    assert admission['allowed_scope']=='unchanged_source_rebuild_without_board_operations'
    assert sha(ROOT/'hardware/contract.json')==admission['hardware_contract_sha256']==plan['hardware_contract_sha256']
    check_files(ROOT,admission['production_sources'])
    check_files(ROOT,admission['evidence_files'])
    return admission
def metrics(path):return {k:int(v) for k,v in (x.split() for x in path.read_text().splitlines())}
def predecessor(folder,stage):
    p=folder/'stages'/stage/'stage_result.json';r=read(p)
    assert r['status']=='PASS' and r['stage']==stage,p
    assert sha(ROOT/'hardware/contract.json')==r['hardware_contract_sha256']
    check_files(p.parent,r['artifacts'])
    for name,digest in r['execution_sources'].items():
        current=ROOT/name
        if sha(current)==digest:continue
        # Preserve the exact earlier launcher bytes when only the packaging
        # validator is repaired. Production inputs/Tcl/outputs/guard still bind
        # normally; no old PASS is rewritten or applied to changed hardware.
        historic=ROOT/'reference/build/executors'/(digest+'.py')
        assert name=='scripts/rebuild_hardware.py' and historic.is_file() and sha(historic)==digest,name
    assert read(local(r['guard']))['status']=='PASS','Predecessor guard did not finish successfully'
    return p.parent,r
def platform_result(path):
    p=local(path);r=read(p)
    assert r['status']=='PASS' and r['hardware_contract_sha256']==sha(ROOT/'hardware/contract.json')
    assert read(p.parent/'guard.json')['status']=='PASS'
    check_files(p.parent,r['generated'])
    bd=p.parent/'project/snapshot.srcs/sources_1/bd/snapshot_bd'
    assert bd.is_dir()
    return p,r,bd
def primitive_dict(m):
    return '[dict create '+' '.join(k+' '+str(m[v]) for k,v in [('DSP48E1','dsp'),('RAMB36E1','ramb36'),('RAMB18E1','ramb18'),('PS7','ps7'),('IOBUF','iobuf')])+']'

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='hardware-stage'
    job=local(os.environ['SNAP_BUILD_DIR']);planpath=local(os.environ['SNAP_RELEASE_PLAN']);plan=read(planpath)
    assert plan['status']=='SOURCE_DEPENDENCY_PLAN_VERIFIED_NOT_EXECUTED'
    admission=evidence(plan);folder=planpath.parent
    stage=os.environ['SNAP_RELEASE_STAGE'];assert stage in STAGES
    work=folder/'stages'/stage;work.mkdir(exist_ok=True)
    assert not (work/'stage_result.json').exists(),'This stage already has an attempt; make a new hardware plan to retry, preserving failures.'
    spec=read(ROOT/'config/vivado.json');launcher=Path(os.environ[spec['environment']]).resolve(strict=True)
    assert sha(launcher)==spec['launcher_sha256'] and sha(launcher.parent/spec['engine_relative'])==spec['engine_sha256']
    env=os.environ.copy();deps={};generated=[];record=next((x for x in plan['stages'] if x['stage']==stage),None)
    if record:
        assert sha(ROOT/record['script'])==record['script_sha256']
        check_files(ROOT,record['complete_source_closure']);check_files(folder,record['generated_inputs'])
        for entry in record.get('boundary_headers',{}).values():assert sha(folder/entry['generated'])==entry['sha256']
        script=record['script'];env.update(record['required_environment'])
        for prior in ({'compute':['nn'],'sgm':['sgm_volume']}.get(stage,[])):
            d,r=predecessor(folder,prior);deps[prior]={'path':str((d/'stage_result.json').relative_to(ROOT)),'sha256':sha(d/'stage_result.json')}
            if prior=='sgm_volume':
                imported=work/'imported';imported.mkdir(exist_ok=True)
                target=imported/'fw_sgm_volume_rtl.edf';shutil.copyfile(d/'component.edf',target)
                assert sha(target)==sha(d/'component.edf')
    elif stage=='platform':
        pp,pr,bd=platform_result(os.environ['SNAP_RELEASE_PLATFORM'])
        assert sha(folder/plan['platform_input']['path'])==plan['platform_input']['sha256']
        for prior in ('compute','sgm','camera'):
            d,r=predecessor(folder,prior);deps[prior]={'path':str((d/'stage_result.json').relative_to(ROOT)),'sha256':sha(d/'stage_result.json')}
            imported=work/'imported';imported.mkdir(exist_ok=True)
            module={'compute':'r14_shared_compute_mmio_v1','sgm':'fw_sgm_mmio_rtl','camera':'camera_pair_capture'}[prior]
            target=imported/(module+'.edf');shutil.copyfile(d/'component.edf',target)
            assert sha(target)==sha(d/'component.edf')
        deps['platform_export']={'path':pp.relative_to(ROOT).as_posix(),'sha256':sha(pp)}
        env.update(SNAPSHOT_PLATFORM_AREA_ADMITTED='1',SNAP_AREA_EXPORT='1')
        script='scripts/platform_area.tcl'
    elif stage in ('preflight','place'):
        area,ar=predecessor(folder,'platform');m=ar['primitive_counts']
        assert m.get('blackboxes',0)==0 and m['latches']==0 and m['ps7']==1 and m['iobuf']==6
        # A count of LUT primitive cells is not the occupied Slice LUT total:
        # Vivado packs compatible LUT primitives. Use its full utilization row.
        usage=(area/'utilization.rpt').read_text()
        lut_match=re.search(r'\|\s*Slice LUTs\*?\s*\|\s*(\d+)\s*\|\s*\d+\s*\|\s*(\d+)',usage)
        assert lut_match and int(lut_match[2])==53200,'Missing complete Slice LUT accounting'
        assert m['dsp']<=220 and m['ramb36']+m['ramb18']/2<=140 and int(lut_match[1])<=53200 and m['ff']<=106400
        if stage=='place':predecessor(folder,'preflight')
        pp,pr,bd=platform_result(os.environ['SNAP_RELEASE_PLATFORM'])
        assert ar['dependencies']['platform_export']=={'path':pp.relative_to(ROOT).as_posix(),'sha256':sha(pp)}
        net=work/'snapshot_platform_top.edf';shutil.copyfile(area/'component.edf',net)
        assert sha(net)==sha(area/'component.edf')
        xpm=launcher.parent.parent/'data/ip/xpm/xpm_cdc/tcl'
        constraints={n:xpm/(n+'.tcl') for n in admission['xpm_constraint_hashes']}
        for n,p in constraints.items():assert sha(p)==admission['xpm_constraint_hashes'][n],n
        lines=['set inspected_primitive_counts '+primitive_dict(m),'set inspected_netlist '+tpath(net),
            'set inspected_ps_xdc '+tpath(bd/'ip/snapshot_bd_ps_0/snapshot_bd_ps_0.xdc'),
            'set xpm_constraint_files [dict create '+' '.join('{'+n+'} {'+p.as_posix()+'}' for n,p in constraints.items())+']']
        if stage=='place':lines=['set physical_operation {place_and_report_only}','set logic_optimization {}']+lines
        (work/'inputs.tcl').write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env['SNAPSHOT_PHYSICAL_'+('PREFLIGHT' if stage=='preflight' else 'PLACEMENT')+'_ADMITTED']='1'
        script='scripts/physical_'+('preflight' if stage=='preflight' else 'placement')+'.tcl'
        deps['platform']={'path':(area/'stage_result.json').relative_to(ROOT).as_posix(),'sha256':sha(area/'stage_result.json')}
    elif stage=='route':
        placed,pr=predecessor(folder,'place');area,ar=predecessor(folder,'platform')
        (work/'inputs.tcl').write_text('set inspected_checkpoint '+tpath(placed/'placed.dcp')+'\nset inspected_primitive_counts '+primitive_dict(ar['primitive_counts'])+'\n',encoding='utf-8')
        env['SNAPSHOT_PHYSICAL_ROUTE_ADMITTED']='1';script='scripts/physical_route.tcl'
        deps['place']={'path':(placed/'stage_result.json').relative_to(ROOT).as_posix(),'sha256':sha(placed/'stage_result.json')}
    else:
        routed,rr=predecessor(folder,'route')
        assert rr['internal_timing_pass'] and rr['fully_routed'] and not rr['routing_errors']
        (work/'inputs.tcl').write_text('set inspected_checkpoint '+tpath(routed/'routed.dcp')+'\n',encoding='utf-8')
        env['SNAPSHOT_BITSTREAM_ADMITTED']='1';script='scripts/rebuild_bitstream.tcl'
        deps['route']={'path':(routed/'stage_result.json').relative_to(ROOT).as_posix(),'sha256':sha(routed/'stage_result.json')}
    result={'status':'RUNNING','stage':stage,'recorded_utc':datetime.now(timezone.utc).isoformat(),
        'hardware_contract_sha256':sha(ROOT/'hardware/contract.json'),'hardware_plan':{'path':planpath.relative_to(ROOT).as_posix(),'sha256':sha(planpath)},
        'admission_sha256':sha(ROOT/'reference/build/rebuild_admission.json'),'dependencies':deps,
        'guard':(job/'guard.json').relative_to(ROOT).as_posix(),
        'execution_sources':{script:sha(ROOT/script),'scripts/rebuild_hardware.py':sha(Path(__file__))},
        'new_board_operations':0,'external_camera_timing_qualified':False,'board_pass':False}
    if stage in ('place','route','bitstream'):
        result['execution_sources']['scripts/owned_residency.py']=sha(ROOT/'scripts/owned_residency.py')
    def save():(work/'stage_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        with temporary_ascii_drive(ROOT,job/'path_alias.json') as alias:
            if stage=='platform':env['SNAP_REBUILD_PLATFORM_BD']=str(alias/bd.relative_to(ROOT))
            with (work/'vendor.stdout.log').open('wb') as log,(work/'vendor.stdin').open('wb') as f:f.write(b'')
            with (work/'vendor.stdout.log').open('wb') as log,(work/'vendor.stdin').open('rb') as inp:
                command=[str(launcher),'-mode','batch','-nojournal','-source',str(alias/script),'-log','vivado.log']
                def execute():
                    return subprocess.run(command,cwd=alias/work.relative_to(ROOT),env=env,stdin=inp,stdout=log,stderr=subprocess.STDOUT,timeout=1770 if stage=='route' else 570)
                if stage in ('place','route','bitstream'):
                    from owned_residency import with_residency
                    done=with_residency(execute,work,(launcher.parent/spec['engine_relative']).resolve(strict=True),stage)
                else: done=execute()
        log=(work/'vendor.stdout.log').read_text(errors='replace')
        assert done.returncode==0 and 'ERROR:' not in log,log[-4000:]
        assert 'CRITICAL WARNING:' not in log, 'Review critical warnings in the preserved build log'
        if stage in STAGES[:6]:
            m=metrics(work/'metrics.tsv');assert m.get('blackboxes',0)==m.get('latches',0)==0,m
            result['primitive_counts']=m
            required=['metrics.tsv','utilization.rpt','hierarchy.rpt',record['artifact'] if record else 'component.edf']
            if stage=='platform':required+=['pad_buffers.tsv','ip_sources.tsv']
        elif stage in ('preflight','place','route'):
            required=['clocks.rpt','timing.rpt','utilization.rpt','drc.rpt','cdc_raw.rpt','check_timing.rpt','applied_constraints.xdc']
            if stage=='place':required+=['placed.dcp']
            if stage=='route':
                required+=['routed.dcp','route_status.rpt','bus_skew.rpt']
                timing=(work/'timing.rpt').read_text()
                summary=timing.split('| Design Timing Summary',1)[1]
                row=next(line.split() for line in summary.splitlines() if re.fullmatch(r'\s*(?:-?\d+(?:\.\d+)?\s+){11}-?\d+(?:\.\d+)?\s*',line))
                values=list(map(float,row))
                result.update(WNS_ns=values[0],WHS_ns=values[4],WPWS_ns=values[8],
                    internal_timing_pass=all(values[i]>=0 for i in (0,4,8)) and all(values[i]==0 for i in (1,2,5,6,9,10)) and all(values[i]>0 for i in (3,7,11)))
                route=(work/'route_status.rpt').read_text()
                result['routing_errors']=int(re.search(r'nets with routing errors\.*\s*:?\s*(\d+)',route,re.I)[1])
                route_counts=[int(re.search(label+r'\.*\s*:?\s*(\d+)',route,re.I)[1]) for label in ('Routable nets','Fully routed nets')]
                result['fully_routed']=route_counts[0]==route_counts[1] and route_counts[0]>0
                assert result['internal_timing_pass'] and result['fully_routed'] and not result['routing_errors'],'Fresh route does not meet internal timing/route admission'
                skew=(work/'bus_skew.rpt').read_text()
                assert not re.search(r'(?i)VIOLATED|timing constraints are not met',skew),'Bus skew violation requires review'
        else:required=['engineering.bit']
        for name in required:assert (work/name).stat().st_size>0,name
        result['artifacts']={p.relative_to(work).as_posix():sha(p) for p in work.rglob('*') if p.is_file() and p.name!='stage_result.json'}
        result['status']='PASS';print(json.dumps({'stage':stage,'status':'PASS','output':work.relative_to(ROOT).as_posix()}))
    except BaseException as exc:result.update(status='FAILED',error=repr(exc));raise
    finally:save()

if __name__=='__main__':main()
