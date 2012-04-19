/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SENDRESULTS_H__
#define __SENDRESULTS_H__

extern void add_to_sub_queue(myconn_t *rec, char *moduleid);
extern void send_test_results(listhead_t *head, char *collector, int issubmodule);

#endif
