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

typedef struct pagelist_t {
	char *pagename;
	struct pagelist_t *next;
} pagelist_t;

typedef struct namelist_t {
	char ip[16];
	char *bbhostname;	/* Name for item 2 of bb-hosts */
	char *clientname;	/* CLIENT: tag - host alias */
	char *displayname;	/* NAME: tag - display purpose only */
	char *downtime;
	struct pagelist_t *page;
	void *data;		/* Misc. data supplied by the user of this library function */
	struct namelist_t *next;
} namelist_t;

extern namelist_t *load_hostnames(char *bbhostsfn, int fqdn);
extern char *knownhost(char *filename, char *hostip, int ghosthandling, int *maybedown);
extern namelist_t *hostinfo(char *hostname);

#endif

