static int overlap(uint32_t a,uint32_t na,uint32_t b,uint32_t nb){return (uint64_t)a+na>b && (uint64_t)b+nb>a;}
static int valid_descriptor(const rn_descriptor *p){
    const uint32_t *w=p->word;uint32_t iw=w[RN_D_INPUT_WIDTH],ih=w[RN_D_INPUT_HEIGHT],ci=w[RN_D_INPUT_CHANNELS];
    uint32_t ow=w[RN_D_OUTPUT_WIDTH],oh=w[RN_D_OUTPUT_HEIGHT],co=w[RN_D_OUTPUT_CHANNELS],k=w[RN_D_KERNEL_WIDTH],s=w[RN_D_STRIDE2];
    uint32_t dw=w[RN_D_CMD_DEPTHWISE],dil=w[RN_D_DILATION_SHIFT],channels=w[RN_D_DESTINATION_CHANNELS];
    if(dw>1 || iw<4 || iw>320 || (iw&3u) || ih<2 || ih>256 || (ih&1u) || s>1 || !ci || ci>32 || !co || co>16 ||
       ow!=(iw>>s) || oh!=(ih>>s) || w[RN_D_KERNEL_HEIGHT]!=k || (w[RN_D_SOURCE_FRACTION]&~0x33fu) || (w[RN_D_SOURCE_FRACTION]&63u)>30 || w[RN_D_TARGET_FRACTION]>30 ||
       ((w[RN_D_SOURCE_FRACTION]&RN_SOURCE_RELU6) && ((w[RN_D_SOURCE_FRACTION]&63u)<9 || (w[RN_D_SOURCE_FRACTION]&63u)>16)) ||
       w[RN_D_ACTIVATION_MODE] || w[RN_D_REPLICATE] || w[RN_D_LATTICE_TWO] || w[RN_D_PHASE_X] || w[RN_D_PHASE_Y] || w[RN_D_STRIPE_PIXELS])return 0;
    if(w[RN_D_DESTINATION_WIDTH]!=ow || w[RN_D_DESTINATION_HEIGHT]!=oh || !channels || channels>32 ||
       w[RN_D_CHANNEL_START]>channels || co>channels-w[RN_D_CHANNEL_START])return 0;
    if(w[RN_D_SOURCE_BYTES]!=(uint64_t)iw*ih*ci*((w[RN_D_SOURCE_FRACTION]&RN_SOURCE_WIDE)?4u:2u) || w[RN_D_DESTINATION_BYTES]!=(uint64_t)ow*oh*channels*2u)return 0;
    if(overlap(w[RN_D_SOURCE_ADDRESS],w[RN_D_SOURCE_BYTES],w[RN_D_DESTINATION_ADDRESS],w[RN_D_DESTINATION_BYTES]))return 0;
    if(dw){
        if(ci!=co || co>4 || channels!=co || k!=3 || dil>2 || (s && dil) || w[RN_D_CHANNEL_START] ||
           w[RN_D_PADDING_X]!=(1u<<dil) || w[RN_D_PADDING_Y]!=(1u<<dil))return 0;
    }else{
        if((k!=1 && k!=3) || dil || ci*k*k>32 || w[RN_D_PADDING_X]!=k/2 || w[RN_D_PADDING_Y]!=k/2 ||
           w[RN_D_WEIGHT_BYTES]!=ci*k*k*32u)return 0;
        if(overlap(w[RN_D_WEIGHT_ADDRESS],w[RN_D_WEIGHT_BYTES],w[RN_D_DESTINATION_ADDRESS],w[RN_D_DESTINATION_BYTES]))return 0;
    }
    if(w[80]>>16)return 0;
    return 1;
}
