"""Check the saved board packet using the actual protocol parser; no inference."""
from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'src/desktop/protocol'))
from snapshot_wire import decode_packet,frozen_source
record=json.loads((root/'data/sample.json').read_text(encoding='utf8'))
packet=(root/'data/sample.sdp').read_bytes()
if hashlib.sha256(packet).hexdigest()!=record['source_sha256']:
    raise ValueError('Board sample checksum mismatch')
header=decode_packet(packet)
if header.__dict__!=record['header'] or frozen_source(header,packet)!=record['source']:
    raise ValueError('Board sample identity mismatch')
if header.model_id!=11459 or (header.width,header.height)!=(320,240):
    raise ValueError('Unexpected model or image dimensions')
print('PASS: saved board sample SHA-256, protocol CRC, dimensions and frame identity.')
