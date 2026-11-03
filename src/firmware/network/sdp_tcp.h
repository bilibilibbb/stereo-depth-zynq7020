#ifndef SDP_TCP_H
#define SDP_TCP_H
#include <stddef.h>
#include <stdint.h>
#include "lwip/tcp.h"

enum { SDP_OK=0,SDP_PENDING=1,SDP_ARGUMENT=-1,SDP_CANCELLED=-4,SDP_BUSY=-5,SDP_IO=-9 };
typedef struct {
    struct tcp_pcb *listener,*peer;
    const uint8_t *packet;
    size_t length,queued,acked;
    uint8_t ack[96];
    unsigned ack_used,kind,active_kind,active,application_ack;
    int result,last_lwip_error;
    uint32_t reconnects,disconnects;
} sdp_tcp;
/* CPU0/raw-lwIP context only, after real GEM/netif setup. No ISR/CPU1 call.
 * Zero initialize. kind1 preview/port6000; kind2 paired results/port6001.
 * One client per endpoint; a second client cannot steal an active stream. */
int sdp_tcp_listen(sdp_tcp *,unsigned,uint16_t);
/* Immutable complete packet. OK borrows the source; PENDING/BUSY do not.
 * One in-flight message, no unbounded queue. The result packet survives a
 * disconnect and restarts at byte zero when the PC reconnects. */
int sdp_tcp_begin(sdp_tcp *,const void *,size_t);
/* At most one <=2048-byte COPY enqueue and one tcp_output per call.
 * The board must ALSO service xemacif_input and TCP timers every main loop.
 * ERR_MEM is backpressure, never a spin wait. */
int sdp_tcp_step(sdp_tcp *);
/* OK/negative means source loan ended. Pair OK requires both TCP delivery
 * and matching PC validated-pair ACK; neither proves browser repaint. */
int sdp_tcp_result(const sdp_tcp *);
/* Aborts the old connection, preserving framing on the next connection.
 * COPY enqueues never leave GEM holding the caller's source buffer. */
void sdp_tcp_cancel(sdp_tcp *);
int sdp_tcp_close(sdp_tcp *);
#endif
