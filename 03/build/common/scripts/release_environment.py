"""Resolve a user-installed tool without accepting a different compiler silently."""
from pathlib import Path
import hashlib, json, os, shutil
ROOT=Path(__file__).resolve().parents[1]
def tool(name):
    spec=json.loads((ROOT/'config/toolchain.json').read_text(encoding='utf-8'))[name]
    value=os.environ.get(spec['environment']) or shutil.which(spec['executable'])
    if not value:raise RuntimeError('Set '+spec['environment']+' to '+spec['executable']+' from the documented tool version')
    path=Path(value).resolve(strict=True)
    with path.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
    if actual!=spec['sha256']:raise RuntimeError('Tool identity differs from the qualified compiler: '+name)
    return path
