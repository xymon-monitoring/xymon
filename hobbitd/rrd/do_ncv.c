/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles any message with data in the form                      */
/*     NAME: VALUE                                                            */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/* split-ncv added by Charles Goyard November 2006                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ncv_rcsid[] = "$Id: do_ncv.c,v 1.10 2006-06-09 22:23:49 henrik Exp $";

int do_ncv_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char **params = NULL;
	int paridx;
	char dsdef[1024];     /* destination DS syntax for rrd engine */
	char *l, *name, *val;
	char *envnam;
	char *dstypes = NULL; /* contain NCV_testname value*/
	int split_ncv = 0;
	int dslen;
	sprintf(rrdvalues, "%d", (int)tstamp);
	params = (char **)calloc(8, sizeof(char *));
	params[0] = "rrdcreate";
	paridx = 1;

	envnam = (char *)malloc(9 + strlen(testname) + 1);
	sprintf(envnam, "SPLITNCV_%s", testname);
	l = getenv(envnam);
	if (l) {
		split_ncv = 1;
		dslen = 200;
	}
	else {
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
			else break;	/* We've hit the end of the message */
		}

		if (name) { 
			val = l + strspn(l, " \t"); 
			l = val + strspn(val, "0123456789."); 
			if( *l ) { 
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
			else break;
		}

		if (name && val && *val) {
			char *endptr;

			strtod(val, &endptr);
			if (isspace((int)*endptr) || (*endptr == '\0')) {
				char dsname[250];    /* name of ncv in status message (with space and al) */
				char dskey[252];     /* name of final DS key (stripped)                   */
				char *dstype = NULL; /* type of final DS                                  */
				char *inp;
				int outidx;
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
					else if (split_ncv && (dsname[outidx - 1] != '_')) {
						dsname[outidx++] = '_';
					}
				}
				if(dsname[outidx-1] == '_') {
					dsname[outidx-1] = '\0';
				}
				else {
				dsname[outidx] = '\0';
				}
				sprintf(dskey, ",%s:", dsname);
				if(split_ncv) {
					/* setupfn("%s,%s.rrd", testname, dsname); */
					snprintf(rrdfn, sizeof(rrdfn)-1, "%s,%s.rrd", testname,dsname);
					rrdfn[sizeof(rrdfn)-1] = '\0';
					
					params[1] = rrdfn;
					paridx = 1;
				}

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
						sprintf(dsdef, "DS:lambda:%s:600:0:U", dstype);
					}
					else {
					sprintf(dsdef, "DS:%s:%s:600:0:U", dsname, dstype);
					}
					if (p) *p = ',';
				}
				else { /* nothing specified in the environnement, and no '*:' default */
					if(split_ncv) {
						strcpy(dsdef, "DS:lambda:DERIVE:600:0:U");
					}
				else {
					sprintf(dsdef, "DS:%s:DERIVE:600:0:U", dsname);
				}
				}

				if (!dstype || (strncasecmp(dstype, "NONE", 4) != 0)) { /* if we have something */
					paridx++;
					params = (char **)realloc(params, (7 + paridx)*sizeof(char *));
					params[paridx] = strdup(dsdef);
					params[paridx+1] = NULL;
					sprintf(rrdvalues+strlen(rrdvalues), ":%s", val);
				}
			}
			
			if(split_ncv && (paridx > 1)) {
				int i;
				params[++paridx] = strdup(rra1);
				params[++paridx] = strdup(rra2);
				params[++paridx] = strdup(rra3);
				params[++paridx] = strdup(rra4);

				if(has_trackmax(testname)) {
					params = (char **)realloc(params, (11 + paridx)*sizeof(char *));
					params[++paridx] = strdup(rra5);
					params[++paridx] = strdup(rra6);
					params[++paridx] = strdup(rra7);
					params[++paridx] = strdup(rra8);
				}

				params[++paridx] = NULL;
				create_and_update_rrd(hostname, rrdfn, params, NULL);
				for(i = 2 ; i<paridx ; i++) {
					params[i] = NULL;
				}
				sprintf(rrdvalues, "%d", (int)tstamp);
		}
	}

	} /* end of while */

	if (split_ncv) {
		for (paridx=2; (params[paridx] != NULL); paridx++) {
			xfree(params[paridx]);
		}
	}
	else if(paridx > 1) {
		params[++paridx] = strdup(rra1);
		params[++paridx] = strdup(rra2);
		params[++paridx] = strdup(rra3);
		params[++paridx] = strdup(rra4);

		if(has_trackmax(testname)) {
			params = (char **)realloc(params, (11 + paridx)*sizeof(char *));
			params[++paridx] = strdup(rra5);
			params[++paridx] = strdup(rra6);
			params[++paridx] = strdup(rra7);
			params[++paridx] = strdup(rra8);
		}

		params[++paridx] = NULL;
		create_and_update_rrd(hostname, rrdfn, params, NULL);
		for (paridx=2; (params[paridx] != NULL); paridx++)
		xfree(params[paridx]);
	}

	xfree(params);
	if (dstypes) xfree(dstypes);

	return 0;
}

