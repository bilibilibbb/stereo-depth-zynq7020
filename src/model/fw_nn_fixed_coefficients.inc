/* Native DMA source images are immutable PS DDR data shared by all frames.
 * Publication is per device/cache lifetime; PL bank residency is unchanged. */
struct np_coeff_view {uint16_t offset[4],pitch[2],rows[2],bias_words[2];};
#ifndef FW_NN_FIXED_COEFFICIENTS
#define FW_NN_FIXED_COEFFICIENTS 1
#endif
#include "fw_nn_fixed_coefficients_data.inc"
#ifdef R14_HOST_REFERENCE
#define NP_COEFF_WORK(d,words,terms) ((d)->coefficient_stats[2]+=(words),(d)->coefficient_stats[3]+=(terms))
#else
#define NP_COEFF_WORK(d,words,terms) ((void)0)
#endif
static void np_coeff_select(np_device *d,unsigned key){
 d->fixed=FW_NN_FIXED_COEFFICIENTS && key<75u && np_coeff_views[key].pitch[0]?np_coeff_views+key:NULL;
#ifdef R14_HOST_REFERENCE
 if(d->coefficients_reference)d->fixed=NULL;
 d->coefficient_stats[0]++;d->coefficient_stats[1]+=d->fixed!=NULL;
#endif
 if(d->fixed && !d->coeff_published){
  d->cache.clean(d->cache.user,np_coeff_data,sizeof(np_coeff_data));d->cache.barrier(d->cache.user);d->coeff_published=1u;
#ifdef R14_HOST_REFERENCE
  d->coefficient_stats[4]++;d->coefficient_stats[5]+=sizeof(np_coeff_data);
#endif
 }
}
static const uint64_t *np_w(const np_device *d){return d->fixed?np_coeff_data+d->fixed->offset[0]:d->weights[0];}
static unsigned np_wp(const np_device *d){return d->fixed?d->fixed->pitch[0]*8u:sizeof(d->weights[0]);}
static const uint64_t *np_b(const np_device *d){return d->fixed?np_coeff_data+d->fixed->offset[1]:d->bias;}
static const uint64_t *np_dw(const np_device *d,unsigned ch){return d->fixed?np_coeff_data+d->fixed->offset[2]+ch*d->fixed->pitch[1]:d->dw_weights[ch];}
static unsigned np_dwp(const np_device *d){return d->fixed?d->fixed->pitch[1]*8u:sizeof(d->dw_weights[0]);}
static const uint64_t *np_db(const np_device *d){return d->fixed?np_coeff_data+d->fixed->offset[3]:d->dw_bias;}
const void *fw_nn_fixed_coefficients(uint32_t *bytes){
 if(bytes)*bytes=FW_NN_FIXED_COEFFICIENTS?sizeof(np_coeff_data):0u;
 return FW_NN_FIXED_COEFFICIENTS?np_coeff_data:NULL;
}
