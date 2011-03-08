/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* critical.cfg file.                                                         */
/*                                                                            */
/* Copyright (C) 2005-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADCRITICALCONF_H__
#define __LOADCRITICALCONF_H__

#include <time.h>

typedef struct critconf_t {
	char *key;
	int priority;
	time_t starttime, endtime;
	char *crittime;
	char *ttgroup;
	char *ttextra;
	char *updinfo;
} critconf_t;

#define CRITCONF_TIMEFILTER 1
#define CRITCONF_FIRSTMATCH 2
#define CRITCONF_FIRST      3
#define CRITCONF_NEXT       4
#define CRITCONF_RAW_FIRST  5
#define CRITCONF_RAW_NEXT   6
#define CRITCONF_FIRSTHOSTMATCH 7

extern int load_critconfig(char *fn);
extern critconf_t *get_critconfig(char *key, int flags, char **resultkey);
extern int update_critconfig(critconf_t *rec);
extern void addclone_critconfig(char *origin, char *newclone);
extern void dropclone_critconfig(char *drop);
extern int delete_critconfig(char *dropkey, int evenifcloned);

#endif

