#ifndef __BBDUTIL_H__
#define __BBDUTIL_H__

#define SHAREDBUFSZ (3*MAXMSG+4096)

/* Semaphore numbers */
#define BOARDBUSY   0
#define GOCLIENT    1
#define CLIENTCOUNT 2

#define CHAN_MASTER 0
#define CHAN_CLIENT 1

extern char *channelnames[];

extern bbd_channel_t *setup_channel(enum msgchannels_t chnname, int role);
extern void close_channel(bbd_channel_t *chn, int role);
#endif

