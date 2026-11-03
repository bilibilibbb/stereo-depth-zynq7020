#include "boot_config.h"
#include "memory_layout.h"
#include "camera_capture.h"
#include "ff.h"
#include <string.h>
static FATFS filesystem;
static int read_file(const char *path,void *dst,uint32_t size){
    FIL f;UINT got;uint32_t offset=0;FRESULT rc;int error=0;
    rc=f_open(&f,path,FA_READ);if(rc!=FR_OK)return -10-(int)rc;
    if(f_size(&f)!=size)error=-40;
    while(!error && offset<size){
        uint32_t take=size-offset;if(take>32768u)take=32768u;
        rc=f_read(&f,(uint8_t*)dst+offset,take,&got);if(rc!=FR_OK || got!=take)error=-41;else offset+=take;
    }
    rc=f_close(&f);if(rc!=FR_OK && !error)error=-42;return error;
}
int ss_boot_files(ss_boot_config *c,uint32_t recipe,uint64_t *boot){
    _Alignas(64) uint8_t config[SS_BOOT_CONFIG_BYTES],old[16],next[16],verify[16];
    FIL f;UINT wrote;FRESULT fr;int rc;uint64_t identity;ss_calibration cal;
    if(!c || !boot)return -1;
    if(f_mount(&filesystem,"0:/",1)!=FR_OK)return -50;
    rc=read_file("0:/SNAPCFG.BIN",config,sizeof(config));if(rc)return rc;
    rc=ss_boot_decode(c,config,sizeof(config),recipe);if(rc)return rc;
    rc=read_file("0:/R14CAL.BIN",(void*)(uintptr_t)SS_CALIBRATION_BEGIN,SS_CALIBRATION_BYTES);if(rc)return rc;
    if((ss_crc32_update(UINT32_MAX,(const void*)(uintptr_t)SS_CALIBRATION_BEGIN,SS_CALIBRATION_BYTES)^UINT32_MAX)!=c->calibration_crc)return -51;
    rc=ss_calibration_bind(&cal,(const void*)(uintptr_t)SS_CALIBRATION_BEGIN,SS_CALIBRATION_BYTES,c->platform);
    if(rc || cal.mode_tag!=c->mode_tag)return -52;
    /* Required, already provisioned sequence file. Missing/corrupt sequence
     * is not silently recreated, which could reuse a published identity. */
    rc=read_file("0:/BOOTSEQ.BIN",old,sizeof(old));if(rc)return rc;
    rc=ss_boot_sequence_next(next,old,c->seed,&identity);if(rc)return rc;
    fr=f_open(&f,"0:/BOOTSEQ.BIN",FA_READ|FA_WRITE);if(fr!=FR_OK)return -53;
    fr=f_write(&f,next,sizeof(next),&wrote);
    rc=(fr==FR_OK && wrote==sizeof(next) && f_sync(&f)==FR_OK)?0:-54;
    fr=f_close(&f);if(fr!=FR_OK)rc=-55;if(rc)return rc;
    rc=read_file("0:/BOOTSEQ.BIN",verify,sizeof(verify));if(rc)return rc;
    if(memcmp(next,verify,sizeof(next)))return -56;
    if(f_mount(NULL,"0:/",0)!=FR_OK)return -57;
    *boot=identity;return 0;
}
