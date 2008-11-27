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

static char trends_rcsid[] = "$Id: do_counts.c,v 1.5 2006/06/09 22:23:49 henrik Rel $";

/* 
 * This module was inspired by a mail from Stef Coene:
 *
 * Date: Wed, 17 Jan 2007 14:04:29 +0100
 * From: Stef Coene
 * Subject: Re: [hobbit] hobbit monitoring
 * 
 * On Wednesday 17 January 2007 10:38, Stef Coene wrote:
 * > Same for trending, hobbit is not powerfull enough for our needs.  And we
 * > don't have the C knowledge to change hobbit.  One of the problems is that
 * > there is 1 rrd file / check.  But we have checks where the number on 1 page
 * > we want to graph can change over time.
 * Just wondering, how hard would it be to create an extra channel for trending?
 * So you can use the bb client to send "numbers" to the hobbit server together
 * with some extra control information.
 * 
 * bb <bb server> trends <server name>
 * <rrd file name> <ds name> <number> <options>
 *
 */

/*
 * To use this, send a "data" message to hobbit like this:
 *
 *    data $MACHINE.trends
 *    [filename.rrd]
 *        DS-definition1 VALUE2
 *        DS-definition2 VALUE2
 *
 * E.g. to create/update a custom RRD file "weather.rrd" with two 
 * GAUGE datasets "temp" and "wind", with current values "21" and 
 * "8" respectively, send this message:
 *
 *    [weather.rrd]
 *    DS:temp:GAUGE:600:0:U 21
 *    DS:wind:GAUGE:600:0:U 8
 */

static int do_trends_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *boln, *eoln, *p;
	char *rrdfn = NULL;
	int dscount = 0;
	char **creparams;

	creparams = (char **)calloc(7, sizeof(char *));

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		if (*boln == '[') {
			/* Flush the current RRD file */
			if (rrdfn && (dscount > 0)) {
				/* Setup the static RRD create-parameters */
				creparams[0] = "rrdcreate";
				creparams[1] = rrdfn;
				creparams[2+dscount] = rra1; dscount++;
				creparams[2+dscount] = rra2; dscount++;
				creparams[2+dscount] = rra3; dscount++;
				creparams[2+dscount] = rra4; dscount++;
				creparams[2+dscount] = NULL; dscount++;

				create_and_update_rrd(hostname, rrdfn, creparams, NULL);

				xfree(rrdfn); rrdfn = NULL;
				creparams = (char **)realloc(creparams, 7*sizeof(char *));
				memset(creparams, 0, 7*sizeof(char *));
				dscount = 0;
			}

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
				creparams = (char **)realloc(creparams, (7+dscount)*sizeof(char **));
				creparams[1+dscount] = boln;
				sprintf(rrdvalues+strlen(rrdvalues), ":%s", valptr);
			}
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	/* Do the last RRD set */
	if (rrdfn && (dscount > 0)) {
		/* Setup the static RRD create-parameters */
		creparams[0] = "rrdcreate";
		creparams[1] = rrdfn;
		creparams[2+dscount] = rra1; dscount++;
		creparams[2+dscount] = rra2; dscount++;
		creparams[2+dscount] = rra3; dscount++;
		creparams[2+dscount] = rra4; dscount++;
		creparams[2+dscount] = NULL; dscount++;

		create_and_update_rrd(hostname, rrdfn, creparams, NULL);
	}

	if (rrdfn) xfree(rrdfn);
	xfree(creparams);

	return 0;
}

