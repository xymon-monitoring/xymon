/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBGEND_IPC_H__
#define __BBGEND_IPC_H__

#define SHAREDBUFSZ (3*MAXMSG+4096)

/* Semaphore numbers */
#define BOARDBUSY   0
#define GOCLIENT    1
#define CLIENTCOUNT 2

#define CHAN_MASTER 0
#define CHAN_CLIENT 1

enum msgchannels_t { C_STATUS, C_STACHG, C_PAGE, C_DATA, C_NOTES, C_ENADIS };

typedef struct bbd_channel_t {
	enum msgchannels_t channelid;
	int shmid;
	int semid;
	char *channelbuf;
	unsigned int seq;
	struct bbd_channel_t *next;
} bbd_channel_t;

extern char *channelnames[];

extern bbd_channel_t *setup_channel(enum msgchannels_t chnname, int role);
extern void close_channel(bbd_channel_t *chn, int role);
#endif

