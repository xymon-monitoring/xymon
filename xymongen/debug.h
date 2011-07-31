/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DEBUG_H_
#define __DEBUG_H_

extern int timing;

extern void add_timestamp(const char *msg);
extern void show_timestamps(char **buffer);
extern long total_runtime(void);
extern const char *textornull(const char *text);
extern void dumphosts(host_t *head, char *prefix);
extern void dumpgroups(group_t *head, char *prefix, char *hostprefix);
extern void dumphostlist(hostlist_t *head);
extern void dumpstatelist(state_t *head);
extern void dumpall(xymongen_page_t *head);

#endif

