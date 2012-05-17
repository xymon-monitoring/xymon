/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SECTIONS_H__
#define __SECTIONS_H__

typedef struct sectlist_t {
	char *sname;
	char *sdata;
	char *nextsectionrestoreptr, *sectdatarestoreptr;
	char nextsectionrestoreval, sectdatarestoreval;
	struct sectlist_t *next;
} sectlist_t;

extern sectlist_t *defsecthead;

extern void nextsection_r_done(void *secthead);
extern void splitmsg_r(char *clientdata, sectlist_t **secthead);
extern void splitmsg_done(void);
extern void splitmsg(char *clientdata);
extern char *nextsection_r(char *clientdata, char **name, void **current, void **secthead);
extern char *nextsection(char *clientdata, char **name);
extern char *getdata(char *sectionname);

#endif

