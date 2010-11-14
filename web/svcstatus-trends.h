/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SVCSTATUS_TRENDS_H__
#define __SVCSTATUS_TRENDS_H__

extern char *generate_trends(char *hostname, time_t starttime, time_t endtime);

#endif

