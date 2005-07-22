/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CLIENT_CONFIG_H__
#define __CLIENT_CONFIG_H__

#include "libbbgen.h"

extern void load_client_config(char *fn);

extern void get_cpu_thresholds(namelist_t *hinfo, float *loadyellow, float *loadred, int *recentlimit, int *ancientlimit);
extern void get_disk_thresholds(namelist_t *hhinfo, char *fsname, int *warnlevel, int *paniclevel);
extern void get_memory_thresholds(namelist_t *hhinfo, 
				  int *physyellow, int *physred, 
				  int *swapyellow, int *swapred, 
				  int *actyellow, int *actred);

extern int clear_process_counts(namelist_t *hinfo);
extern char *check_process_count(int *pcount, int *lowlim, int *uplim, int *pcolor);
extern void add_process_count(char *pname);

#endif

