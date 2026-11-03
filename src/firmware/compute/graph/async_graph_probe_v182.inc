/* Read-only host-test observations of actual phase boundaries. No duplicated
 * math and no timing assumptions. Absent from the target build. */
unsigned cpu_async_phase_v182(const void *s){const g_coop_job *j=(const g_coop_job*)s;return j && j->magic==G_COOP_MAGIC?j->phase:UINT32_MAX;}
unsigned cpu_async_node_v182(const void *s){const g_coop_job *j=(const g_coop_job*)s;return j && j->magic==G_COOP_MAGIC?j->node:UINT32_MAX;}
unsigned cpu_async_cursor_v182(const void *s){const g_coop_job *j=(const g_coop_job*)s;return j && j->magic==G_COOP_MAGIC?j->cpu_cursor:UINT32_MAX;}
unsigned cpu_async_checkpoint_state_v182(const void *s){const g_color_checkpoint *c=(const g_color_checkpoint*)s;return c && c->checkpoint_magic==G_CHECKPOINT_MAGIC?c->state:UINT32_MAX;}
size_t cpu_async_checkpoint_filter_hold_v182(const void *s){const g_color_checkpoint *c=(const g_color_checkpoint*)s;return c && c->checkpoint_magic==G_CHECKPOINT_MAGIC?c->job.lease_bytes[15]:SIZE_MAX;}
void *cpu_async_checkpoint_coarse_v182(void *s){g_color_checkpoint *c=(g_color_checkpoint*)s;return c && c->checkpoint_magic==G_CHECKPOINT_MAGIC?c->planes+37u*CN:0;}
int cpu_async_capture_v182(unsigned kind,const void *s,cpu_probe_fixture *out){
 const g_coop_job *j=(const g_coop_job*)s;const g_color_checkpoint *c=(const g_color_checkpoint*)s;unsigned i;
 if(!s || !out)return RN_ARGUMENT;
 if(kind==2 || kind==3){
  if(c->checkpoint_magic!=G_CHECKPOINT_MAGIC)return RN_ARGUMENT;
  if(kind==2 && c->state==GC_CPU_DECODE){memcpy(out->filtered,c->job.filtered,17u*CN*4u);return RN_OK;}
  if(kind==3 && c->state==GC_TAIL_READY){memcpy(out->coarse,c->planes+37u*CN,2u*CN*4u);return RN_OK;}
  return RN_BUSY_ERROR;
 }
 if(j->magic!=G_COOP_MAGIC || !j->owned)return RN_ARGUMENT;
 switch(kind){
 case 0:
  if(j->node!=51 || j->phase!=G_COOP_NODE)return RN_BUSY_ERROR;
  for(i=0;i<17u*CN;i++){out->raw_logits[i]=g_value(&j->g,rn_nodes[51].input[0],(int)i);}return RN_OK;
 case 1:
  if(j->phase!=G_COOP_COLOR_READY)return RN_BUSY_ERROR;
  memcpy(out->prepared_logits,j->logits,17u*CN*4u);memcpy(out->guide,j->guide,3u*CN*4u);return RN_OK;
 case 4:
  if(j->node!=53 || j->phase!=G_COOP_NODE)return RN_BUSY_ERROR;
  memcpy(out->attributes,g_fp(&j->g,56),8u*128u*160u*4u);return RN_OK;
 case 5:
  if(j->phase!=G_COOP_PRED)return RN_BUSY_ERROR;
  memcpy(out->final_i32,g_i32(&j->g,RN_FINAL),2u*N*4u);return RN_OK;
 case 6:
  if(j->phase!=G_COOP_MEDIAN || j->cpu_cursor)return RN_BUSY_ERROR;
  memcpy(out->prediction,j->pre,PN*4u);return RN_OK;
 case 7:
  if(j->phase!=G_COOP_CPU_RGB || j->cpu_cursor)return RN_BUSY_ERROR;
  memcpy(out->median,j->med,PN*4u);return RN_OK;
 case 8:
  if(j->phase!=G_COOP_GRAY || !j->cpu_rgb_ready)return RN_BUSY_ERROR;
  memcpy(out->rgb,j->rgb,3u*PN);return RN_OK;
 case 9:
  if(j->phase!=G_COOP_NODE || j->node!=0)return RN_BUSY_ERROR;
  memcpy(out->priors,g_fp(&j->g,2),4u*CN*4u);return RN_OK;
 default:return RN_ARGUMENT;
 }
}
