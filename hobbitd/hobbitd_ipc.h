extern char *channelnames[];

extern bbd_channel_t *setup_channel(enum msgchannels_t chnname, int flags);
extern void close_channel(bbd_channel_t *chn, int flags);

