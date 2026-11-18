/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* critical.cfg file.                                                         */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
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

#define DEFAULT_CRITCONFIGFN "etc/critical.cfg"

extern int load_critconfig(char *fn);
extern critconf_t *get_critconfig(char *key, int flags, char **resultkey);
/*
 * update_critconfig() writes critical.cfg, after copying the old one to
 * critical.cfg.bak. On anything but CRITSAVE_OK, errno says why.
 */
#define CRITSAVE_OK          0	/* written, and backed up */
#define CRITSAVE_NOOPEN      1	/* could not be opened for writing: unchanged */
#define CRITSAVE_NOBACKUP    2	/* written, but critical.cfg.bak was not */
#define CRITSAVE_WRITEFAIL   3	/* writing failed; put back from the backup: unchanged */
#define CRITSAVE_WRITEFAIL_BROKEN 4	/* writing failed, and it could not be put back: may be incomplete */
#define CRITSAVE_NOREAD      5	/* there, but could not be read, so not backed up: not written, unchanged */
extern int update_critconfig(critconf_t *rec);
extern void addclone_critconfig(char *origin, char *newclone);
extern void dropclone_critconfig(char *drop);
/* 0 deleted, 1 not deleted because other hosts are clones of it, 2 no such record */
/* What delete_critconfig() did. Only CRITDELETE_OK changed anything. */
#define CRITDELETE_OK        0	/* deleted, in memory: update_critconfig() writes it */
#define CRITDELETE_CLONED    1	/* not deleted: other hosts are clones of it */
#define CRITDELETE_NORECORD  2	/* not deleted: there is no record by that key */
extern int delete_critconfig(char *dropkey, int evenifcloned);

#endif

