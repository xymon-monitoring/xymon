/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for xymonnet collector                               */
/*                                                                            */
/* Copyright (C) 2009-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
 * What I'd like to do here is to have a very generic string-matching engine.
 * It would go something like
 *   NET <testspec> <keyword> <pattern>
 * E.g. to test that the "smtp" test returns the expected string
 *   NET smtp Status OK
 * Or the content of a web page:
 *   NET https://webmail.hswn.dk/squirrelmail/src/login.php HTTPbody HSWN
 */

static char netcollect_rcsid[] = "$Id: snmpcollect.c 6712 2011-07-31 21:01:52Z storner $";

void handle_netcollect_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	void *ns1var, *ns1sects;
	char *onetest, *testname;
	char fromline[1024], msgline[1024];
	strbuffer_t *summary = newstrbuffer(0);

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	onetest = nextsection_r(clientdata, &testname, &ns1var, &ns1sects);
	while (onetest) {
		int color, rulecolor, anyrules;
		char *groups;

		if (strcmp(testname, "proxy") == 0) {
			/*
			 * Data was forwarded through a proxy - skip this section.
			 * We dont want a "proxy" status for all hosts with network tests.
			 */
			goto sectiondone;
		}

		/* Match the test data against the configuration */
		anyrules = 1;
		color = COL_GREEN;
		clearalertgroups();
		clearstrbuffer(summary);

		rulecolor = COL_CLEAR;
		if (rulecolor > color) color = rulecolor;

		/* Generate the status message */
		groups = getalertgroups();
		init_status(color);
		if (groups) sprintf(msgline, "status/group:%s ", groups); else strcpy(msgline, "status ");
		addtostatus(msgline);
		sprintf(msgline, "%s.%s %s %s\n", hostname, testname, colorname(color), ctime(&timestamp));
		addtostatus(msgline);
		if (STRBUFLEN(summary) > 0) {
			addtostrstatus(summary);
			addtostatus("\n");
		}
		// addtostatus(onemib);
		addtostatus(onetest);
		finish_status();

sectiondone:
		onetest = nextsection_r(NULL, &testname, &ns1var, &ns1sects);
	}
	nextsection_r_done(ns1sects);

	freestrbuffer(summary);
}

