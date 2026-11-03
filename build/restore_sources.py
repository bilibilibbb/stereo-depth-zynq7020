"""Reconstruct the two frozen, case-sensitive Vivado source trees from this release."""
from pathlib import Path
import hashlib
import json
import shutil
import sys

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    if len(sys.argv)!=2:
        raise SystemExit('Usage: python build/restore_sources.py DESTINATION')
    root=Path(__file__).resolve().parents[1]
    destination=Path(sys.argv[1]).resolve()
    if destination==root or destination.is_relative_to(root):
        raise SystemExit('Choose a destination outside the release directory.')
    entries=json.loads((root/'build/source_paths.json').read_text(encoding='utf8'))
    for item in entries:
        source=(root/item['source']).resolve()
        target=(destination/item['original']).resolve()
        if not source.is_relative_to(root) or not target.is_relative_to(destination):
            raise ValueError('Unsafe entry in source map.')
        if digest(source)!=item['sha256']:
            raise ValueError('Source checksum mismatch: '+item['source'])
        if target.exists() and digest(target)!=item['sha256']:
            raise FileExistsError('Refusing to overwrite a modified file: '+str(target))
    for item in entries:
        target=destination/item['original']
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(root/item['source'],target)
    print('Restored and checked',len(entries),'source-release files in',destination)

if __name__=='__main__':
    main()
