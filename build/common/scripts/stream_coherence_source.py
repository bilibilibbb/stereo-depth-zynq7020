"""Exact, reviewable derivation of the coherent PS-only stream controls."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT/'firmware/legacy/field_stream.c'
NEW = ROOT/'firmware/legacy/field_stream_coherent.c'


def expected():
    s = BASE.read_text()
    def replace(a, b, count=1):
        nonlocal s
        assert s.count(a) == count, (a, s.count(a), count)
        s = s.replace(a, b)
    replace('#define FS_MAGIC', '''/* Both A9s use the same normal-WB SHAREABLE translation table, SCU and
 * ACTLR.SMP/FW checked at platform join. These first two cache lines are
 * PS-only live control: acquire/release communicates them through coherence.
 * Invalidating either live line can discard the other core's dirty progress.
 * Keep maintenance on leased payload/DMA ranges and pre-publication init. */
static void fs_control_order(void){fs_cache()->barrier(fs_cache()->user);}
#define FS_MAGIC''')
    # The helper needs the existing fs_cache declaration above it.
    replace('static void fs_control_order(void){fs_cache()->barrier(fs_cache()->user);}\n', '')
    replace('static int fs_cache_valid', 'static void fs_control_order(void){fs_cache()->barrier(fs_cache()->user);}\nstatic int fs_cache_valid')
    replace('int fs_color_begin_v346(', 'static int fs_color_prepare_v346(')
    replace('fs_clean(s,s,(uint32_t)offsetof(fs_color,packet));return ss_lg_stream_publish(s,SS_LG_STREAM_COLOR);',
            'return RC_OK;')
    marker='int fs_color_logits_begin_v348('
    plain='''int fs_color_begin_v346(void *memory,size_t bytes,const double *cost,const float *guide,const double *weight,unsigned d,float *scratch,float *out,const field_cache_v282 *cache){
 int error=fs_color_prepare_v346(memory,bytes,cost,guide,weight,d,scratch,out,cache);if(error)return error;
 fs_clean(memory,memory,(uint32_t)offsetof(fs_color,packet));return ss_lg_stream_publish(memory,SS_LG_STREAM_COLOR);
}
'''
    replace(marker, plain+marker)
    replace('error=fs_color_begin_v346(memory,bytes,mean,guide,weight,d,scratch,out,cache);',
            'error=fs_color_prepare_v346(memory,bytes,mean,guide,weight,d,scratch,out,cache);')
    replace('s->mean_address=(uint64_t)(uintptr_t)mean;fs_clean(s,s,(uint32_t)offsetof(fs_color,packet));return RC_OK;',
            's->mean_address=(uint64_t)(uintptr_t)mean;fs_clean(s,s,(uint32_t)offsetof(fs_color,packet));return ss_lg_stream_publish(s,SS_LG_STREAM_COLOR);')
    # Remove only cache operations naming the two control records; not packet,
    # descriptor, coefficient, output or temporary buffer maintenance.
    s,n=re.subn(r'fs_(?:clean|invalidate)\(([sv]),&\1->(?:producer|consumer),(?:64|sizeof\(\1->(?:producer|consumer)\))\);', 'fs_control_order();', s)
    assert n==14,n
    s,n=re.subn(r'fs_cache\(\)->(?:clean|invalidate)\(fs_cache\(\)->user,&[sv]->(?:producer|consumer),64\);', '', s)
    assert n==11,n
    # Shared progress is observed with acquire, including the readiness API.
    for field in ('produced','cancel','error'):
        s=s.replace('v->producer.'+field, 'fq_load(&v->producer.'+field+')')
    s=s.replace('v->consumer.consumed', 'fq_load(&v->consumer.consumed)')
    s=s.replace('if(s->consumer.terminal)', 'if(fq_load(&s->consumer.terminal))')
    s=s.replace('if(s->producer.cancel)error=', 'if(fq_load(&s->producer.cancel))error=')
    s=s.replace('else if(s->producer.error)error=(int)s->producer.error;',
                'else if(fq_load(&s->producer.error))error=(int)fq_load(&s->producer.error);')
    s=s.replace('if(s->producer.error)return fs_color_terminal(s,(int)s->producer.error);',
                'if(fq_load(&s->producer.error))return fs_color_terminal(s,(int)fq_load(&s->producer.error));')
    s=s.replace('s->producer.error=RC_ARGUMENT;break;', 'fq_store(&s->producer.error,RC_ARGUMENT);break;')
    return s


def verify():
    assert NEW.read_text()==expected(), 'Unreviewed stream source change'


def test_source():
    s=(ROOT/'tests/legacy_stream.c').read_text()
    a=s.index('static void barrier(')
    b=s.index('static field_cache_v282 cache=')
    s=s[:a]+'''static ss_lg_stream_mailbox mailbox;
static unsigned expect_logits;
static unsigned forbidden_operations;
static void barrier(void *p){
    (void)p;
    /* Terminal is visible before the worker returns. Reuse must wait for
     * the separate return receipt, even with fully coherent controls. */
    if(core==1&&watch_terminal&&((const uint32_t *)(stream+64))[4]){
        int rc;core=0;watch_terminal=0;
        rc=kind==1?fs_color_take_v346(stream):fs_gray_take_v346(stream);
        CHECK(rc==RC_PENDING);CHECK(!ss_lg_stream_available());receipt_checks++;core=1;
    }
}
static void range_check(const void *p,uint32_t n){
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)stream;
    if(mailbox.state&&a<b+128&&b<a+n){
        forbidden_operations++;
#ifndef SS_ALLOW_UNSAFE_CONTROLS
        fprintf(stderr,"UNSAFE_LIVE_CONTROL_MAINTENANCE core=%u\\n",core);exit(41);
#endif
    }
}
static void invalidate(void *p,void *a,uint32_t n){(void)p;CHECK(a&&n);range_check(a,n);invalidate_count[core]++;}
static void clean(void *p,const void *a,uint32_t n){(void)p;CHECK(a&&n);range_check(a,n);clean_count[core]++;}
int __real_ss_lg_stream_publish(void *,unsigned);
int __wrap_ss_lg_stream_publish(void *address,unsigned type){
    const uint64_t *metadata=(const uint64_t *)((const unsigned char *)address+128);
    int rc;
    if(type==SS_LG_STREAM_COLOR&&expect_logits&&
       (metadata[4]!=(uint64_t)(uintptr_t)logits||metadata[2]!=(uint64_t)(uintptr_t)mean)){
        fprintf(stderr,"INCOMPLETE_DESCRIPTOR_AT_PUBLICATION\\n");exit(42);
    }
    rc=__real_ss_lg_stream_publish(address,type);
    if(!rc){core=1;CHECK(ss_lg_stream_worker_step()==RC_PENDING);core=0;}
    return rc;
}
'''+s[b:]
    s=s.replace('static ss_lg_stream_mailbox mailbox;\n    CHECK', 'CHECK')
    s=s.replace('int rc,done=0;kind=1;', 'int rc,done=0;kind=1;expect_logits=use_logits;')
    s=s.replace('CHECK(clean_count[0]&&clean_count[1]&&invalidate_count[0]&&invalidate_count[1]);',
                'CHECK(clean_count[0]&&clean_count[1]&&invalidate_count[1]);CHECK(!forbidden_operations);')
    return s


if __name__=='__main__':
    assert not NEW.exists(), 'Do not overwrite a bound source'
    NEW.write_text(expected(), encoding='utf-8')
    (ROOT/'tests/board_probe/coherent_stream_test.c').write_text(test_source(), encoding='utf-8')
