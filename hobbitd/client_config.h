/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CLIENT_CONFIG_H__
#define __CLIENT_CONFIG_H__

#include "libbbgen.h"

extern int load_client_config(char *configfn);
extern void dump_client_config(void);

extern void clearalertgroups(void);
extern char *getalertgroups(void);
extern void addalertgroup(char *group);

extern int get_cpu_thresholds(void *hinfo, char *classname, 
			      float *loadyellow, float *loadred, int *recentlimit, int *ancientlimit,
			      int *maxclockdiff);

extern int get_disk_thresholds(void *hinfo, char *classname,
				char *fsname,
				long *warnlevel, long *paniclevel,
				int *abswarn, int *abspanic,
				int *ignored, char **group);

extern void get_memory_thresholds(void *hhinfo, char *classname,
				  int *physyellow, int *physred, 
				  int *swapyellow, int *swapred, 
				  int *actyellow, int *actred);

extern int get_paging_thresholds(void *hinfo, char *classname, 
				 int *pagingyellow, int *pagingred);

extern int scan_log(void *hinfo, char *classname, 
		    char *logname, char *logdata, char *section, strbuffer_t *summarybuf);
extern int check_file(void *hinfo, char *classname, 
		      char *filename, char *filedata, char *section, strbuffer_t *summarybuf, off_t *sz, 
		      char **id, int *trackit, int *anyrules);
extern int check_dir(void *hinfo, char *classname, 
		     char *filename, char *filedata, char *section, strbuffer_t *summarybuf, unsigned long *sz, 
		     char **id, int *trackit);

extern int clear_process_counts(void *hinfo, char *classname);
extern void add_process_count(char *pname);
extern char *check_process_count(int *pcount, int *lowlim, int *uplim, int *pcolor, char **id, int *trackit, char **group);

extern int clear_disk_counts(void *hinfo, char *classname);
extern void add_disk_count(char *dname);
extern char *check_disk_count(int *dcount, int *lowlim, int *uplim, int *dcolor, char **group);

extern int clear_port_counts(void *hinfo, char *classname);
extern void add_port_count(char *spname, char *tpname, char *stname);
extern char *check_port_count(int *pcount, int *lowlim, int *uplim, int *pcolor, char **id, int *trackit, char **group);

#endif

