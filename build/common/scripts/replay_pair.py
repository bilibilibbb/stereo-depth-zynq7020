"""Replay an already captured board result; no inference or network operation."""
from pathlib import Path
import hashlib,json,os,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'common'),str(ROOT/'desktop_app')]
from snapshot_wire import decode_packet,frozen_source
import render
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    assert os.environ.get('SNAP_RELEASE_GUARDED')=='pair-replay','Use run.py pair-replay'
    out=Path(os.environ['SNAP_BUILD_DIR']).resolve(strict=True)
    assert out.is_relative_to(ROOT/'build')
    ref=json.loads((ROOT/'data/image_provenance.json').read_text(encoding='utf-8'))
    packet=ROOT/'data/accepted_pair.sdp'
    assert sha(packet)==ref['source_sha256'] and sha(ROOT/'desktop_app/render.py')==ref['renderer_sha256']
    raw=packet.read_bytes();h=decode_packet(raw);source=frozen_source(h,raw)
    assert h.__dict__==ref['header'] and source==ref['source']
    assets={}
    for name,im in zip(('accepted_photo.png','accepted_sgm.png','accepted_model.png'),render.pair(h,raw,shared=True)):
        p=out/name;im.save(p);im.close();assets[name]=sha(p)
        assert assets[name]==ref['assets'][name],name
    result={'status':'PASS','scope':'Saved board packet decoded with unchanged production renderer; not a new live camera capture or inference.',
            'source_frame':source['frame'],'model_id':h.model_id,'images':assets,'new_board_operations':0,'network_changes':0}
    (out/'pair_replay.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))
if __name__=='__main__':main()
