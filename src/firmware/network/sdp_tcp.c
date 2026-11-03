#include "sdp_tcp.h"
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8 | p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];}
static int nonzero64(const uint8_t *p){return be32(p) || be32(p+4);}
static void restart(sdp_tcp *s){s->queued=s->acked=0;s->ack_used=s->application_ack=0;}
static void drop(sdp_tcp *s){
    struct tcp_pcb *p=s->peer;s->peer=NULL;restart(s);s->disconnects++;
    if(p){tcp_arg(p,NULL);tcp_recv(p,NULL);tcp_sent(p,NULL);tcp_err(p,NULL);tcp_abort(p);}
}
static err_t reject(sdp_tcp *s,struct tcp_pcb *p){
    if(!p)return ERR_VAL;
    if(p==s->peer)drop(s);else if(p)tcp_abort(p);
    return ERR_ABRT;
}
static void failed(void *arg,err_t err){
    sdp_tcp *s=arg;
    /* lwIP has already freed this PCB. Never close/abort it again. */
    s->last_lwip_error=err;s->peer=NULL;s->disconnects++;restart(s);
}
static err_t sent(void *arg,struct tcp_pcb *p,u16_t bytes){
    sdp_tcp *s=arg;
    if(p!=s->peer || !s->active || bytes>s->queued-s->acked)return reject(s,p);
    s->acked+=bytes;return ERR_OK;
}
static int correct_ack(const sdp_tcp *s){
    uint8_t expected[96]={0};
    memcpy(expected,"SDP1",4);expected[4]=1;expected[5]=4;expected[7]=96;
    memcpy(expected+16,s->packet+16,24);memcpy(expected+72,s->packet+72,4);
    memcpy(expected+84,s->packet+12,4);
    return !memcmp(expected,s->ack,sizeof(expected));
}
static err_t received(void *arg,struct tcp_pcb *p,struct pbuf *buf,err_t err){
    sdp_tcp *s=arg;unsigned bytes,copied;
    if(!buf)return reject(s,p);
    bytes=buf->tot_len;
    if(p!=s->peer || err!=ERR_OK || s->active_kind!=2u || !s->active || s->application_ack ||
       s->queued!=s->length || bytes>96u-s->ack_used){pbuf_free(buf);return reject(s,p);}
    copied=pbuf_copy_partial(buf,s->ack+s->ack_used,(u16_t)bytes,0);
    tcp_recved(p,(u16_t)bytes);pbuf_free(buf);
    if(copied!=bytes)return reject(s,p);
    s->ack_used+=bytes;
    if(s->ack_used==96u){
        if(!correct_ack(s))return reject(s,p);
        s->application_ack=1;
    }
    return ERR_OK;
}
static err_t accepted(void *arg,struct tcp_pcb *p,err_t err){
    sdp_tcp *s=arg;
    if(err!=ERR_OK || !p || s->peer)return reject(s,p);
    s->peer=p;restart(s);s->reconnects++;
    tcp_arg(p,s);tcp_sent(p,sent);tcp_recv(p,received);tcp_err(p,failed);
    tcp_nagle_disable(p);return ERR_OK;
}
int sdp_tcp_listen(sdp_tcp *s,unsigned kind,uint16_t port){
    struct tcp_pcb *p,*listener;err_t rc;
    if(!s || s->listener || s->peer || s->active || (kind!=1u && kind!=2u) || !port)return SDP_ARGUMENT;
    p=tcp_new();if(!p)return SDP_PENDING;
    rc=tcp_bind(p,IP_ADDR_ANY,port);
    if(rc){tcp_close(p);s->last_lwip_error=rc;return SDP_IO;}
    listener=tcp_listen_with_backlog(p,1);
    if(!listener){tcp_close(p);return SDP_PENDING;}
    s->listener=listener;s->kind=kind;s->result=SDP_OK;
    tcp_arg(listener,s);tcp_accept(listener,accepted);return SDP_OK;
}
int sdp_tcp_begin(sdp_tcp *s,const void *packet,size_t bytes){
    const uint8_t *p=packet;uint32_t payload;unsigned width,height,format;
    if(!s || !s->listener || !p || bytes<96u || bytes>96u+640u*480u*3u ||
       memcmp(p,"SDP1",4) || p[4]!=1u || (p[5]!=s->kind && !(s->kind==2u && p[5]==3u)) || be16(p+6)!=96u ||
       !nonzero64(p+16) || be16(p+82)>1u || be32(p+92))return SDP_ARGUMENT;
    payload=be32(p+8);width=be16(p+76);height=be16(p+78);format=be16(p+80);
    if(payload!=bytes-96u)return SDP_ARGUMENT;
    if(p[5]==3u){
        if(payload || be32(p+12) || width || height || format || be16(p+82) || be32(p+84)>7u)return SDP_ARGUMENT;
    }else{
    if(!nonzero64(p+24) || be32(p+84) || be32(p+88) || !width || !height || width>640u || height>480u)return SDP_ARGUMENT;
    if(s->kind==1u){
        if((format!=1u && format!=2u) || (format==2u && width%2u) ||
           payload!=width*height*(format==1u?3u:2u))return SDP_ARGUMENT;
    }else if(width!=320u || height!=240u || (format!=3u && format!=4u && format!=5u) || payload!=(format==5u?691240u:691200u) ||
             (be32(p+72)!=11459u && be32(p+72)!=1u))return SDP_ARGUMENT;
    }
    if(s->active)return SDP_BUSY;
    if(!s->peer)return SDP_PENDING;
    s->packet=p;s->length=bytes;s->active_kind=p[5];s->active=1;s->result=SDP_PENDING;restart(s);return SDP_OK;
}
int sdp_tcp_result(const sdp_tcp *s){return !s?SDP_ARGUMENT:s->active?SDP_PENDING:s->result;}
int sdp_tcp_step(sdp_tcp *s){
    size_t take;err_t rc;
    if(!s)return SDP_ARGUMENT;
    if(!s->active)return s->result;
    if(s->acked==s->length && (s->active_kind!=2u || s->application_ack)){
        s->active=0;s->packet=NULL;s->result=SDP_OK;return SDP_OK;
    }
    if(!s->peer)return SDP_PENDING;
    take=s->length-s->queued;
    if(take>8192u)take=8192u;
    if(take>tcp_sndbuf(s->peer))take=tcp_sndbuf(s->peer);
    if(take){
        rc=tcp_write(s->peer,s->packet+s->queued,(u16_t)take,TCP_WRITE_FLAG_COPY);
        if(rc==ERR_OK)s->queued+=take;
        else if(rc!=ERR_MEM){s->last_lwip_error=rc;drop(s);return SDP_PENDING;}
    }
    rc=tcp_output(s->peer);if(rc!=ERR_OK)s->last_lwip_error=rc;
    return SDP_PENDING;
}
void sdp_tcp_cancel(sdp_tcp *s){
    if(!s || !s->active)return;
    drop(s);s->active=0;s->packet=NULL;s->result=SDP_CANCELLED;
}
int sdp_tcp_close(sdp_tcp *s){
    err_t rc;if(!s)return SDP_ARGUMENT;
    sdp_tcp_cancel(s);if(s->peer)drop(s);
    if(s->listener){rc=tcp_close(s->listener);if(rc)return SDP_PENDING;s->listener=NULL;}
    return SDP_OK;
}
