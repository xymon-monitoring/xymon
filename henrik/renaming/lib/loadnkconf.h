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

#ifndef __LOADNKCONF_H__
#define __LOADNKCONF_H__

#include <time.h>

typedef struct nkconf_t {
	char *key;
	int priority;
	time_t starttime, endtime;
	char *nktime;
	char *ttgroup;
	char *ttextra;
	char *updinfo;
} nkconf_t;

#define NKCONF_TIMEFILTER 1
#define NKCONF_FIRSTMATCH 2
#define NKCONF_FIRST      3
#define NKCONF_NEXT       4
#define NKCONF_RAW_FIRST  5
#define NKCONF_RAW_NEXT   6
#define NKCONF_FIRSTHOSTMATCH 7

extern int load_nkconfig(char *fn);
extern nkconf_t *get_nkconfig(char *key, int flags, char **resultkey);
extern int update_nkconfig(nkconf_t *rec);
extern void addclone_nkconfig(char *origin, char *newclone);
extern void dropclone_nkconfig(char *drop);
extern int delete_nkconfig(char *dropkey, int evenifcloned);

#endif

