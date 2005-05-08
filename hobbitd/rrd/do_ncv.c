/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles any message with data in the form                      */
/*     NAME: VALUE                                                            */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ncv_rcsid[] = "$Id: do_ncv.c,v 1.2 2005-05-08 19:35:29 henrik Exp $";

int do_ncv_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char **params = NULL;
	int paridx;
	char dsdef[1024];
	char *l, *eoln, *name, *val;

	sprintf(rrdfn, "%s.rrd", testname);
	sprintf(rrdvalues, "%d", (int)tstamp);

	params = (char **)calloc(8, sizeof(char *));
	params[0] = "rrdcreate";
	params[1] = rrdfn;
	paridx = 1;

	l = strchr(msg, '\n'); if (l) l++;
	while (l) {
		eoln = strchr(l, '\n'); if (eoln) *eoln = '\0';

		name = val = NULL;
		name = strtok(l, " \t:=");
		if (name) val = strtok(NULL, " \t\r");

		if (name && val) {
			char *endptr;

			strtod(val, &endptr);
			if (*endptr == '\0') {
				/* val contains a valid number */
				if (strlen(name) > 19) *(name+19) = '\0'; /* RRD limitation */

				sprintf(dsdef, "DS:%s:DERIVE:600:0:U", name);
				paridx++;
				params = (char **)realloc(params, (7 + paridx)*sizeof(char *));
				params[paridx] = strdup(dsdef);
				params[paridx+1] = NULL;

				sprintf(rrdvalues+strlen(rrdvalues), ":%s", val);
			}
		}

		l = (eoln ? eoln + 1 : NULL);
	}

	if (paridx > 1) {
		params[++paridx] = strdup(rra1);
		params[++paridx] = strdup(rra2);
		params[++paridx] = strdup(rra3);
		params[++paridx] = strdup(rra4);
		params[++paridx] = NULL;

		create_and_update_rrd(hostname, rrdfn, params, NULL);

		for (paridx=2; (params[paridx] != NULL); paridx++)
		xfree(params[paridx]);
	}

	xfree(params);

	return 0;
}

