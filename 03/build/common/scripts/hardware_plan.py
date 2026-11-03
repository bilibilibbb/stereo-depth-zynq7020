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
    if stage == 'nn':
        (dest/'nn_sources.tcl').write_text('set nn_rtl '+tcl_list(paths)+'\n', encoding='utf-8')
        (dest/'nn_clock.xdc').write_text('create_clock -name clk -period 10.0 [get_ports clk]\n', encoding='utf-8')
    elif stage in ('camera', 'sgm_volume'):
        (dest/'area_sources.tcl').write_text('set area_edifs {}\nset area_expected_blackboxes {}\nset area_rtl '+tcl_list(paths)+'\n', encoding='utf-8')
    else:
        checkpoint = tcl_path((out/'stages/nn/nn.dcp').relative_to(ROOT).as_posix()) if stage == 'compute' else '{}'
        edif = tcl_path((out/'stages/sgm/imported/fw_sgm_volume_rtl.edf').relative_to(ROOT).as_posix()) if stage == 'sgm' else '{}'
        text = '\n'.join([
            'set area_top {'+record['top']+'}',
            'set area_compute_period_ns '+('10.0' if stage=='compute' else '0'),
            'set area_generics {'+numeric_arguments(record['effective_generics'])+'}',
            'set area_child {'+record['import_boundaries'][0]+'}',
            'set area_edif '+edif, 'set area_checkpoint '+checkpoint,
            'set area_rtl '+tcl_list(paths), ''])
        (dest/'area_sources.tcl').write_text(text, encoding='utf-8')
    record['generated_inputs'] = {p.relative_to(out).as_posix():sha(p) for p in dest.iterdir()}
    record['required_environment'] = ({'SNAPSHOT_LEGACY_NN_ADMITTED':'1'} if stage=='nn' else
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
    assert out.is_relative_to(ROOT / 'build')
    contract = read(ROOT / 'hardware/contract.json')
    assert contract['backend'] == 'v351a_v279_FIELD_PS'
    assert contract['clock_hz']['compute_conditional'] == 100000000
    for rel, digest in contract['sources'].items():
        assert sha(ROOT / rel) == digest, rel
    closure, modules, texts = resolve_sources()
    build_id = int(contract['platform_id'], 16)
    stages = [
        ('nn', 'retained_neural_service_v2', (), {'ADDRESS_MIN':0x19000000, 'ADDRESS_END':0x1a000000}, 'legacy_nn_area.tcl', 'nn.dcp'),
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
                  'script':'scripts/'+script, 'script_sha256':sha(ROOT/'scripts'/script), 'artifact':artifact}
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
    layout = read(ROOT/'hardware/rebuild_layout.json')
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
              'platform_native_sources':sorted(complete), 'all_synthesis_sources':len(listed),
              'platform_input':{'path':'stages/platform/area_sources.tcl','sha256':sha(platform_dir/'area_sources.tcl')},
              'generated_inputs_scope':'Package-relative actual stage inputs and real-child boundaries; component/physical execution is separate and has not been run by this command.',
              'remaining_stages':['generated BD/IP plus complete platform assembly','constraint preflight','placement','routing','review actual physical results','bitstream and boot image after admission'],
              'physical_qualification':'Internal timing and external DVP/CDC constraints must be reported separately; prior physical results are not inherited.',
              'new_board_operations':0, 'eda_executed':False, 'complete_hardware_rebuild_verified':False}
    (out/'hardware_plan.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'], 'all_synthesis_sources':len(listed),
                      'stages':{r['stage']:len(r['complete_source_closure']) for r in records}, 'eda_executed':False}))

if __name__ == '__main__':
    main()
