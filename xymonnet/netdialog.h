/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __NETDIALOG_H__
#define __NETDIALOG_H__

extern void load_protocols(char *fn);
extern char **net_dialog(char *testspec, myconn_netparams_t *netparams, enum net_test_options_t *options, void *hostinfo);

#endif

