#include "nn_snapshot_profile.h"
#include <string.h>
#include <limits.h>
_Static_assert(__atomic_always_lock_free(4,0),"A9 MMIO ownership requires lock-free32-bit atomics");
enum {FREE=0u,STARTING=1u,OWNED=2u,OPERATING=3u,RESERVED=4u};
static uint32_t rd(fw_nn_mmio *m,uint32_t a){return m->io.read32(m->io.user,a);}
static void wr(fw_nn_mmio *m,uint32_t a,uint32_t v){m->io.write32(m->io.user,a,v);}
static void order(fw_nn_mmio *m){m->io.barrier(m->io.user);}
static uint32_t status_reg(unsigned u){return u==FW_NN_CORE?FW_NN_REG_CORE_STATUS:FW_NN_REG_DMA_STATUS;}
static uint32_t command_reg(unsigned u){return u==FW_NN_CORE?FW_NN_REG_CORE_COMMAND:FW_NN_REG_DMA_COMMAND;}
static int valid(const fw_nn_mmio *m){return m && m->magic==FW_NN_MMIO_ID;}
static int public_blocked(const fw_nn_mmio *m){return valid(m) && __atomic_load_n(&m->reservation,__ATOMIC_ACQUIRE)!=0u;}
#ifdef R14_HOST_REFERENCE
static uint64_t descriptor_work[4];
void fw_nn_mmio_descriptor_stats(uint64_t *out){if(out)memcpy(out,descriptor_work,sizeof(descriptor_work));}
#define ND_WORK(index,n) (descriptor_work[index]+=(n))
#else
#define ND_WORK(index,n) ((void)0)
#endif
static void unlock(fw_nn_mmio *m,unsigned u){__atomic_store_n(m->state+u,OWNED,__ATOMIC_RELEASE);}
static int lock(fw_nn_mmio *m,unsigned u,uint32_t ticket){
 uint32_t expected=OWNED;
 if(!valid(m) || u>FW_NN_DMA || !ticket)return FW_ARGUMENT;
 if(!__atomic_compare_exchange_n(m->state+u,&expected,OPERATING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return expected==FREE?FW_STALE:FW_BUSY;
 if(m->ticket[u]!=ticket){unlock(m,u);return FW_STALE;}return FW_OK;
}
static int owner_valid(const uint32_t *w){return (w[0]|w[1]) && w[2] && w[3] && w[4];}
static int residual_valid(const uint32_t *w){
 uint32_t pixels=w[6]&31u,outputs=(w[6]>>5)&31u,n=pixels*(w[23]+1u),width=w[10],pitch=w[11],column=w[12];
 uint32_t first=w[8],last=w[8],end,result=w[16]&16383u,result_pitch=w[16]>>14,result_end;
 if((w[5]&(FW_NN_DEPTHWISE|FW_NN_STEM|FW_NN_PW_LINK)) ||
    (w[5]&(FW_NN_PW_RAW|FW_NN_RESULT_STORE))!=(FW_NN_PW_RAW|FW_NN_RESULT_STORE) || ((w[7]>>4)&15u)!=12u ||
    !pixels || pixels>16u || !outputs || outputs>16u || w[8]>4095u || w[9]>4095u || !width || width>320u || pitch<width || pitch>511u || column>=width || n+column>2u*width)return 0;
 if(n+column>width){if(w[9]<first)first=w[9];if(w[9]>last)last=w[9];}
 end=last+((outputs-1u)/4u)*pitch+width;
 if(w[5]&FW_NN_RESULT_GROUP4){
  uint32_t source=w[13]&16383u,source_pitch=w[13]>>14,channels=(w[6]>>10)&127u,source_end;
  result_end=result+(((outputs-1u)/4u)*result_pitch+n)*4u;
  if((result&3u) || !channels || channels>64u || source_pitch<n)return 0;
  source_end=source+(channels-1u)*source_pitch+n;
  if(source_end>16384u || (source/4u<(result_end+3u)/4u && result/4u<(source_end+3u)/4u))return 0;
 }else result_end=result+(outputs-1u)*result_pitch+n;
 return end<=4096u && result_pitch>=n && result_end<=16384u && !(first<(result_end+3u)/4u && result/4u<end);
}
static int dw_batch_valid(const uint32_t *w){
 unsigned recipe=w[7]>>18,groups=(w[7]>>8)&7u,terms=(w[7]>>11)&127u,tiles=(w[23]&31u)+1u,tail=w[23]>>5,cols=2u*(terms*(tiles-1u)+(tail?tail:terms));
 unsigned width=(w[21]>>11)&511u,pitch=w[12]&511u,step=w[21]>>20,i,j,rows=recipe==0u?4u:3u;
 uint32_t first=4096u,last=0u,dest_first=16384u,dest_end=0u,ends[4];int origin=(int)(w[21]&2047u);if(origin&1024)origin-=2048;
 if(w[5]!=(1u|FW_NN_DEPTHWISE|FW_NN_DW_QUAD|FW_NN_DW_STORE) || recipe>2u ||
    !groups || groups>4u || !terms || terms>(recipe==0u?16u:32u) || (tail && (tiles==1u || tail>=terms)) || !width || width>320u ||
    cols>511u || (recipe==0u && pitch<cols))return 0;
 if(recipe==1u){
  if((step!=2u && step!=4u) || tiles!=step || (w[12]>>9)!=step || w[22]!=2u*step || tail || origin+(int)tiles-1>1023)return 0;
 }else if((w[12]>>9)!=1u || step!=1u || w[22]!=(recipe==0u?2u:4u) || origin+(int)((tiles-1u)*terms*w[22])>1023)return 0;
 for(i=0;i<rows;i++)if(w[17u+i]&(1u<<16)){
  uint32_t base=w[17u+i]&4095u,end=base+((width+15u)&~15u);
  if((base&15u) || end>4096u)return 0;
  if(base<first)first=base;
  if(end>last)last=end;
 }
 if(!last)first=0u;
 for(i=0;i<groups;i++){
  uint32_t base=w[8u+i];ends[i]=base+cols+(recipe==0u?pitch:0u);
  if(ends[i]>16384u)return 0;
  if(base<dest_first)dest_first=base;
  if(ends[i]>dest_end)dest_end=ends[i];
  for(j=0;j<i;j++)if(base<ends[j] && w[8u+j]<ends[i])return 0;
 }
 if(recipe==1u){
  /* Native SIX rows can straddle the reserved scratch hole. Check each
   * real row, while the running lease still protects their full envelope. */
  for(i=0;i<rows;i++)if(w[17u+i]&(1u<<16)){
   uint32_t base=w[17u+i]&4095u,end=base+((width+15u)&~15u);
   if(dest_first/4u<end && base<(dest_end+3u)/4u)return 0;
  }
  return 1;
 }
 return !(dest_first/4u<last && first<(dest_end+3u)/4u);
}
static int core_valid(const uint32_t *w){
 unsigned i;uint32_t f=w[5];
 if(!owner_valid(w) || (f&~32767u) || (w[6]>>29) || (w[7]>>20) || (w[12]>>12) || (w[13]>>23) || (w[16]>>23) || (w[21]>>23) || (w[22]>>10) || (w[23]>>12))return 0;
 if((w[23]>>5) && (!(f&FW_NN_DEPTHWISE) || !(w[23]&31u)))return 0;
 if((f&FW_NN_RESIDUAL) && !residual_valid(w))return 0;
 if((f&FW_NN_PW_BANK23) && ((f&3u)!=1u || (f&(FW_NN_DEPTHWISE|FW_NN_STEM))))return 0;
 if((f&FW_NN_RESULT_GROUP4) && (!(f&(FW_NN_STEM|FW_NN_PW_RAW)) || !(f&FW_NN_RESULT_STORE) || (f&FW_NN_PW_LINK)))return 0;
 if(w[23] && (((f&FW_NN_DEPTHWISE) && !dw_batch_valid(w)) || ((f&FW_NN_STEM) && !(f&FW_NN_RESULT_GROUP4))))return 0;
 for(i=8;i<12;i++)if(w[i]>>14)return 0;
 for(i=17;i<21;i++)if(w[i]>>17)return 0;
 if(f&FW_NN_DEPTHWISE)return (f&FW_NN_DW_QUAD) && (f&FW_NN_DW_STORE) && !(f&(FW_NN_STEM|FW_NN_PW_LINK|FW_NN_PW_RAW|FW_NN_RESULT_STORE));
 if(!(f&FW_NN_RESULT_STORE) || (f&(FW_NN_DW_PAIR|FW_NN_DW_QUAD|FW_NN_DW_PACK|FW_NN_DW_STORE)))return 0;
 return f&FW_NN_STEM?!(f&(FW_NN_PW_RAW|FW_NN_PW_LINK)):(f&FW_NN_PW_RAW)!=0;
}
static int dma_valid(const uint32_t *w){
 if(!owner_valid(w) || (w[5]>>2) || (w[7]>>13) || (w[8]>>12) || (w[11]>>8))return 0;
 if(w[11]&FW_NN_DMA_PACK_I16)return w[5]==FW_NN_DMA_RAW_LOAD && !(w[11]&~131u) && !(w[9]>>17);
 if(w[11] && w[5]==FW_NN_DMA_WEIGHT)return w[11]<=3u && !(w[9]>>8) && (w[9]&3u)+w[11]<4u;
 return w[11]?w[5]<2u && !(w[9]>>13):!(w[9]>>8) && !(w[10]>>5);
}
static int start_expected(fw_nn_mmio *m,unsigned u,const uint32_t *w,uint32_t *ticket,uint32_t idle_state){
 uint32_t expected=idle_state,base=status_reg(u),n=u==FW_NN_CORE?24u:12u;unsigned i,reuse;
 if(!valid(m) || !w || !ticket || !(u==FW_NN_CORE?core_valid(w):dma_valid(w)))return FW_ARGUMENT;
 if(!__atomic_compare_exchange_n(m->state+u,&expected,STARTING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return FW_BUSY;
 if((idle_state==FREE && public_blocked(m)) || (idle_state==RESERVED && !public_blocked(m))){__atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_BUSY;}
 if(rd(m,base)!=0 || rd(m,FW_NN_REG_POISON)){
  __atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_BUSY;
 }
 if(m->next_ticket[u]==UINT32_MAX){__atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_STALE;}
 reuse=m->descriptor_reuse;
 if(reuse){
  uint32_t c,d;expected=0;
  if(!__atomic_compare_exchange_n(&m->descriptor_start,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){__atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_BUSY;}
  /* Check both units: reset after initial DMA must stop a first-ever CORE.
   * The issue gate keeps peer next_ticket stable even with two A9 callers. */
  c=rd(m,FW_NN_REG_CORE_TICKET);d=rd(m,FW_NN_REG_DMA_TICKET);ND_WORK(2,2u);
  if(c!=m->next_ticket[0] || d!=m->next_ticket[1]){
   m->descriptor_valid[u]=0u;ND_WORK(3,1u);__atomic_store_n(&m->descriptor_start,0u,__ATOMIC_RELEASE);
   __atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_STALE;
  }
 }
 m->ticket[u]=++m->next_ticket[u];m->last_completed[u]=0;m->cancel_sent[u]=0;
 if(reuse){
  uint32_t *payload=u==FW_NN_CORE?m->core_payload:m->dma_payload;unsigned known=m->descriptor_valid[u];
  for(i=0;i<n;i++){
   uint32_t *prior=i<5u?m->owner[u]+i:payload+i-5u;
   if(known)ND_WORK(0,1u);
   if(!known || *prior!=w[i]){wr(m,command_reg(u)+4u*i,w[i]);if(i>=5u){*prior=w[i];ND_WORK(1,1u);}}
   if(i<5u)*prior=w[i];
  }
  m->descriptor_valid[u]=1u;
 }else{
  for(i=0;i<5;i++)m->owner[u][i]=w[i];
  for(i=0;i<n;i++)wr(m,command_reg(u)+4u*i,w[i]);
 }
 wr(m,base+12u,m->ticket[u]);order(m);wr(m,base+16u,FW_NN_HW_START);order(m);
 if(reuse)__atomic_store_n(&m->descriptor_start,0u,__ATOMIC_RELEASE);
 *ticket=m->ticket[u];__atomic_store_n(m->state+u,OWNED,__ATOMIC_RELEASE);return FW_OK;
}
int fw_nn_mmio_start_core(fw_nn_mmio *m,const fw_nn_core_command *c,uint32_t *ticket){return start_expected(m,FW_NN_CORE,c?c->words:NULL,ticket,FREE);}
int fw_nn_mmio_start_dma(fw_nn_mmio *m,const fw_nn_dma_command *c,uint32_t *ticket){return start_expected(m,FW_NN_DMA,c?c->words:NULL,ticket,FREE);}
static int snapshot(fw_nn_mmio *m,unsigned u,fw_nn_progress *p){
 uint32_t base=status_reg(u),identity=rd(m,base+4u),status=rd(m,base),fault=rd(m,base+8u),poison=rd(m,FW_NN_REG_POISON),terms=0,reads=0,zeros=0,count=0,tiles=0;unsigned i;
 if(identity!=m->ticket[u])return FW_STALE;
 if(u==FW_NN_DMA)count=rd(m,FW_NN_REG_DMA_COMPLETED);
 else{terms=rd(m,FW_NN_REG_TERMS);reads=rd(m,FW_NN_REG_READS);zeros=rd(m,FW_NN_REG_ZEROS);tiles=rd(m,FW_NN_REG_TILES);}
 for(i=0;i<5;i++)if(rd(m,command_reg(u)+4u*i)!=m->owner[u][i])return FW_STALE;
 order(m);
 if(rd(m,base+4u)!=identity)return FW_STALE;
 if(rd(m,base)!=status)return FW_PENDING;
 if((status&~15u) || (((status&FW_NN_HW_BUSY)!=0u)==((status&FW_NN_HW_DRAINED)!=0u)) ||
    ((status&(FW_NN_HW_CANCELLED|FW_NN_HW_FAULT)) && !(status&FW_NN_HW_DRAINED)) || poison>1u ||
    ((status&FW_NN_HW_FAULT)!=0u && !fault) || (fault && !(status&FW_NN_HW_FAULT) && !(u==FW_NN_DMA && poison && status==FW_NN_HW_BUSY)) ||
    count>4096u || count<m->last_completed[u] || (terms>>16)>(terms&65535u) || zeros>65535u)return FW_PACKET;
 if(u==FW_NN_CORE && (!(tiles>>16) || (tiles>>16)>32u || (tiles&65535u)>(tiles>>16) ||
    (status==FW_NN_HW_DRAINED && (tiles&65535u)!=(tiles>>16))))return FW_PACKET;
 m->last_completed[u]=count;
 *p=(fw_nn_progress){(status&FW_NN_HW_DRAINED)!=0u,(status&FW_NN_HW_CANCELLED)!=0u,fault,poison,count,terms&65535u,terms>>16,reads&65535u,reads>>16,zeros,tiles&65535u,tiles>>16};return FW_OK;
}
static void snapshot_history(fw_nn_mmio *m,unsigned u,int rc,const fw_nn_progress *p){
 /* A status transition returns no receipt and authorizes no ACK. It does
  * not itself change the descriptor. The next snapshot/issue still checks
  * ticket history; faults, cancellation and invalid receipts invalidate. */
 if(rc?(rc!=FW_PENDING || !FW_NN_DMA_RECEIPT_TRANSACTION):(p->fault || p->cancelled))m->descriptor_valid[u]=0u;
}
static int poll_any(fw_nn_mmio *m,unsigned u,uint32_t ticket,fw_nn_progress *out){
 int rc;if(!out)return FW_ARGUMENT;rc=lock(m,u,ticket);if(rc)return rc;rc=snapshot(m,u,out);snapshot_history(m,u,rc,out);unlock(m,u);return rc;
}
static int cancel_any(fw_nn_mmio *m,unsigned u,uint32_t ticket){
 uint32_t base,status;unsigned i;int rc=lock(m,u,ticket);if(rc)return rc;base=status_reg(u);
 m->descriptor_valid[u]=0u;
 if(rd(m,base+4u)!=ticket){unlock(m,u);return FW_STALE;}status=rd(m,base);
 for(i=0;i<5;i++)if(rd(m,command_reg(u)+4u*i)!=m->owner[u][i]){unlock(m,u);return FW_STALE;}
 if(rd(m,base+4u)!=ticket){unlock(m,u);return FW_STALE;}
 if(status==FW_NN_HW_BUSY){if(!m->cancel_sent[u]){wr(m,base+16u,FW_NN_HW_CANCEL);order(m);m->cancel_sent[u]=1u;}rc=FW_OK;}
 else rc=(status&FW_NN_HW_DRAINED) && !(status&~14u)?FW_OK:FW_PACKET;
 unlock(m,u);return rc;
}
static int ack_to(fw_nn_mmio *m,unsigned u,uint32_t ticket,uint32_t idle_state){
 fw_nn_progress p;int rc=lock(m,u,ticket);if(rc)return rc;rc=snapshot(m,u,&p);
 snapshot_history(m,u,rc,&p);
 if(rc || !p.drained){unlock(m,u);return rc?rc:FW_PENDING;}
 wr(m,status_reg(u)+16u,FW_NN_HW_ACK);order(m);__atomic_store_n(m->state+u,idle_state,__ATOMIC_RELEASE);return FW_OK;
}
static int summary_any(fw_nn_mmio *m,uint32_t ticket,unsigned depthwise,uint32_t *out,unsigned words){
 /* ABI15 does not export hardware summary bitmaps. Never issue a read of
  * the rejected AXI ranges or return a successful all-zero substitute. */
 (void)m;(void)ticket;(void)depthwise;(void)out;(void)words;
 return FW_ARGUMENT;
}
int fw_nn_mmio_poll(fw_nn_mmio *m,unsigned u,uint32_t ticket,fw_nn_progress *out){return public_blocked(m)?FW_BUSY:poll_any(m,u,ticket,out);}
int fw_nn_mmio_cancel(fw_nn_mmio *m,unsigned u,uint32_t ticket){return public_blocked(m)?FW_BUSY:cancel_any(m,u,ticket);}
int fw_nn_mmio_ack(fw_nn_mmio *m,unsigned u,uint32_t ticket){return public_blocked(m)?FW_BUSY:ack_to(m,u,ticket,FREE);}
int fw_nn_mmio_summary(fw_nn_mmio *m,uint32_t ticket,unsigned depthwise,uint32_t *out,unsigned words){return public_blocked(m)?FW_BUSY:summary_any(m,ticket,depthwise,out,words);}
int fw_nn_mmio_map(fw_nn_mmio *m,const void *p,uint32_t bytes,uint32_t *address){
 int rc;if(!valid(m) || !m->io.map || !p || !bytes || (bytes&7u) || bytes>32768u || !address)return FW_ARGUMENT;
 rc=m->io.map(m->io.user,p,bytes,address);if(rc)return rc;
 return ((*address&7u) || bytes>UINT32_MAX-*address)?FW_ARGUMENT:FW_OK;
}
int fw_nn_mmio_map_rows(fw_nn_mmio *m,const void *p,uint32_t bytes,uint32_t rows,uint32_t pitch,uint32_t *address){
 uintptr_t at=(uintptr_t)p;uint32_t first=0,physical,i;int rc;
 if(!p || !address || !bytes || (bytes&7u) || !rows || rows>128u || bytes>32768u/rows || pitch<bytes || (pitch&7u))return FW_ARGUMENT;
 for(i=0;i<rows;i++){
  if(bytes>UINTPTR_MAX-at)return FW_ARGUMENT;
  rc=fw_nn_mmio_map(m,(const void *)at,bytes,&physical);if(rc)return rc;
  if(!i)first=physical;else if(physical!=first+(uint64_t)i*pitch)return FW_ARGUMENT;
  if(i+1u<rows){if(pitch>UINTPTR_MAX-at)return FW_ARGUMENT;at+=pitch;}
 }
 *address=first;return FW_OK;
}
int fw_nn_mmio_set_pack_i16(fw_nn_mmio *m,fw_nn_dma_command *c,const int16_t *data,uint32_t pixels,uint32_t planes,uint32_t pitch,uint32_t raw_base,uint32_t first_lane,uint32_t skew){
 uintptr_t at=(uintptr_t)data;uint32_t offset=(uint32_t)(at&7u)/2u,words,physical;int rc;
 if(!c || !data || (at&1u) || !pixels || pixels>511u || !planes || planes>4u || first_lane>3u || planes>4u-first_lane || raw_base>4095u || pixels>4096u-raw_base || skew>15u)return FW_ARGUMENT;
 words=(offset+pixels+3u)/4u;if(planes==1u && !pitch)pitch=words*8u;
 rc=fw_nn_mmio_map_rows(m,(const void *)(at&~(uintptr_t)7u),words*8u,planes,pitch,&physical);if(rc)return rc;
 c->words[5]=FW_NN_DMA_RAW_LOAD;c->words[6]=physical;c->words[7]=words;c->words[8]=raw_base;
 c->words[9]=pixels|(first_lane<<9)|(skew<<11)|(offset<<15);c->words[10]=pitch;c->words[11]=FW_NN_DMA_PACK_I16|(planes-1u);return FW_OK;
}
int fw_nn_mmio_init(fw_nn_mmio *m,const fw_nn_mmio_io *io){
 uint32_t tickets[2];fw_nn_mmio_io saved;
 if(!m || !io || !io->read32 || !io->write32 || !io->barrier)return FW_ARGUMENT;
 if(m->magic)return FW_BUSY;
 saved=*io;
 if(io->read32(io->user,FW_NN_REG_ID)!=FW_NN_MMIO_ID || io->read32(io->user,FW_NN_REG_ABI)!=FW_NN_MMIO_ABI || (io->read32(io->user,FW_NN_REG_CAPS)&FW_NN_MMIO_CAPS)!=FW_NN_MMIO_CAPS)return FW_ARGUMENT;
 if(io->read32(io->user,FW_NN_REG_CORE_STATUS) || io->read32(io->user,FW_NN_REG_DMA_STATUS) || io->read32(io->user,FW_NN_REG_POISON))return FW_BUSY;
 tickets[0]=io->read32(io->user,FW_NN_REG_CORE_TICKET);tickets[1]=io->read32(io->user,FW_NN_REG_DMA_TICKET);io->barrier(io->user);
 if(io->read32(io->user,FW_NN_REG_CORE_STATUS) || io->read32(io->user,FW_NN_REG_DMA_STATUS) || io->read32(io->user,FW_NN_REG_POISON) ||
    io->read32(io->user,FW_NN_REG_CORE_TICKET)!=tickets[0] || io->read32(io->user,FW_NN_REG_DMA_TICKET)!=tickets[1])return FW_BUSY;
 memset(m,0,sizeof(*m));m->io=saved;m->next_ticket[0]=tickets[0];m->next_ticket[1]=tickets[1];m->magic=FW_NN_MMIO_ID;return FW_OK;
}
int fw_nn_mmio_descriptor_reuse(fw_nn_mmio *m,unsigned enabled){
 uint32_t expected=0;unsigned acquired=0;int rc=FW_BUSY;
 if(!valid(m) || enabled>1u)return FW_ARGUMENT;
 if(!__atomic_compare_exchange_n(&m->reservation,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return FW_BUSY;
 for(acquired=0;acquired<2u;acquired++){expected=FREE;if(!__atomic_compare_exchange_n(m->state+acquired,&expected,RESERVED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;}
 if(acquired!=2u || rd(m,FW_NN_REG_CORE_STATUS) || rd(m,FW_NN_REG_DMA_STATUS) || rd(m,FW_NN_REG_POISON))goto done;
 m->descriptor_valid[0]=m->descriptor_valid[1]=0u;
 if(rd(m,FW_NN_REG_CORE_TICKET)!=m->next_ticket[0] || rd(m,FW_NN_REG_DMA_TICKET)!=m->next_ticket[1]){rc=FW_STALE;goto done;}
 order(m);
 if(rd(m,FW_NN_REG_CORE_STATUS) || rd(m,FW_NN_REG_DMA_STATUS) || rd(m,FW_NN_REG_POISON))goto done;
 if(rd(m,FW_NN_REG_CORE_TICKET)!=m->next_ticket[0] || rd(m,FW_NN_REG_DMA_TICKET)!=m->next_ticket[1]){rc=FW_STALE;goto done;}
 m->descriptor_reuse=enabled;rc=FW_OK;
done:
 while(acquired)__atomic_store_n(m->state+(--acquired),FREE,__ATOMIC_RELEASE);
 __atomic_store_n(&m->reservation,0u,__ATOMIC_RELEASE);return rc;
}
int fw_nn_encoder_place(fw_nn_core_command *c,unsigned layer){
 uint32_t f,shape,base,pixels,outputs,channels;
 if(!c || layer>FW_NN_ENCODER_PW2 || !core_valid(c->words))return FW_ARGUMENT;
 f=c->words[5];shape=c->words[6];pixels=shape&31u;outputs=(shape>>5)&31u;channels=(shape>>10)&127u;
 if(layer==FW_NN_ENCODER_STEM){
  if(!(f&FW_NN_STEM) || (f&3u) || pixels!=8u || outputs!=12u || channels!=27u)return FW_ARGUMENT;
  base=FW_NN_ENCODER_STEM_BASE;
 }else if(layer==FW_NN_ENCODER_DW1){
  if(!(f&FW_NN_DEPTHWISE) || !(f&FW_NN_DW_QUAD) || ((c->words[7]>>8)&7u)!=4u || (c->words[7]>>18)!=0u)return FW_ARGUMENT;
  base=FW_NN_ENCODER_DW_BASE;
 }else{
  if((f&(FW_NN_STEM|FW_NN_DEPTHWISE|FW_NN_PW_LINK|3u)) || !pixels || pixels>8u || outputs!=16u || channels!=12u)return FW_ARGUMENT;
  base=FW_NN_ENCODER_PW_BASE;
 }
 c->words[6]=(shape&~(63u<<17))|(base<<17);return FW_OK;
}
int fw_nn_encoder_weight_command(fw_nn_mmio *m,fw_nn_dma_command *c,const uint64_t *data,unsigned layer,unsigned bank){
 uint32_t words,base,physical;int rc;
 if(!c || layer>FW_NN_ENCODER_PW2 || bank>3u || (layer==FW_NN_ENCODER_STEM && bank>2u))return FW_ARGUMENT;
 words=layer==FW_NN_ENCODER_STEM?27u:layer==FW_NN_ENCODER_DW1?16u:12u;
 base=layer==FW_NN_ENCODER_STEM?FW_NN_ENCODER_STEM_BASE:layer==FW_NN_ENCODER_DW1?FW_NN_ENCODER_DW_BASE:FW_NN_ENCODER_PW_BASE;
 rc=fw_nn_mmio_map(m,data,words*8u,&physical);if(rc)return rc;
 c->words[5]=FW_NN_DMA_WEIGHT;c->words[6]=physical;c->words[7]=words;c->words[8]=0;c->words[9]=(base<<2)|bank;c->words[10]=0;c->words[11]=0;return FW_OK;
}
#include "fw_nn_block_runtime.inc"
