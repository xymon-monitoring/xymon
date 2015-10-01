/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* This module handles any message with data in the form                      */
/*     NAME: VALUE                                                            */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/* split-ncv added by Charles Goyard November 2006                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ncv_rcsid[] = "$Id$";

int do_ncv_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char **params = NULL;
	int paridx;
	char dsdef[1024];     /* destination DS syntax for rrd engine */
	char *l, *name, *val;
	char *envnam;
	char *dstypes = NULL; /* contain NCV_testname value */
	int split_ncv = 0;
	int dslen;

	snprintf(rrdvalues, sizeof(rrdvalues), "%d", (int)tstamp);
	params = (char **)calloc(1, sizeof(char *));
	paridx = 0;

	/* Get the NCV_* or SPLITNCV_* environment setting */
	envnam = (char *)malloc(9 + strlen(testname) + 1);
	sprintf(envnam, "SPLITNCV_%s", testname);
	l = getenv(envnam);
	if (l) {
		split_ncv = 1;
		dslen = 200;
	}
	else {
		split_ncv = 0;
		dslen = 19;
		setupfn("%s.rrd", testname);
		sprintf(envnam, "NCV_%s", testname);
		l = getenv(envnam);
	}

	if (l) {
		dstypes = (char *)malloc(strlen(l)+3);
		sprintf(dstypes, ",%s,", l);
	}
	xfree(envnam);

	l = strchr(msg, '\n'); if (l) l++;
	while (l && *l && strncmp(l, "@@\n", 3)) {
		name = val = NULL;

		l += strspn(l, " \t\n");
		if (*l) { 
			/* See if this line contains a '=' or ':' sign */
			name = l; 
			l += strcspn(l, ":=\n");
			if (*l) {
				if (( *l == '=') || (*l == ':')) {
					*l = '\0'; l++;
				}
				else {
					/* No marker, so skip this line */
					name = NULL;
				}
			}
			else break;     /* We've hit the end of the message */
		}

		/* Skip any color marker "&COLOR " in front of the ds name */
		if (name && (*name == '&')) {
			name++;
			name += strspn(name, "abcdefghijklmnopqrstuvwxyz");
			name += strspn(name, " \t");
			if (*name == '\0') name = NULL;
		}

		if (name) { 
			val = l + strspn(l, " \t"); 
			/* Find the end of the value string */
			l = val; if ((*l == '-') || (*l == '+')) l++; /* Pass leading sign */
			l += strspn(l, "0123456789.+-eE"); /* and the numbers. */
			if( *val ) {
				int iseol = (*l == '\n');

				*l = '\0'; 
				if (!iseol) {
					/* If extra data after the value, skip to end of line */
					l = strchr(l+1, '\n');
					if (l) l++; 
				}
				else {
					l++;
				}
			}
			else break; /* No value data */
		}

		if (name && val && *val) {
			char *endptr;
			double dummy;

			dummy = strtod(val, &endptr); /* Dont care - we're only interested in endptr */
			if (isspace((int)*endptr) || (*endptr == '\0')) {
				char dsname[250];    /* name of ncv in status message (with space and all) */
				char dskey[252];     /* name of final DS key (stripped)                    */
				char *dstype = NULL; /* type of final DS                                   */
				char *inp;
				int outidx = 0;
				/* val contains a valid number */

				/* rrdcreate(1) says: ds must be in the set [a-zA-Z0-9_] ... */
				for (inp=name,outidx=0; (*inp && (outidx < dslen)); inp++) {
					if ( ((*inp >= 'A') && (*inp <= 'Z')) ||
					     ((*inp >= 'a') && (*inp <= 'z')) ||
					     ((*inp >= '0') && (*inp <= '9'))    ) {
						dsname[outidx++] = *inp;
					}
					/* ... however, for split ncv, we replace anything else  */
					/* with an underscore, compacting successive invalid     */
					/* characters into a single one                          */
					else if (split_ncv && ((outidx == 0) || (dsname[outidx - 1] != '_'))) {
						dsname[outidx++] = '_';
					}
				}

				if ((outidx > 0) && (dsname[outidx-1] == '_')) {
					dsname[outidx-1] = '\0';
				}
				else {
					dsname[outidx] = '\0';
				}

				snprintf(dskey, sizeof(dskey), ",%s:", dsname);
				if (split_ncv) setupfn2("%s,%s.rrd", testname, dsname);

				if (dstypes) {
					dstype = strstr(dstypes, dskey);
					if (!dstype) {
						strcpy(dskey, ",*:");
						dstype = strstr(dstypes, dskey);
					}
				}

				if (dstype) { /* if ds type is forced */
					char *p;

					dstype += strlen(dskey);
					p = strchr(dstype, ','); if (p) *p = '\0';
					if(split_ncv) {
						snprintf(dsdef, sizeof(dsdef), "DS:lambda:%s:600:U:U", dstype);
					}
					else {
						snprintf(dsdef, sizeof(dsdef), "DS:%s:%s:600:U:U", dsname, dstype);
					}
					if (p) *p = ',';
				}
				else { /* nothing specified in the environnement, and no '*:' default */
					if(split_ncv) {
						strcpy(dsdef, "DS:lambda:DERIVE:600:U:U");
					}
					else {
						snprintf(dsdef, sizeof(dsdef), "DS:%s:DERIVE:600:U:U", dsname);
					}
				}

				if (!dstype || (strncasecmp(dstype, "NONE", 4) != 0)) { /* if we have something */
					params[paridx] = strdup(dsdef);
					paridx++;
					params = (char **)realloc(params, (1 + paridx)*sizeof(char *));
					params[paridx] = NULL;
					snprintf(rrdvalues+strlen(rrdvalues), sizeof(rrdvalues)-strlen(rrdvalues), ":%s", val);
				}
			}

			if (split_ncv && (paridx > 0)) {
				create_and_update_rrd(hostname, testname, classname, pagepaths, params, NULL);

				/* We've created one RRD, so reset the params for the next one */
				for (paridx=0; (params[paridx] != NULL); paridx++) xfree(params[paridx]);
				paridx = 0;
				params[0] = NULL;
				snprintf(rrdvalues, sizeof(rrdvalues), "%d", (int)tstamp);
			}
		}
	} /* end of while */

	if (!split_ncv && params[0]) create_and_update_rrd(hostname, testname, classname, pagepaths, params, NULL);

	for (paridx=0; (params[paridx] != NULL); paridx++) xfree(params[paridx]);
	xfree(params);
	if (dstypes) xfree(dstypes);

	return 0;
}

