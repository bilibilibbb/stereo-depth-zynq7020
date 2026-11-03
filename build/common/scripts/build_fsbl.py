"""Rebuild SDK FSBL from an explicit, verified package platform export."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, re, shutil, subprocess, zipfile
from ascii_project_path import temporary_ascii_drive

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED') == 'fsbl', 'Use run.py fsbl --platform <receipt>'
    out = Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    assert out.is_relative_to(ROOT / 'build')
    receipt = Path(os.environ['SNAP_RELEASE_PLATFORM']).resolve(strict=True)
    assert receipt.is_relative_to(ROOT / 'build')
    platform = read(receipt)
    guard = read(receipt.parent / 'guard.json')
    assert platform['status'] == guard['status'] == 'PASS' and guard['action'] == 'platform'
    assert guard['exit_code'] == 0 and platform['new_board_operations'] == 0
    assert not platform['synthesis_run']
    assert platform['hardware_contract_sha256'] == sha(ROOT / 'hardware/contract.json')
    admission = read(ROOT / 'reference/build/platform_admission.json')
    assert admission['allowed_action'] == 'platform_generation_only'
    assert admission['hardware_contract_sha256'] == platform['hardware_contract_sha256']
    assert platform['admission_sha256'] == sha(ROOT / 'reference/build/platform_admission.json')
    weights = admission['weights']
    assert sha(ROOT / weights['path']) == weights['sha256']
    contract = read(ROOT / 'hardware/contract.json')
    for rel, digest in contract['sources'].items():
        assert sha(ROOT / rel) == digest, rel
    assert sha(ROOT / 'hardware/generated_contract.tcl') == contract['generated_tcl_sha256']
    for rel, digest in platform['generated'].items():
        assert sha(receipt.parent / rel) == digest, rel

    spec = read(ROOT / 'config/hsi.json')
    launcher = Path(os.environ[spec['environment']]).resolve(strict=True)
    assert sha(launcher) == spec['launcher_sha256']
    assert sha(launcher.parent / spec['engine_relative']) == spec['engine_sha256']
    arm = read(ROOT / 'config/toolchain.json')['arm']
    gcc = Path(os.environ[arm['environment']]).resolve(strict=True)
    assert sha(gcc) == arm['sha256']
    shutil.copyfile(receipt.parent / 'snapshot.hwdef', out / 'snapshot.hdf')
    with zipfile.ZipFile(out / 'snapshot.hdf') as z:
        assert 'ps7_init.c' in z.namelist() and not any(n.endswith('.bit') for n in z.namelist())
        ps_init_hash = hashlib.sha256(z.read('ps7_init.c')).hexdigest()
    result = {'status': 'RUNNING', 'started_utc': datetime.now(timezone.utc).isoformat(),
              'platform_receipt': receipt.relative_to(ROOT).as_posix(), 'platform_receipt_sha256': sha(receipt),
              'hardware_contract_sha256': sha(ROOT / 'hardware/contract.json'),
              'hdf_sha256': sha(out / 'snapshot.hdf'), 'ps7_init_sha256': ps_init_hash,
              'platform_generation_admission_sha256': platform['admission_sha256'],
              'new_board_operations': 0, 'boot_image_created': False, 'physical_pass': False}
    try:
        env = os.environ.copy()
        env.update(SNAPSHOT_BOOT_SUPPORT_ADMITTED='1', MAKEFLAGS='-j1')
        with temporary_ascii_drive(ROOT, out / 'path_alias.json') as alias:
            work = alias / out.relative_to(ROOT)
            (work / 'vendor.stdin').write_bytes(b'')
            with (work / 'vendor.stdin').open('rb') as inp, (work / 'hsi.stdout.log').open('wb') as stdout, (work / 'hsi.stderr.log').open('wb') as stderr:
                command = [str(launcher), '-mode', 'batch', '-source', str(alias / 'scripts/boot_support.tcl'),
                           '-nojournal', '-log', 'hsi.log', '-tclargs', str(work)]
                done = subprocess.run(command, cwd=work, env=env, stdin=inp, stdout=stdout, stderr=stderr, timeout=300)
        logs = (out / 'hsi.stdout.log').read_text(errors='replace') + (out / 'hsi.stderr.log').read_text(errors='replace')
        assert done.returncode == 0 and 'SNAPSHOT_BOOT_SUPPORT_GENERATED_UNDEPLOYED' in logs, logs[-3000:]
        elfs = list((out / 'fsbl').rglob('*.elf'))
        assert len(elfs) == 1
        elf = elfs[0]
        ps_files = list((out / 'fsbl').rglob('ps7_init.c'))
        assert ps_files and all(sha(p) == ps_init_hash for p in ps_files)
        libraries = list((out / 'fsbl').rglob('libxil.a'))
        headers = list((out / 'fsbl').rglob('xparameters.h'))
        assert libraries and headers
        undefined = subprocess.check_output([str(gcc.with_name('arm-none-eabi-nm.exe')), '-u', str(elf)], text=True)
        assert not undefined.strip()
        symbols = subprocess.check_output([str(gcc.with_name('arm-none-eabi-nm.exe')), '-n', str(elf)], text=True)
        for name in ('main', 'ps7_init', 'FsblHandoff'):
            assert re.search(r'\b' + name + r'$', symbols, re.M), name
        (out / 'fsbl_symbols.txt').write_text(symbols, encoding='utf-8')
        result.update(status='PASS', elf=elf.relative_to(ROOT).as_posix(), elf_sha256=sha(elf),
                      no_undefined_symbols=True,
                      generated={p.relative_to(out).as_posix(): sha(p) for p in libraries + headers + ps_files},
                      scope='SDK-generated FSBL and BSP from this package platform export; no bitstream, boot image or board operation.')
    finally:
        if result['status'] == 'RUNNING':
            result['status'] = 'FAILED'
        (out / 'fsbl_result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
