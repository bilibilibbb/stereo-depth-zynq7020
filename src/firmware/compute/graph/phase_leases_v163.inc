/* Read-only admission across every live graph/capsule and queue consumer.
 * This must run before begin performs any prior or tensor writes. */
RN_API int r14_graph_coop_span_leased(const void *s,const void *p,size_t n){
 const g_coop_job *j=(const g_coop_job *)s;unsigned k;
 if(!j || j->magic!=G_COOP_MAGIC || !p || !n || n>UINTPTR_MAX-(uintptr_t)p)return RN_ARGUMENT;
 if(!j->owned)return 0;
 for(k=0;k<16;k++)if(j->lease_bytes[k] && span_overlap(p,n,j->lease[k],j->lease_bytes[k]))return 1;
 return 0;
}
RN_API int r14_graph_coop_validate_frame(const void *s,const r14_graph_frame *f,const void *const *holders,unsigned count,const void *const *regions,const size_t *bytes,unsigned region_count){
 const g_coop_job *j=(const g_coop_job *)s;const void *p[16];size_t n[16];unsigned i,k;int rc;
 if(!j || j->magic!=G_COOP_MAGIC || !f || (count && !holders) || (region_count && (!regions || !bytes)))return RN_ARGUMENT;
 if(j->owned)return RN_BUSY_ERROR;
 p[0]=f->left;n[0]=3u*N*4u;p[1]=f->right;n[1]=3u*N*4u;
 p[2]=f->native;n[2]=4u*N*4u;p[3]=f->gray;n[3]=N*4u;
 p[4]=f->right_gray;n[4]=N*4u;p[5]=f->curve;n[5]=34u*CN*4u;
 p[6]=f->output;n[6]=N*4u;p[7]=f->tensor_memory;n[7]=f->tensor_bytes;
 p[8]=f->float_memory;n[8]=f->float_bytes;p[9]=s;n[9]=sizeof(*j);
 p[10]=f->statistics;n[10]=f->statistics?7u*sizeof(uint64_t):0;
 p[11]=f->post;n[11]=f->post?sizeof(*f->post):0;
 p[12]=f->post?f->post->source:0;n[12]=f->post?f->post->source_bytes:0;
 p[13]=f->post?f->post->destination:0;n[13]=f->post?f->post->destination_bytes:0;
 p[14]=f->post?f->post->ownership:0;n[14]=f->post?sizeof(*f->post->ownership):0;
 p[15]=f->filter_memory;n[15]=(f->filter_memory && !f->detach_gray)?f->filter_bytes:0;
 for(k=0;k<count;k++)if(!holders[k] || holders[k]==s || ((const g_coop_job *)holders[k])->magic!=G_COOP_MAGIC)return RN_ARGUMENT;
 for(k=0;k<region_count;k++)if(!regions[k] || !bytes[k] || bytes[k]>UINTPTR_MAX-(uintptr_t)regions[k])return RN_ARGUMENT;
 for(i=0;i<16;i++)if(n[i]){
  if(!p[i] || n[i]>UINTPTR_MAX-(uintptr_t)p[i])return RN_ARGUMENT;
  for(k=0;k<count;k++){rc=r14_graph_coop_span_leased(holders[k],p[i],n[i]);if(rc<0)return rc;if(rc)return RN_BUSY_ERROR;}
  for(k=0;k<region_count;k++)if(span_overlap(p[i],n[i],regions[k],bytes[k]))return RN_BUSY_ERROR;
 }
 return RN_OK;
}
