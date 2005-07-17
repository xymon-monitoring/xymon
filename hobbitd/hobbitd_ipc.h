/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HOBBITD_IPC_H__
#define __HOBBITD_IPC_H__

#define SHAREDBUFSZ 102400

/* Semaphore numbers */
#define BOARDBUSY   0
#define GOCLIENT    1
#define CLIENTCOUNT 2

#define CHAN_MASTER 0
#define CHAN_CLIENT 1

enum msgchannels_t { C_STATUS=1, C_STACHG, C_PAGE, C_DATA, C_NOTES, C_ENADIS, C_CLIENT, C_LAST };

typedef struct hobbitd_channel_t {
	enum msgchannels_t channelid;
	int shmid;
	int semid;
	char *channelbuf;
	unsigned int seq;
	unsigned long msgcount;
	struct hobbitd_channel_t *next;
} hobbitd_channel_t;

extern char *channelnames[];

extern hobbitd_channel_t *setup_channel(enum msgchannels_t chnname, int role);
extern void close_channel(hobbitd_channel_t *chn, int role);
#endif

