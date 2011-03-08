/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __XYMOND_IPC_H__
#define __XYMOND_IPC_H__

#include "xymond_buffer.h"

/* Semaphore numbers */
#define BOARDBUSY   0
#define GOCLIENT    1
#define CLIENTCOUNT 2

#define CHAN_MASTER 0
#define CHAN_CLIENT 1

typedef struct xymond_channel_t {
	enum msgchannels_t channelid;
	int shmid;
	int semid;
	char *channelbuf;
	unsigned int seq;
	unsigned long msgcount;
	struct xymond_channel_t *next;
} xymond_channel_t;

extern char *channelnames[];

extern xymond_channel_t *setup_channel(enum msgchannels_t chnname, int role);
extern void close_channel(xymond_channel_t *chn, int role);
#endif

