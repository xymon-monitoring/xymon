#ifndef __BBDWORKER_H__
#define __BBDWORKER_H__

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbd_net.h"
#include "bbdutil.h"

extern unsigned char *get_bbgend_message(void);
extern unsigned char *nlencode(unsigned char *msg);
extern void nldecode(unsigned char *msg);

#endif

