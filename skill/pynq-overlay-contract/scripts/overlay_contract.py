"""Validate exported Overlay.ip_dict metadata and bound bit/hwh artifacts.

Pure JSON/file inspection: no PYNQ import, download or register writes.
"""
import argparse
import hashlib
import json
from pathlib import Path


def number(value):
    if isinstance(value,bool):raise ValueError('Boolean is not an address')
    if isinstance(value,int):return value
    if isinstance(value,str):return int(value,16 if value.lower().startswith('0x') else 10)
    raise ValueError('Integer or decimal/hex string required')


def check_ip(ip_dict, contract):
    name=contract['ip_name']
    if name not in ip_dict:raise ValueError('Required IP absent: '+name)
    ip=ip_dict[name]
    if ip.get('type')!=contract['vlnv']:raise ValueError('IP VLNV/version mismatch')
    addr=number(ip['phys_addr']);size=number(ip['addr_range'])
    alignment=number(contract.get('address_alignment',4))
    required=number(contract.get('minimum_address_range',4))
    if addr<0 or alignment<=0 or addr%alignment or required<1 or size<required:
        raise ValueError('Invalid address, alignment or register span')
    params=ip.get('parameters',ip.get('config',{}))
    for key, expected in contract.get('parameters',{}).items():
        if key not in params or number(params[key])!=number(expected):
            raise ValueError('Hardware parameter mismatch: '+key)
    return {'ip_name':name,'vlnv':ip['type'],'phys_addr':addr,'addr_range':size,
            'scope':'Metadata contract only; inspect clocks, resets and data path for the target board.'}


def check_artifacts(bit,hwh,expected_hashes):
    bit,hwh=Path(bit),Path(hwh)
    if bit.suffix.lower()!='.bit' or hwh.suffix.lower()!='.hwh' or bit.stem!=hwh.stem:
        raise ValueError('Expected same-stem .bit and .hwh files')
    if not expected_hashes:raise ValueError('Supply hashes from the intended build receipt')
    result={}
    for role,path in (('bit',bit),('hwh',hwh)):
        with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
        if digest!=expected_hashes[role]:raise ValueError('Build hash mismatch: '+role)
        result[role]=digest
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('metadata_json');p.add_argument('contract_json');p.add_argument('--bit');p.add_argument('--hwh');p.add_argument('--hashes')
    a=p.parse_args()
    def read(path):
        return json.loads(Path(path).read_text(encoding='utf-8'))
    result=check_ip(read(a.metadata_json),read(a.contract_json))
    if any((a.bit,a.hwh,a.hashes)):
        if not all((a.bit,a.hwh,a.hashes)):p.error('--bit, --hwh and --hashes must be supplied together')
        result['artifacts']=check_artifacts(a.bit,a.hwh,read(a.hashes))
    print(json.dumps(result,indent=2))
