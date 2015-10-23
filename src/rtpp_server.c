/*
 * Copyright (c) 2004-2006 Maxim Sobolev <sobomax@FreeBSD.org>
 * Copyright (c) 2006-2007 Sippy Software, Inc., http://www.sippysoft.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rtp.h"
#include "rtp_packet.h"
#include "rtpp_types.h"
#include "rtpp_refcnt.h"
#include "rtpp_server.h"
#include "rtpp_server_fin.h"
#include "rtpp_genuid_singlet.h"
#include "rtpp_util.h"

/*
 * Minimum length of each RTP packet in ms.
 * Actual length may differ due to codec's framing constrains.
 */
#define RTPS_TICKS_MIN  10

#define RTPS_SRATE      8000

struct rtpp_server_priv {
    struct rtpp_server_obj pub;
    double btime;
    unsigned char buf[1024];
    rtp_hdr_t *rtp;
    unsigned char *pload;
    int fd;
    int loop;
    uint64_t dts;
    int ptime;
    void *rco[0];
};

#define PUB2PVT(pubp)      ((struct rtpp_server_priv *)((char *)(pubp) - offsetof(struct rtpp_server_priv, pub)))

static void rtpp_server_dtor(struct rtpp_server_priv *);
static struct rtp_packet *rtpp_server_get(struct rtpp_server_obj *, double, int *);
static uint32_t rtpp_server_get_ssrc(struct rtpp_server_obj *);
static uint16_t rtpp_server_get_seq(struct rtpp_server_obj *);

struct rtpp_server_obj *
rtpp_server_ctor(const char *name, rtp_type_t codec, int loop, double dtime,
  int ptime)
{
    struct rtpp_server_priv *rp;
    int fd;
    char path[PATH_MAX + 1];

    sprintf(path, "%s.%d", name, codec);
    fd = open(path, O_RDONLY);
    if (fd == -1)
	goto e0;

    rp = rtpp_zmalloc(sizeof(struct rtpp_server_priv) + rtpp_refcnt_osize());
    if (rp == NULL) {
	goto e1;
    }

    rp->pub.rcnt = rtpp_refcnt_ctor_pa(&rp->rco[0], rp,
      (rtpp_refcnt_dtor_t)&rtpp_server_dtor);
    if (rp->pub.rcnt == NULL) {
        goto e2;
    }

    rp->btime = dtime;
    rp->dts = 0;
    rp->fd = fd;
    rp->loop = (loop > 0) ? loop - 1 : loop;
    rp->ptime = (ptime > 0) ? ptime : RTPS_TICKS_MIN;

    rp->rtp = (rtp_hdr_t *)rp->buf;
    rp->rtp->version = 2;
    rp->rtp->p = 0;
    rp->rtp->x = 0;
    rp->rtp->cc = 0;
    rp->rtp->mbt = 1;
    rp->rtp->pt = codec;
    rp->rtp->ts = 0;
    rp->rtp->seq = random() & 0xffff;
    rp->rtp->ssrc = random();
    rp->pload = rp->buf + RTP_HDR_LEN(rp->rtp);

    rp->pub.get = &rtpp_server_get;
    rp->pub.get_ssrc = &rtpp_server_get_ssrc;
    rp->pub.get_seq = &rtpp_server_get_seq;
    rtpp_gen_uid(&rp->pub.sruid);

    return (&rp->pub);
e2:
    free(rp);
e1:
    close(fd);
e0:
    return (NULL);
}

static void
rtpp_server_dtor(struct rtpp_server_priv *rp)
{

    rtpp_server_obj_fin(&rp->pub);
    close(rp->fd);
    free(rp);
}

static struct rtp_packet *
rtpp_server_get(struct rtpp_server_obj *self, double dtime, int *rval)
{
    struct rtp_packet *pkt;
    uint32_t ts;
    int rlen, rticks, bytes_per_frame, ticks_per_frame, number_of_frames;
    int hlen;
    struct rtpp_server_priv *rp;

    rp = PUB2PVT(self);

    if (rp->btime + ((double)rp->dts / 1000.0) > dtime) {
        *rval = RTPS_LATER;
	return (NULL);
    }

    switch (rp->rtp->pt) {
    case RTP_PCMU:
    case RTP_PCMA:
	bytes_per_frame = 8;
	ticks_per_frame = 1;
	break;

    case RTP_G729:
	/* 10 ms per 8 kbps G.729 frame */
	bytes_per_frame = 10;
	ticks_per_frame = 10;
	break;

    case RTP_G723:
	/* 30 ms per 6.3 kbps G.723 frame */
	bytes_per_frame = 24;
	ticks_per_frame = 30;
	break;

    case RTP_GSM:
	/* 20 ms per 13 kbps GSM frame */
	bytes_per_frame = 33;
	ticks_per_frame = 20;
	break;

    case RTP_G722:
	bytes_per_frame = 8;
	ticks_per_frame = 1;
	break;

    default:
	*rval = RTPS_ERROR;
        return (NULL);
    }

    number_of_frames = rp->ptime / ticks_per_frame;
    if (rp->ptime % ticks_per_frame != 0)
	number_of_frames++;

    rlen = bytes_per_frame * number_of_frames;
    rticks = ticks_per_frame * number_of_frames;
    rp->dts += rticks;

    pkt = rtp_packet_alloc();
    if (pkt == NULL) {
        *rval = RTPS_ENOMEM;
        return (NULL);
    }
    hlen = RTP_HDR_LEN(rp->rtp);

    if (read(rp->fd, pkt->data.buf + hlen, rlen) != rlen) {
	if (rp->loop == 0 || lseek(rp->fd, 0, SEEK_SET) == -1 ||
	  read(rp->fd, pkt->data.buf + hlen, rlen) != rlen) {
	    *rval = RTPS_EOF;
            rtp_packet_free(pkt);
            return (NULL);
        }
	if (rp->loop != -1)
	    rp->loop -= 1;
    }

    if (rp->rtp->mbt != 0 && ntohs(rp->rtp->seq) != 0) {
	rp->rtp->mbt = 0;
    }

    ts = ntohl(rp->rtp->ts);
    rp->rtp->ts = htonl(ts + (RTPS_SRATE * rticks / 1000));
    rp->rtp->seq = htons(ntohs(rp->rtp->seq) + 1);

    memcpy(&pkt->data.header, rp->rtp, hlen);

    pkt->size = hlen + rlen;
    return (pkt);
}

static uint32_t
rtpp_server_get_ssrc(struct rtpp_server_obj *self)
{
    struct rtpp_server_priv *rp;

    rp = PUB2PVT(self);
    return (ntohl(rp->rtp->ssrc));
}

static uint16_t
rtpp_server_get_seq(struct rtpp_server_obj *self)
{
    struct rtpp_server_priv *rp;

    rp = PUB2PVT(self);
    return (ntohs(rp->rtp->seq));
}