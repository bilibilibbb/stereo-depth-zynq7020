"""Record runtime/package identities and stream-hash supplied artifacts, read-only."""
from pathlib import Path
from datetime import datetime,timezone
from importlib.metadata import version,PackageNotFoundError
import argparse,hashlib,json,platform,sys

def manifest(files):
    records=[]
    for role,name in sorted(files.items()):
        p=Path(name)
        with p.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
        records.append({'role':role,'filename':p.name,'bytes':p.stat().st_size,'sha256':digest})
    packages={}
    for name in ('pynq','numpy'):
        try:packages[name]=version(name)
        except PackageNotFoundError:packages[name]=None
    return {'created_utc':datetime.now(timezone.utc).isoformat(),'python':sys.version.split()[0],
            'system':platform.system(),'machine':platform.machine(),'packages':packages,'files':records,
            'scope':'Identity record only; hardware execution, correctness and performance are not implied.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('bit','hwh','input','contract'):p.add_argument('--'+key,required=True)
    a=p.parse_args();print(json.dumps(manifest(vars(a)),indent=2))
