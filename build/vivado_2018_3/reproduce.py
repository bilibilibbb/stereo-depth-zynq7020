"""Locate the documented tools, check them, then call the existing guarded flow.

doctor and verify never run EDA. rebuild is an explicit source-to-BOOT request.
No installation, global environment changes, board operations or version bypass.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parent
RELATIVE = {
    'HOST_PATH': ('Vivado/2018.3/tps/mingw/6.2.0/win64.o/nt/bin/gcc.exe',),
    'ARM_PATH': ('SDK/2018.3/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe',),
    'MAKE_PATH': ('SDK/2018.3/gnuwin/bin/make.exe',),
    'VIVADO_PATH': ('Vivado/2018.3/bin/vivado.bat',),
    'HSI_PATH': ('SDK/2018.3/bin/hsi.bat',),
    'BOOTGEN_PATH': ('SDK/2018.3/bin/bootgen.bat',),
    'IVERILOG_PATH': ('bin/iverilog.exe', 'iverilog.exe'),
    'VVP_PATH': ('bin/vvp.exe', 'vvp.exe'),
}

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def specifications(root=ROOT):
    config = json.loads((root/'config/toolchain.json').read_text(encoding='utf-8'))
    rows = [{**item, 'name': name, 'expected_sha256': item['sha256']}
            for name, item in config.items()]
    for name in ('vivado', 'hsi', 'bootgen'):
        item = json.loads((root/f'config/{name}.json').read_text(encoding='utf-8'))
        rows.append({**item, 'name': name, 'executable': name+'.bat',
                     'expected_sha256': item['launcher_sha256']})
    return rows

def resolve_candidate(spec, env, xilinx_root=None, iverilog_root=None, path_lookup=shutil.which):
    variable = spec['environment']
    # An explicitly configured tool is never silently replaced by a different one.
    if env.get(variable):
        return Path(env[variable]).expanduser(), 'environment'
    root = iverilog_root if variable in ('IVERILOG_PATH', 'VVP_PATH') else xilinx_root
    if root is not None:
        choices = [Path(root)/rel for rel in RELATIVE[variable]]
        return next((p for p in choices if p.is_file()), choices[0]), 'installation_root'
    found = path_lookup(spec['executable'], path=env.get('PATH', ''))
    return (Path(found), 'PATH') if found else (None, 'not_found')

def inspect_tool(spec, path, origin):
    row = {'name': spec['name'], 'environment': spec['environment'], 'origin': origin,
           'path': str(path.resolve()) if path is not None else None,
           'expected_sha256': spec['expected_sha256']}
    if path is None or not path.is_file():
        row.update(status='MISSING', message='Install the documented tool or provide its path.')
        return row
    try:
        row['actual_sha256'] = sha(path)
        if row['actual_sha256'] != spec['expected_sha256']:
            row.update(status='DIFFERENT_TOOL', message='This tool is not the qualified version; do not edit hashes to bypass the check.')
            return row
        if 'engine_relative' in spec:
            engine = path.parent/spec['engine_relative']
            row['engine_path'] = str(engine.resolve())
            row['engine_sha256'] = sha(engine) if engine.is_file() else None
            if row['engine_sha256'] != spec['engine_sha256']:
                row.update(status='DIFFERENT_ENGINE', message='Launcher matches but the installed engine is absent or different.')
                return row
        row.update(status='READY', message='Matches the documented qualified tool.')
    except OSError as exc:
        row.update(status='UNREADABLE', message=str(exc))
    return row

def check_sources(root=ROOT):
    records = json.loads((root/'source_manifest.json').read_text(encoding='utf-8'))['files']
    for item in records:
        path = (root/item['path']).resolve(strict=True)
        if not path.is_relative_to(root.resolve()) or sha(path) != item['sha256']:
            raise RuntimeError('Source mismatch: '+item['path'])
    return len(records)

def doctor(root=ROOT, env=None, xilinx_root=None, iverilog_root=None):
    env = dict(os.environ if env is None else env)
    rows = [inspect_tool(spec, *resolve_candidate(spec, env, xilinx_root, iverilog_root))
            for spec in specifications(root)]
    result = {'checked_utc': datetime.now(timezone.utc).isoformat(),
              'profile': 'Windows / Vivado 2018.3 / SDK 2018.3',
              'platform_ok': os.name == 'nt',
              'python': sys.version.split()[0],
              'python_qualified_minor': sys.version_info[:2] == (3, 12),
              'tools': rows, 'eda_started': False, 'board_operations': 0,
              'modern_vivado_compatibility': '2025.2/2026.1 full rebuild not validated; this entry does not migrate SDK, IP or HDF to XSA.'}
    try:
        result['source_files'] = check_sources(root)
        result['source_status'] = 'PASS'
    except (OSError, ValueError, KeyError, RuntimeError) as exc:
        result.update(source_status='FAIL', source_error=str(exc))
    result['ready'] = (result['platform_ok'] and result['python_qualified_minor'] and
                       result['source_status'] == 'PASS' and all(r['status']=='READY' for r in rows))
    result['status'] = 'ENVIRONMENT_READY' if result['ready'] else 'SETUP_REQUIRED'
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('doctor', 'verify', 'rebuild'), nargs='?', default='doctor')
    parser.add_argument('--xilinx-root', type=Path, help='Parent containing Vivado/2018.3 and SDK/2018.3')
    parser.add_argument('--iverilog-root', type=Path, help='Icarus installation directory or bin directory')
    parser.add_argument('--json-out', type=Path, help='Write this invocation check to a new JSON file')
    args = parser.parse_args()
    if args.action == 'verify':
        # Source-only review remains useful on computers without vendor tools.
        return subprocess.call([sys.executable, '-X', 'utf8', str(ROOT/'run.py'), 'verify'], cwd=ROOT)
    result = doctor(xilinx_root=args.xilinx_root, iverilog_root=args.iverilog_root)
    if args.json_out:
        with args.json_out.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2)
            stream.write('\n')
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    if not result['ready']:
        print('SETUP REQUIRED: see docs/reviewer_quickstart.md. No EDA or board operation started.', file=sys.stderr)
        return 2
    if args.action == 'doctor':
        return 0
    env = os.environ.copy()
    for row in result['tools']:
        env[row['environment']] = row['path']
    # Existing stages keep their own dynamic memory guard and global mutex.
    return subprocess.call([sys.executable, '-X', 'utf8', str(ROOT/'rebuild_all.py')], cwd=ROOT, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
