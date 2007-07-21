/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles custom "trends" data.                                  */
/*                                                                            */
/* Copyright (C) 2007 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char trends_rcsid[] = "$Id: do_trends.c,v 1.2 2007-07-21 09:44:37 henrik Exp $";

/* 
 * This module was inspired by a mail from Stef Coene:
 *
 * ---------------------------------------------------------------------------
 * Date: Wed, 17 Jan 2007 14:04:29 +0100
 * From: Stef Coene
 * Subject: Re: [hobbit] hobbit monitoring
 * 
 * Just wondering, how hard would it be to create an extra channel for trending?
 * So you can use the bb client to send "numbers" to the hobbit server together
 * with some extra control information.
 * 
 * bb <bb server> trends <server name>
 * <rrd file name> <ds name> <number> <options>
 * -----------------------------------------------------------------------------
 *
 * Instead of a dedicated Hobbit channel for this, I decided to use the 
 * existing "data" message type. To use this, send a "data" message to 
 * hobbit formatted like this:
 *
 *    data $MACHINE.trends
 *    [filename.rrd]
 *    DS-definition1 VALUE2
 *    DS-definition2 VALUE2
 *
 * E.g. to create/update a custom RRD file "weather.rrd" with two 
 * GAUGE datasets "temp" and "wind", with current values "21" and 
 * "8" respectively, send this message:
 *
 *    [weather.rrd]
 *    DS:temp:GAUGE:600:0:U 21
 *    DS:wind:GAUGE:600:0:U 8
 */

static void do_trends_rrd_flush(char *hostname, char *rrdfn, char **creparams, int dscount)
{
	if (rrdfn && (dscount > 0)) {
		/* Setup the static RRD create-parameters */
		creparams[0] = "rrdcreate";
		creparams[1] = rrdfn;
		creparams[2+dscount] = NULL; dscount++;

		create_and_update_rrd(hostname, rrdfn, creparams, NULL);
	}
}

static int do_trends_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *boln, *eoln, *p;
	char *rrdfn = NULL;
	int dscount = 0;
	char **creparams;

	creparams = (char **)calloc(3, sizeof(char *));

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		if (*boln == '[') {
			/* Flush the current RRD file */
			do_trends_rrd_flush(hostname, rrdfn, creparams, dscount);

			xfree(rrdfn); rrdfn = NULL;
			creparams = (char **)realloc(creparams, 3*sizeof(char *));
			dscount = 0;

			/* Get the RRD filename */
			p = strchr(boln+1, ']'); if (p) *p = '\0';
			rrdfn = strdup(boln+1);

			/* And setup the initial rrdvalues string */
			sprintf(rrdvalues, "%d", (int)tstamp);
		}
		else if (strncmp(boln, "DS:", 3) == 0) {
			char *valptr = boln + strcspn(boln, " \t");

			if ((*valptr == ' ') || (*valptr == '\t')) {
				*valptr = '\0'; valptr += 1 + strspn(valptr+1, " \t");
				dscount++;
				creparams = (char **)realloc(creparams, (3+dscount)*sizeof(char **));
				creparams[1+dscount] = boln;
				sprintf(rrdvalues+strlen(rrdvalues), ":%s", valptr);
			}
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	/* Do the last RRD set */
	do_trends_rrd_flush(hostname, rrdfn, creparams, dscount);

	if (rrdfn) xfree(rrdfn);
	xfree(creparams);

	return 0;
}

