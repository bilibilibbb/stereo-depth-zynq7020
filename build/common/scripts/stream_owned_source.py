"""Current mailbox lease, not retained DDR bytes, identifies a live stream."""
from pathlib import Path
import stream_coherence_source as coherent

ROOT=coherent.ROOT
BASE=coherent.NEW
NEW=ROOT/'firmware/legacy/field_stream_owned.c'

def expected():
    coherent.verify()
    s=BASE.read_text()
    old=' if(s->producer.magic==FS_MAGIC && fq_load(&s->producer.active))return RC_AGAIN;'
    assert s.count(old)==2
    return s.replace(old,''' /* ss_lg_stream_available() above is the live lease authority. The arena
  * may retain FS_MAGIC/active from a prior boot or another retired use.
  * A live current stream still prevents admission through its mailbox,
  * including terminal-before-return. Never infer a lease from stale DDR. */''')

def test_source():
    s=coherent.test_source()
    a='''    CHECK(!(use_logits?fs_color_logits_begin_v348(stream,sizeof(stream),logits,mean,guide,weight,disparity,scratch,out,&cache):
           fs_color_begin_v346(stream,sizeof(stream),cost,guide,weight,disparity,scratch,out,&cache)));'''
    b='''    CHECK(ss_lg_stream_available());
    {const uint32_t stale[2]={0x46533346u,1};memcpy(stream,stale,sizeof(stale));}
    rc=use_logits?fs_color_logits_begin_v348(stream,sizeof(stream),logits,mean,guide,weight,disparity,scratch,out,&cache):
           fs_color_begin_v346(stream,sizeof(stream),cost,guide,weight,disparity,scratch,out,&cache);
    if(rc){fprintf(stderr,"STALE_STREAM_HEADER_REJECTED\\n");exit(43);}
    {unsigned char before[192];uint32_t epoch=mailbox.epoch;memcpy(before,stream,sizeof(before));
     CHECK(fs_color_begin_v346(stream,sizeof(stream),cost,guide,weight,disparity,scratch,out,&cache)==RC_AGAIN);
     CHECK(!memcmp(before,stream,sizeof(before))&&epoch==mailbox.epoch&&!ss_lg_stream_available());}'''
    assert s.count(a)==1;s=s.replace(a,b)
    a='    CHECK(!fs_gray_begin_v346(stream,sizeof(stream),coeff,q40,gray_guide,median,gray_out,&cache));watch_terminal=1;'
    b='''    CHECK(ss_lg_stream_available());
    {const uint32_t stale[2]={0x46533346u,1};memcpy(stream,stale,sizeof(stale));}
    rc=fs_gray_begin_v346(stream,sizeof(stream),coeff,q40,gray_guide,median,gray_out,&cache);
    if(rc){fprintf(stderr,"STALE_STREAM_HEADER_REJECTED\\n");exit(43);}
    {unsigned char before[192];uint32_t epoch=mailbox.epoch;memcpy(before,stream,sizeof(before));
     CHECK(fs_gray_begin_v346(stream,sizeof(stream),coeff,q40,gray_guide,median,gray_out,&cache)==RC_AGAIN);
     CHECK(!memcmp(before,stream,sizeof(before))&&epoch==mailbox.epoch&&!ss_lg_stream_available());}
    watch_terminal=1;'''
    assert s.count(a)==1;s=s.replace(a,b)
    return s

def verify():
    assert NEW.read_text()==expected()

if __name__=='__main__':
    assert not NEW.exists()
    NEW.write_text(expected(),encoding='utf-8')
    (ROOT/'tests/board_probe/owned_stream_test.c').write_text(test_source(),encoding='utf-8')
