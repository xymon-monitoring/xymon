/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADHOSTS_H__
#define __LOADHOSTS_H__

extern void load_hostnames(char *bbhostsfn, int fqdn);
extern char *knownhost(char *filename, char *srcip, int ghosthandling, int *maybedown);
extern char *hostdispname(char *hostname);
extern char *hostpagename(char *hostname);

#endif

