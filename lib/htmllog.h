/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTMLLOG_H__
#define __HTMLLOG_H__

#include <stdio.h>

enum histbutton_t { HIST_TOP, HIST_BOTTOM, HIST_NONE };

extern enum histbutton_t histlocation;

extern void generate_html_log(char *hostname, char *displayname, char *service, char *ip, 
		       int color, char *sender, char *flags, 
		       time_t logtime, char *timesincechange, 
		       char *firstline, char *restofmsg, char *ackmsg, 
		       int is_history, int wantserviceid, int htmlfmt, int hobbitd,
		       FILE *output);

#endif
