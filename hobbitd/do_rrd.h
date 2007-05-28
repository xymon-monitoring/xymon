/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DO_RRD_H__
#define __DO_RRD_H__

#include <time.h>

#include "libbbgen.h"

extern char *rrddir;
extern void setup_exthandler(char *handlerpath, char *ids);
extern void update_rrd(char *hostname, char *testname, char *restofmsg, time_t tstamp, char *sender, hobbitrrd_t *ldef);
extern void rrdcacheflushall(void);
extern void rrdcacheflushhost(char *hostname);

#endif

