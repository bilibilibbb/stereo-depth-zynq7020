"""Source and artifact checks for the separate 2026.1 migration."""
from pathlib import Path,PurePosixPath
import hashlib,json,os,re,shutil,zipfile

def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,value):Path(p).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(p):
    with Path(p).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def checked_path(root,rel):
    name=PurePosixPath(rel)
    if name.is_absolute() or '..' in name.parts or '\\' in rel:raise ValueError('Unsafe relative path: '+rel)
    p=(Path(root)/Path(*name.parts)).resolve(strict=True)
    if not p.is_relative_to(Path(root).resolve()):raise ValueError('Path escapes source root')
    return p

def verify_sources(root):
    root=Path(root).resolve(strict=True)
    manifest=read(root/'modern/source_manifest.json')
    if sha(root/'source_manifest.json')!=manifest['baseline_manifest_sha256']:
        raise RuntimeError('Baseline source manifest changed')
    base=read(root/'source_manifest.json')['files']
    for item in base+manifest['migration_files']:
        p=checked_path(root,item['path'])
        if p.stat().st_size!=item['bytes'] or sha(p)!=item['sha256']:
            raise RuntimeError('Source changed: '+item['path'])
    contract=read(root/'hardware/contract.json')
    if contract['backend']!='v351a_v279_FIELD_PS' or contract['clock_hz']['compute_conditional']!=100000000:
        raise RuntimeError('Production contract changed')
    return len(base)+len(manifest['migration_files'])

def require_version(output):
    matches=re.findall(r'\bVivado\s+v(\d+\.\d+(?:\.\d+)?)\b',output,re.I)
    if len(set(matches))!=1 or matches[0]!='2026.1':
        raise RuntimeError('Expected the actual Vivado 2026.1 base release; found '+repr(matches))
    return matches[0]

def find_vivado(explicit=None,env=None):
    env=os.environ if env is None else env
    value=explicit or env.get('VIVADO_2026_PATH')
    if value:
        path=Path(value).expanduser().resolve()
        return path if path.is_file() else None
    # No fallback to the old VIVADO_PATH profile; a PATH tool still has to pass
    # the real version query before generating any platform.
    found=shutil.which('vivado.bat',path=env.get('PATH',''))
    return Path(found).resolve() if found else None

def engine_for(launcher):
    launcher=Path(launcher)
    options=[launcher.parent/'unwrapped/win64.o/vivado.exe',launcher.parent/'vivado.exe']
    existing=[p.resolve() for p in options if p.is_file()]
    if len(existing)!=1:raise RuntimeError('Cannot identify one installed Vivado engine')
    return existing[0]

def inspect_xsa(path):
    # Validate the produced archive without extracting executable content.
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:raise RuntimeError('Corrupt XSA')
        names=archive.namelist()
        if not names or len(names)!=len(set(names)):raise RuntimeError('Empty or duplicate XSA entries')
        for name in names:
            rel=PurePosixPath(name)
            if rel.is_absolute() or '..' in rel.parts or '\\' in name:raise RuntimeError('Unsafe XSA entry')
        if not any(n.lower().endswith('.hwh') for n in names):raise RuntimeError('XSA lacks hardware metadata')
        if any(n.lower().endswith('.bit') for n in names):raise RuntimeError('Unexpected bitstream in pre-synthesis XSA')
        ps=[n for n in names if PurePosixPath(n).name=='ps7_init.c']
        if len(ps)!=1:raise RuntimeError('Expected one generated PS7 initialization source')
        return {'entries':names,'ps7_init_sha256':hashlib.sha256(archive.read(ps[0])).hexdigest(),
                'contains_bitstream':False,'sha256':sha(path)}
