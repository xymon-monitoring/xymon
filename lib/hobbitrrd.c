/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for working with LARRD graphs.                        */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitrrd.c,v 1.2 2004-11-13 22:33:46 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "libbbgen.h"
#include "version.h"

#include "bblarrd.h"

static larrdsvc_t *larrdsvcs = NULL;

/*
 * Define the mapping between BB columns and LARRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
static void larrd_setup(void)
{
	static int setup_done = 0;
	char *lenv, *ldef, *p;
	int lcount;
	larrdsvc_t *lrec;

	if (setup_done) return;

	getenv_default("LARRDS", "cpu=la,http,conn,fping=conn,ftp,ssh,telnet,nntp,pop,pop-2,pop-3,pop2,pop3,smtp,imap,disk,vmstat,memory,iostat,netstat,citrix,bbgen,bbtest,bbproxy,time=ntpstat", NULL);

	lenv = strdup(getenv("LARRDS")); lcount = 0;
	p = lenv; do { lcount++; p = strchr(p+1, ','); } while (p);
	larrdsvcs = (larrdsvc_t *)calloc(sizeof(larrdsvc_t), (lcount+1));

	lrec = larrdsvcs; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, '=');
		if (p) {
			*p = '\0'; 
			lrec->bbsvcname = strdup(ldef);
			lrec->larrdsvcname = strdup(p+1);
		}
		else {
			lrec->bbsvcname = lrec->larrdsvcname = strdup(ldef);
		}

		if (strcmp(ldef, "disk") == 0) {
			lrec->larrdpartname = "disk_part";
		}

		ldef = strtok(NULL, ",");
		lrec++;
	}
	free(lenv);

	setup_done = 1;
}


larrdsvc_t *find_larrd(char *service, char *flags)
{
	larrdsvc_t *lrec;

	larrd_setup();

	if (strchr(flags, 'R')) {
		/* Dont do LARRD for reverse tests, since they have no data */
		return NULL;
	}

	lrec = larrdsvcs; while (lrec->bbsvcname && strcmp(lrec->bbsvcname, service)) lrec++;
	return (lrec->bbsvcname ? lrec : NULL);
}

