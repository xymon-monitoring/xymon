/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBGEND_WORKER_H__
#define __BBGEND_WORKER_H__

#include <sys/time.h>

extern unsigned char *get_bbgend_message(char *id, int *seq, struct timeval *timeout);

#endif

