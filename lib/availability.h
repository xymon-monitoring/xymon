/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __REPORTDATA_H__
#define __REPORTDATA_H__

typedef struct {
        time_t starttime;
        time_t duration;
        int color;
        char *cause;
	char *timespec;
        void *next;
} replog_t;
extern replog_t *reploghead;

extern int parse_historyfile(FILE *fd, reportinfo_t *repinfo, char *hostname, char *servicename, time_t fromtime, time_t totime);
#endif

