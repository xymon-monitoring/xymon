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

#ifndef __DEBUG_H_
#define __DEBUG_H_

extern int debug;

#ifdef DEBUG
extern void dprintf(const char *fmt, ...);
#else
/* We want dprintf completely optimized away if not -DDEBUG. Thanks, Linus */
#define dprintf(fmt,arg...) \
	do { } while (0)
#endif

extern const char *textornull(const char *text);
extern void dumplinks(link_t *head);
extern void dumphosts(host_t *head, char *prefix);
extern void dumpgroups(group_t *head, char *prefix, char *hostprefix);
extern void dumphostlist(hostlist_t *head);
extern void dumpstatelist(state_t *head);
extern void dumpall(bbgen_page_t *head);

#endif

