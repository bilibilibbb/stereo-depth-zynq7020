/* Host-only checks of the private snapshot operation against original
 * memmove, including overlap in both directions and in-place use. */
#include <stddef.h>
int fw_nn_plan_snapshot_checks(uint64_t proof[8]){
 fw_nn_block_plan storage[3],source;uint8_t expected[sizeof(storage)];unsigned trial,i,bytes;
 if(!proof)return FW_ARGUMENT;
 memset(proof,0,8u*sizeof(*proof));
 for(trial=0;trial<6u;trial++){
  uint8_t *raw=(uint8_t*)storage;unsigned src=trial==3u?4u:0u,dst=trial<3u?sizeof(source):trial==3u?0u:trial==4u?4u:0u;
  fw_nn_block_plan *from=(fw_nn_block_plan*)(raw+src),*to=(fw_nn_block_plan*)(raw+dst);
  memset(storage,0x6b,sizeof(storage));memset(&source,0xa5,sizeof(source));
  for(i=0;i<24u;i++)source.core.words[i]=100u+i;
  source.prepare_count=trial==0u?0u:trial==1u?2u:9u;source.export_count=trial==0u?0u:trial==1u?1u:9u;source.dma_only=0u;
  memcpy(from,&source,sizeof(source));memcpy(expected,storage,sizeof(storage));
  if(trial>=3u || !FW_NN_COMPACT_PLAN)memmove(expected+dst,expected+src,sizeof(source));
  else{
   memcpy(expected+dst,expected+src,sizeof(source.core));
   memcpy(expected+dst+offsetof(fw_nn_block_plan,prepare),expected+src+offsetof(fw_nn_block_plan,prepare),source.prepare_count*sizeof(*source.prepare));
   memcpy(expected+dst+offsetof(fw_nn_block_plan,export),expected+src+offsetof(fw_nn_block_plan,export),source.export_count*sizeof(*source.export));
   memcpy(expected+dst+offsetof(fw_nn_block_plan,prepare_count),expected+src+offsetof(fw_nn_block_plan,prepare_count),3u*sizeof(uint32_t));
  }
  block_plan_snapshot(to,from);
  if(memcmp(storage,expected,sizeof(storage)))return 100+(int)trial;
  bytes=sizeof(source.core)+3u*sizeof(uint32_t)+(source.prepare_count+source.export_count)*sizeof(*source.prepare);
  if(trial<3u)proof[1u+trial]=bytes;
  proof[0]++;
 }
 proof[4]=sizeof(source);return FW_OK;
}
