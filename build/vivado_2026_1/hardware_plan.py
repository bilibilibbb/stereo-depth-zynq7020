from pathlib import Path
import sys
sys.path.insert(1,str(Path(__file__).resolve().parents[1]/'scripts'))
"""Resolve the complete synthesis dependency graph without private build paths."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, re
from vivado_generics import numeric_arguments

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def tcl_path(rel):
    assert re.fullmatch(r'[A-Za-z0-9_./-]+', rel), rel
    return '[file join $root {' + rel + '}]'

def tcl_list(paths):
    return '[list ' + ' '.join(tcl_path(p) for p in paths) + ']'

def boundary_header(out, module, modules, texts):
    code = texts[modules[module]]
    header = re.search(r'\bmodule\s+'+module+r'\b.*?;', code, re.S)
    assert header and len(re.findall(r'\bmodule\b', code)) == 1
    stub = out / 'boundaries' / (module + '.sv')
    stub.parent.mkdir(exist_ok=True)
    stub.write_text(code[:header.start()] + '\n(* black_box *) ' + header.group() + '\nendmodule\n', encoding='utf-8')
    return stub

def emit_component_inputs(out, record, modules):
    stage = record['stage']
    dest = out / 'stages' / stage
    dest.mkdir(parents=True)
    paths = [((out/'boundaries'/(m+'.sv')).relative_to(ROOT).as_posix()
              if (m := next((n for n in record['import_boundaries'] if modules[n] == p), None)) else p)
             for p in record['native_sources']]
    if stage == 'nn' or stage.startswith('nn_'):
        (dest/'nn_sources.tcl').write_text('set nn_rtl '+tcl_list(paths)+'\n', encoding='utf-8')
        with (dest/'nn_sources.tcl').open('a',encoding='utf-8') as stream:
            stream.write('set nn_top {'+record['top']+'}\n')
            stream.write('set nn_generics {'+numeric_arguments(record['effective_generics'])+'}\n')
            stream.write('set nn_imports [list '+' '.join('[list {'+module+'} '+tcl_path((out/'stages'/stage/'imported'/(module+'.edf')).relative_to(ROOT).as_posix())+']'
                for child,module in record.get('child_stages',{}).items())+']\n')
        (dest/'nn_clock.xdc').write_text('create_clock -name clk -period 10.0 [get_ports clk]\n', encoding='utf-8')
    elif stage in ('camera', 'sgm_volume'):
        (dest/'area_sources.tcl').write_text('set area_edifs {}\nset area_expected_blackboxes {}\nset area_rtl '+tcl_list(paths)+'\n', encoding='utf-8')
    else:
        checkpoint = '{}'
        edif = tcl_path((out/'stages'/stage/'imported'/('retained_neural_service_v2.edf' if stage=='compute' else 'fw_sgm_volume_rtl.edf')).relative_to(ROOT).as_posix())
        text = '\n'.join([
            'set area_top {'+record['top']+'}',
            'set area_compute_period_ns '+('10.0' if stage=='compute' else '0'),
            'set area_generics {'+numeric_arguments(record['effective_generics'])+'}',
            'set area_child {'+record['import_boundaries'][0]+'}',
            'set area_edif '+edif, 'set area_checkpoint '+checkpoint,
            'set area_rtl '+tcl_list(paths), ''])
        (dest/'area_sources.tcl').write_text(text, encoding='utf-8')
    record['generated_inputs'] = {p.relative_to(out).as_posix():sha(p) for p in dest.iterdir()}
    record['required_environment'] = ({'SNAPSHOT_LEGACY_NN_ADMITTED':'1'} if stage=='nn' or stage.startswith('nn_') else
        {'SNAPSHOT_LEGACY_AREA_ADMITTED':'1'} if stage in ('compute','sgm') else
        {'SNAPSHOT_AREA_ADMITTED':'1','SNAP_AREA_COMPONENT':stage,'SNAP_AREA_PROFILE':'baseline','SNAP_AREA_EXPORT':'1'})
    record['execution_condition'] = 'These admission flags are for the existing protected stage launcher after source/gate review; setting them alone is not admission.'

def resolve_sources():
    texts, modules = {}, {}
    for rel in read(ROOT / 'hardware/sources.json')['synthesis']:
        code = (ROOT / rel).read_text(encoding='utf-8')
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', code, flags=re.S)
        texts[rel] = code
        for module in re.findall(r'\bmodule\s+(\w+)', code):
            assert module not in modules, module
            modules[module] = rel
    def closure(top, cuts=()):
        pending, selected = [modules[top]], set()
        while pending:
            rel = pending.pop()
            if rel in selected:
                continue
            selected.add(rel)
            if not any(modules[m] == rel for m in cuts):
                pending.extend(modules[m] for m in set(re.findall(r'\b\w+\b', texts[rel])) & modules.keys())
        return sorted(selected)
    return closure, modules, texts

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED') == 'hardware-plan'
    out = Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    assert out.is_relative_to(ROOT / 'build2026')
    contract = read(ROOT / 'hardware/contract.json')
    assert contract['backend'] == 'v351a_v279_FIELD_PS'
    assert contract['clock_hz']['compute_conditional'] == 100000000
    for rel, digest in contract['sources'].items():
        assert sha(ROOT / rel) == digest, rel
    closure, modules, texts = resolve_sources()
    build_id = int(contract['platform_id'], 16)
    stages = [
        ('nn_core', 'retained_conv_mac128_v1', (), {}, 'nn_leaf_area.tcl', 'nn.edf'),
        ('nn_loader', 'raw_span_loader_v1_sdma_v1', (), {'ADDRESS_MIN':0x19000000, 'ADDRESS_END':0x1a000000}, 'nn_leaf_area.tcl', 'nn.edf'),
        ('nn_writer', 'row_output_lattice_dma_v2_sdma_v1', (), {'ADDRESS_MIN':0x19000000, 'ADDRESS_END':0x1a000000}, 'nn_leaf_area.tcl', 'nn.edf'),
        ('nn', 'retained_neural_service_v2', ('retained_conv_mac128_v1','raw_span_loader_v1_sdma_v1','row_output_lattice_dma_v2_sdma_v1'), {'ADDRESS_MIN':0x19000000, 'ADDRESS_END':0x1a000000}, 'assemble_nn.tcl', 'nn.edf'),
        ('sgm_volume', 'fw_sgm_volume_rtl', (), {}, 'component_area.tcl', 'component.edf'),
        ('camera', 'camera_pair_capture', (), {}, 'component_area.tcl', 'component.edf'),
        ('compute', 'r14_shared_compute_mmio_v1', ('retained_neural_service_v2',), {'ADDRESS_MIN':0x19000000, 'ADDRESS_END':0x1a000000, 'TWO_HP':1, 'BUILD_ID':build_id}, 'legacy_component_area.tcl', 'component.edf'),
        ('sgm', 'fw_sgm_mmio_rtl', ('fw_sgm_volume_rtl',), {'ADDRESS_MIN':0x194e0000, 'ADDRESS_END':0x19666000, 'BUILD_ID':build_id}, 'legacy_component_area.tcl', 'component.edf'),
    ]
    records = []
    for stage, top, cuts, parameters, script, artifact in stages:
        transitive = closure(top)
        native = closure(top, cuts)
        record = {'stage':stage, 'top':top, 'effective_generics':parameters,
                  'complete_source_closure':{rel:sha(ROOT/rel) for rel in transitive},
                  'native_sources':native, 'import_boundaries':list(cuts),
                  'script':'modern/'+script,
                  'script_sha256':sha(ROOT/'modern'/script), 'artifact':artifact}
        if stage=='nn':
            record['child_stages']=dict(zip(('nn_core','nn_loader','nn_writer'),cuts))
        for module in cuts:
            stub = boundary_header(out, module, modules, texts)
            record.setdefault('boundary_headers',{})[module] = {'source':modules[module], 'generated':stub.relative_to(out).as_posix(), 'sha256':sha(stub), 'requires_real_child_artifact':True}
        emit_component_inputs(out, record, modules)
        records.append(record)
    complete = set()
    cuts = ('r14_shared_compute_mmio_v1', 'fw_sgm_mmio_rtl', 'camera_pair_capture')
    for top in ('snapshot_compute_bdif', 'camera_ddr_bdif', 'learning_capture', 'snapshot_platform_top'):
        complete.update(closure(top, cuts))
    union = set(complete)
    for record in records:
        union.update(record['complete_source_closure'])
    listed = set(read(ROOT / 'hardware/sources.json')['synthesis'])
    assert union == listed, {'not_reached':sorted(listed-union), 'extra':sorted(union-listed)}
    platform_dir = out/'stages/platform'
    platform_dir.mkdir()
    platform_paths = []
    for rel in sorted(complete):
        module = next((m for m in cuts if modules[m] == rel), None)
        platform_paths.append(boundary_header(out,module,modules,texts).relative_to(ROOT).as_posix() if module else rel)
    from rebuild_hardware import platform_result
    receipt, platform, bd = platform_result(os.environ['SNAP_RELEASE_PLATFORM'])
    source_bd=receipt.parent/'project/snapshot.srcs/sources_1/bd/snapshot_bd'
    layout={'ip_leafs':[],'module_ref_wrappers':[]}
    xci_inputs={}
    for path in sorted((source_bd/'ip').glob('*/*.xci')):
        inst=read(path)['ip_inst'];vlnv=inst['component_reference']
        xci_inputs[path.relative_to(ROOT).as_posix()]=sha(path)
        if ':module_ref:' in vlnv:
            layout['module_ref_wrappers'].append(inst['xci_name'])
        elif vlnv.split(':')[2] != 'axi_interconnect':
            layout['ip_leafs'].append(inst['xci_name'])
    assert len(layout['ip_leafs'])==10 and len(layout['module_ref_wrappers'])==3,layout
    platform_text = '\n'.join([
        '# Use the freshly generated BD directory from run.py platform.',
        'if {![info exists ::env(SNAP_REBUILD_PLATFORM_BD)]} {error "Set SNAP_REBUILD_PLATFORM_BD to the current package-generated snapshot_bd directory"}',
        'set area_bd [file normalize $::env(SNAP_REBUILD_PLATFORM_BD)]',
        'set area_ip_names {'+' '.join(layout['ip_leafs'])+'}',
        'set area_leaf {}', 'set area_top snapshot_platform_top', 'set area_imports {}',
        'set area_custom_edifs '+tcl_list([(out/'stages/platform/imported'/(m+'.edf')).relative_to(ROOT).as_posix() for m in cuts]),
        'set area_complete 1', 'set area_native_children {'+' '.join(cuts)+'}',
        'set area_custom_wrappers {'+' '.join(layout['module_ref_wrappers'])+'}',
        'set area_expected_blackboxes {}', 'set area_child_checkpoints {}',
        'set area_rtl '+tcl_list(platform_paths), ''])
    (platform_dir/'area_sources.tcl').write_text(platform_text,encoding='utf-8')
    result = {'status':'SOURCE_DEPENDENCY_PLAN_VERIFIED_NOT_EXECUTED', 'created_utc':datetime.now(timezone.utc).isoformat(),
              'hardware_contract_sha256':sha(ROOT/'hardware/contract.json'), 'platform_id':contract['platform_id'],
              'part':'xc7z020clg400-2', 'compute_hz':100000000, 'stages':records,
              'platform_receipt':receipt.relative_to(ROOT).as_posix(), 'platform_receipt_sha256':sha(receipt),
              'xci_inputs':xci_inputs, 'generated_layout':layout,
              'platform_native_sources':sorted(complete), 'all_synthesis_sources':len(listed),
              'platform_input':{'path':'stages/platform/area_sources.tcl','sha256':sha(platform_dir/'area_sources.tcl')},
              'generated_inputs_scope':'Package-relative actual stage inputs and real-child boundaries; component/physical execution is separate and has not been run by this command.',
              'remaining_stages':['generated BD/IP plus complete platform assembly','constraint preflight','placement','routing','review actual physical results','bitstream and boot image after admission'],
              'physical_qualification':'Internal timing and external DVP/CDC constraints must be reported separately; prior physical results are not inherited.',
              'new_board_operations':0, 'eda_executed':False, 'complete_hardware_rebuild_verified':False}
    if os.environ.get('SNAP_RELEASE_REUSE_PLAN'):
        from rebuild_hardware import local,predecessor,evidence
        prior_path=local(os.environ['SNAP_RELEASE_REUSE_PLAN']);prior=read(prior_path)
        evidence(prior)
        result['reused_stages']={}
        current_tool=read(ROOT/'modern/tool_profile.json')
        for name in ('nn_core','nn_loader','nn_writer','nn','sgm_volume','camera','compute','sgm'):
            previous=next(x for x in prior['stages'] if x['stage']==name)
            current=next(x for x in records if x['stage']==name)
            for key in ('top','effective_generics','complete_source_closure','script','script_sha256','import_boundaries'):
                assert previous[key]==current[key],(name,key)
            old_stage=prior.get('reused_stages',{}).get(name)
            stage_path=local(old_stage['path']) if old_stage else prior_path.parent/'stages'/name/'stage_result.json'
            # A guarded interruption may leave only the early stages complete.
            # Reuse real PASS records only; failed or absent stages run afresh.
            if not stage_path.is_file() or read(stage_path).get('status')!='PASS':
                continue
            if any(child not in result['reused_stages'] for child in read(stage_path).get('dependencies',{})):
                continue
            directory,receipt=predecessor(prior_path.parent,name)
            assert receipt['actual_tool']==current_tool
            for child in receipt['dependencies']:
                assert child in result['reused_stages'],(name,child)
                assert receipt['dependencies'][child]['sha256']==result['reused_stages'][child]['sha256']
            result['reused_stages'][name]={'path':(directory/'stage_result.json').relative_to(ROOT).as_posix(),
                'sha256':sha(directory/'stage_result.json'),'reason':'Same source, parameters, tool, synthesis script and dependent child results; resume an interrupted build.'}
        # A physical-input packaging repair does not require synthesizing the
        # same complete platform again. Reuse only an actual guarded PASS,
        # with identical immutable platform export, production source graph,
        # installed tool, synthesis Tcl and all imported child receipts.
        old_area=prior.get('reused_stages',{}).get('platform')
        area_path=local(old_area['path']) if old_area else prior_path.parent/'stages/platform/stage_result.json'
        if area_path.is_file() and read(area_path).get('status')=='PASS':
            for key in ('platform_receipt','platform_receipt_sha256','xci_inputs','generated_layout','platform_native_sources'):
                assert prior[key]==result[key],('platform',key)
            assert sha(prior_path.parent/prior['platform_input']['path'])==prior['platform_input']['sha256']
            directory,receipt=predecessor(prior_path.parent,'platform')
            assert receipt['actual_tool']==current_tool
            assert receipt['execution_sources']['modern/platform_area.tcl']==sha(ROOT/'modern/platform_area.tcl')
            for child in ('compute','sgm','camera'):
                assert receipt['dependencies'][child]['sha256']==result['reused_stages'][child]['sha256']
            assert receipt['dependencies']['platform_export']=={'path':result['platform_receipt'],'sha256':result['platform_receipt_sha256']}
            result['reused_stages']['platform']={'path':(directory/'stage_result.json').relative_to(ROOT).as_posix(),
                'sha256':sha(directory/'stage_result.json'),'reason':'Identical complete platform and child results; physical netlist-bundle loading repair only.'}
            for name in ('preflight','place'):
                previous=prior.get('reused_stages',{}).get(name)
                candidate=local(previous['path']) if previous else prior_path.parent/'stages'/name/'stage_result.json'
                if not candidate.is_file() or read(candidate).get('status')!='PASS':break
                directory,receipt=predecessor(prior_path.parent,name)
                assert receipt['actual_tool']==current_tool
                for child,dependency in receipt['dependencies'].items():
                    assert dependency['sha256']==result['reused_stages'][child]['sha256']
                result['reused_stages'][name]={'path':(directory/'stage_result.json').relative_to(ROOT).as_posix(),
                    'sha256':sha(directory/'stage_result.json'),'reason':'Same validated platform, constraints and physical stage; resume later implementation only.'}
        if os.environ.get('SNAP_RELEASE_ROUTE_SEED'):
            from rebuild_hardware import inspect_route_seed
            assert 'place' in result['reused_stages']
            placed=local(result['reused_stages']['place']['path'])
            result['route_seed']=inspect_route_seed(os.environ['SNAP_RELEASE_ROUTE_SEED'],placed)
    (out/'hardware_plan.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'], 'all_synthesis_sources':len(listed),
                      'stages':{r['stage']:len(r['complete_source_closure']) for r in records}, 'eda_executed':False}))

if __name__ == '__main__':
    main()
