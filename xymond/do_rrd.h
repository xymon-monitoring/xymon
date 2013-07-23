/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DO_RRD_H__
#define __DO_RRD_H__

#include <time.h>

#include "libxymon.h"

extern char *rrddir;
extern char *trackmax;
extern int use_rrd_cache;
extern int no_rrd;
extern void setup_exthandler(char *handlerpath, char *ids);
extern void update_rrd(char *hostname, char *testname, char *restofmsg, time_t tstamp, char *sender, xymonrrd_t *ldef, char *classname, char *pagepaths);
extern void rrdcacheflushall(void);
extern void rrdcacheflushhost(char *hostname);
extern void setup_extprocessor(char *cmd);
extern void shutdown_extprocessor(void);


#endif

