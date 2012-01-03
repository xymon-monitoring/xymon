/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "ntptalk.h"

/* begin OS deps */
typedef double ntp_float_t;
typedef unsigned int ntp_int4_t;
typedef unsigned char ntp_int1_t;
/* end OS deps */

#define NTP_CLIENT      3
#define NTP_V3          3

/* NTP measures time since Jan 1900, whereas Unix from Jan 1970: */
#define SECONDS_1970_VS_1900_LONG       2208988800UL
#define SECONDS_1970_VS_1900_FLOAT      2208988800.0

struct ntp_packet_t {
	ntp_int1_t li_vn_mode;
	ntp_int1_t stratum;
	ntp_int1_t poll;
	ntp_int1_t precision;
	ntp_int4_t root_delay;
	ntp_int4_t root_dispersion;
	ntp_int4_t ref_id;
	ntp_int4_t ref_timestamp_hi;
	ntp_int4_t ref_timestamp_lo;
	ntp_int4_t orig_timestamp_hi;
	ntp_int4_t orig_timestamp_lo;
	ntp_int4_t receive_timestamp_hi;
	ntp_int4_t receive_timestamp_lo;
	ntp_int4_t transmit_timestamp_hi;
	ntp_int4_t transmit_timestamp_lo;
} ntp_packet_t;

/* all ntp_float_t are in NTP format */
static ntp_float_t net_pair_to_ntp_float_t (ntp_int4_t * p)
{
    return (ntp_float_t) ntohl (p[0]) + ((ntp_float_t) ntohl (p[1])) / 4294967296.0;
}

static void tv_to_net_pair (struct timeval *t, ntp_int4_t * p)
{
    unsigned long long l;
    ntp_int4_t u;
    u = (ntp_int4_t) t->tv_sec;
    u += SECONDS_1970_VS_1900_LONG;
    p[0] = (ntp_int4_t) htonl (u);
    l = (unsigned long long) t->tv_usec;
    l = (l << 32) / 1000000ULL;
    u = (ntp_int4_t) l;
    p[1] = (ntp_int4_t) htonl (u);
}

static ntp_float_t mod_this (ntp_float_t x)
{
    if (x >= 4294967296.0 / 2)
	x -= 4294967296.0;
    if (x <= -4294967296.0 / 2)
	x += 4294967296.0;
    return x;
}


int ntp_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata)
{
	int res = 0, n;
	myconn_t *rec = (myconn_t *)userdata;

	switch (id) {
	  case CONN_CB_CONNECT_FAILED:         /* Client mode: New outbound connection failed */
		rec->talkresult = TALK_CONN_FAILED;
		break;

	  case CONN_CB_SSLHANDSHAKE_FAILED:    /* Client/server mode: SSL handshake failed (connection will close) */
		rec->talkresult = TALK_BADSSLHANDSHAKE;
		break;

	  case CONN_CB_CONNECT_COMPLETE:       /* Client mode: New outbound connection succeded */
		rec->talkresult = TALK_OK;
		break;

	  case CONN_CB_WRITECHECK:             /* Client/server mode: Check if application wants to write data */
		res = ((rec->step & 1) == 0) && (rec->step <= 2*NTPTRIES);
		break;

	  case CONN_CB_WRITE:                  /* Client/server mode: Ready for application to write data w/ conn_write() */
		{
			struct ntp_packet_t p;
			struct timeval tv_send;

			memset (&p, 0, sizeof (p));
			p.li_vn_mode = (NTP_V3 << 3) | NTP_CLIENT;
			p.poll = 8;
			p.precision = 0;
			gettimeofday (&tv_send, NULL);
			tv_to_net_pair (&tv_send, &p.transmit_timestamp_hi);
			tv_to_net_pair (&tv_send, rec->ntp_sendtime);
			n = conn_write(connection, &p, sizeof(p));
			if (n == sizeof(ntp_packet_t)) {
				rec->step++;
			}
			else {
				/* Short send, skip to next attempt */
				rec->ntpdiff[rec->step / 2] = -1000.0;
				rec->step += 2;
			}

			res = n;
		}
		break;

	  case CONN_CB_READCHECK:              /* Client/server mode: Check if application wants to read data */
		res = ((rec->step & 1) == 1) && (rec->step <= 2*NTPTRIES);
		break;

	  case CONN_CB_READ:                   /* Client/server mode: Ready for application to read data w/ conn_read() */
		{    
			ntp_int4_t recv_time[2];
			struct timeval tv_recv;
			struct ntp_packet_t p;

			gettimeofday (&tv_recv, NULL);
			n = conn_read(connection, &p, sizeof(ntp_packet_t));

			if (n == sizeof(ntp_packet_t)) {
				tv_to_net_pair (&tv_recv, recv_time);
				rec->ntpdiff[rec->step / 2] =
					(mod_this (net_pair_to_ntp_float_t (&p.receive_timestamp_hi) - net_pair_to_ntp_float_t (rec->ntp_sendtime)) +
					 mod_this (net_pair_to_ntp_float_t (&p.transmit_timestamp_hi) - net_pair_to_ntp_float_t (recv_time))) / 2.0;
				rec->ntpstratum = p.stratum;
			}
			else {
				/* Short packet */
				rec->ntpdiff[rec->step / 2] = -1000.0;
			}

			rec->step++;
			res = n;
		}
		break;

	  case CONN_CB_TIMEOUT:
		rec->talkresult = TALK_CONN_TIMEOUT;
		conn_close_connection(connection, NULL);
		break;

	  case CONN_CB_CLOSED:                 /* Client/server mode: Connection has been closed */
		if (rec && (rec->step > 2*NTPTRIES)) {
			ntp_float_t average = 0.0, maximum = -1000.0, minimum = 1000.0;
			int i, i_max = 0, i_min = 0;
			for (i = 0; i < NTPTRIES; i++) {
				if (rec->ntpdiff[i] > maximum) {
					maximum = rec->ntpdiff[i];
					i_max = i;
				}
				if (rec->ntpdiff[i] < minimum) {
					minimum = rec->ntpdiff[i];
					i_min = i;
				}
			}
			for (i = 0; i < NTPTRIES; i++) {
				/* Olympic scoring: Average leaves out the min. and max. values */
				if (i != i_max && i != i_min) average += rec->ntpdiff[i];
			}
			rec->ntpoffset = average / (ntp_float_t)(NTPTRIES - 2);
		}
		return 0;

	  case CONN_CB_CLEANUP:                /* Client/server mode: Connection cleanup */
		if (rec) {
			connection->userdata = NULL;
			test_is_done(rec);
		}
		return 0;

	  default:
		break;
	}

	return res;

}

