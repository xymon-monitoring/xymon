/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char external_rcsid[] = "$Id: do_external.c,v 1.2 2005-02-06 08:49:02 henrik Exp $";


int do_external_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	FILE *fd;
	char fn[PATH_MAX];
	pid_t childpid;
	enum { R_DEFS, R_FN, R_DATA, R_NEXT } pstate;

	sprintf(fn, "%s/rrd_msg_%d", xgetenv("BBTMP"), (int) getpid());
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot create temp file %s\n", fn);
		return 1;
	}
	if (fwrite(msg, strlen(msg), 1, fd) == 0) {
		errprintf("Error writing to file %s: %s\n", fn, strerror(errno));
		return 2;
	}
	fclose(fd);

	childpid = fork();
	if (childpid == 0) {
		FILE *extfd;
		char extcmd[2*PATH_MAX];
		char l[MAXMSG];
		char *p;
		char **params = NULL;
		int paridx = 1;
		
		/* Now call the external helper */
		sprintf(extcmd, "%s %s %s %s", exthandler, hostname, testname, fn);
		extfd = popen(extcmd, "r");
		if (extfd) {
			pstate = R_DEFS;

			while (fgets(l, sizeof(l), extfd)) {
				p = strchr(l, '\n'); if (p) *p = '\0';
				if (strlen(l) == 0) continue;

				if (pstate == R_NEXT) {
					/* After doing one set of data, allow script to re-use the same DS defs */
					if (strncasecmp(l, "DS:", 3) == 0) {
						/* New DS definitions, scratch the old ones */
						pstate = R_DEFS;

						for (paridx=2; (params[paridx] != NULL); paridx++) {
							if (strncasecmp(params[paridx], "DS:", 3) == 0) xfree(params[paridx]);
						}
						xfree(params);
						params = NULL;
						pstate = R_DEFS;
					}
					else pstate = R_FN;
				}

				switch (pstate) {
				  case R_DEFS:
					if (params == NULL) {
						params = (char **)malloc(8 * sizeof(char *));
						params[0] = "rrdcreate";
						params[1] = rrdfn;
						paridx = 1;
					}

					if (strncasecmp(l, "DS:", 3) == 0) {
						/* Dataset definition */
						paridx++;
						params = (char **)realloc(params, (7 + paridx)*sizeof(char *));
						params[paridx] = strdup(l);
						break;
					}
					else {
						/* No more DS defs - put in the RRA's last. */
						params[++paridx] = rra1;
						params[++paridx] = rra2;
						params[++paridx] = rra3;
						params[++paridx] = rra4;
						params[++paridx] = NULL;
						pstate = R_FN;
					}
					/* Fall through */
				  case R_FN:
					strcpy(rrdfn, l);
					pstate = R_DATA;
					break;

				  case R_DATA:
					sprintf(rrdvalues, "%d:%s", (int)tstamp, l);
					create_and_update_rrd(hostname, rrdfn, params, update_params);
					pstate = R_NEXT;
					break;

				  case R_NEXT:
					/* Should not happen */
					break;
				}
			}
			pclose(extfd);
		}
		else {
			errprintf("Pipe open of RRD handler failed: %s\n", strerror(errno));
		}

		unlink(fn);
		exit(0);
	}
	else if (childpid > 0) {
		/* Parent continues */
	}
	else {
		errprintf("Fork failed in RRD handler: %s\n", strerror(errno));
		unlink(fn);
		return 3;
	}

	return 0;
}

