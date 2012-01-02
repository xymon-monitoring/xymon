/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TCPHTTP_H__
#define __TCPHTTP_H__

enum xymon_httpver_t { HTTPVER_ANY, HTTPVER_10, HTTPVER_11 };

extern char **build_http_dialog(char *testspec);
#endif

