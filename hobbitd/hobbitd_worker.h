/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBDWORKER_H__
#define __BBDWORKER_H__

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbgend.h"
#include "bbdutil.h"

extern unsigned char *get_bbgend_message(char *id, int *seq, struct timeval *timeout);
extern unsigned char *nlencode(unsigned char *msg);
extern void nldecode(unsigned char *msg);

#endif

