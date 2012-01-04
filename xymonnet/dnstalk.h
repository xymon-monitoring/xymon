/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DNSTALK_H__
#define __DNSTALK_H__

extern void dns_library_init(void);
extern int dns_start_query(myconn_t *rec, char *targetserver);
extern int dns_add_active_fds(listhead_t *activelist, int *maxfd, fd_set *fdread, fd_set *fdwrite);
extern void dns_process_active(listhead_t *activelist, fd_set *fdread, fd_set *fdwrite);
extern void dns_finish_queries(listhead_t *activelist);
extern void dns_lookup(myconn_t *rec);

#endif

