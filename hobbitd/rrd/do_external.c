/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char external_rcsid[] = "$Id: do_external.c,v 1.17 2006-07-20 16:06:41 henrik Exp $";

int do_external_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	pid_t childpid;

	dbgprintf("-> do_external(%s, %s)\n", hostname, testname);

	childpid = fork();
	if (childpid == 0) {
		FILE *fd;
		char fn[PATH_MAX];
		enum { R_DEFS, R_FN, R_DATA, R_NEXT } pstate;
		FILE *extfd;
		char extcmd[2*PATH_MAX];
		strbuffer_t *inbuf;
		char *p;
		char **params = NULL;
		int paridx = 1;
		pid_t mypid = getpid();
		
		MEMDEFINE(fn); MEMDEFINE(extcmd);

		sprintf(fn, "%s/rrd_msg_%d", xgetenv("BBTMP"), (int) getpid());
		dbgprintf("%09d : Saving msg to file %s\n", (int)mypid, fn);

		fd = fopen(fn, "w");
		if (fd == NULL) {
			errprintf("Cannot create temp file %s\n", fn);
			exit(1);
		}
		if (fwrite(msg, strlen(msg), 1, fd) != 1) {
			errprintf("Error writing to file %s: %s\n", fn, strerror(errno));
			exit(1) ;
		}
		if (fclose(fd)) errprintf("Error closing file %s: %s\n", fn, strerror(errno));

		inbuf = newstrbuffer(0);

		/* Now call the external helper */
		sprintf(extcmd, "%s %s %s %s", exthandler, hostname, testname, fn);
		dbgprintf("%09d : Calling helper script %s\n", (int)mypid, extcmd);
		extfd = popen(extcmd, "r");
		if (extfd) {
			pstate = R_DEFS;
			initfgets(extfd);

			while (unlimfgets(inbuf, extfd)) {
				p = strchr(STRBUF(inbuf), '\n'); if (p) *p = '\0';
				dbgprintf("%09d : Helper input '%s'\n", (int)mypid, STRBUF(inbuf));
				if (STRBUFLEN(inbuf) == 0) continue;

				if (pstate == R_NEXT) {
					/* After doing one set of data, allow script to re-use the same DS defs */
					if (strncasecmp(STRBUF(inbuf), "DS:", 3) == 0) {
						/* New DS definitions, scratch the old ones */
						pstate = R_DEFS;

						if (params) {
							for (paridx=2; (params[paridx] != NULL); paridx++) 
								xfree(params[paridx]);
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
						params = (char **)calloc(8, sizeof(char *));
						params[0] = "rrdcreate";
						params[1] = rrdfn;
						paridx = 1;
					}

					if (strncasecmp(STRBUF(inbuf), "DS:", 3) == 0) {
						/* Dataset definition */
						paridx++;
						params = (char **)realloc(params, (7 + paridx)*sizeof(char *));
						params[paridx] = strdup(STRBUF(inbuf));
						params[paridx+1] = NULL;
						break;
					}
					else {
						/* No more DS defs - put in the RRA's last. */
						params[++paridx] = strdup(rra1);
						params[++paridx] = strdup(rra2);
						params[++paridx] = strdup(rra3);
						params[++paridx] = strdup(rra4);
						params[++paridx] = NULL;
						pstate = R_FN;
					}
					/* Fall through */
				  case R_FN:
					strncpy(rrdfn, STRBUF(inbuf), sizeof(rrdfn)-1);
					rrdfn[sizeof(rrdfn)-1] = '\0';
					pstate = R_DATA;
					break;

				  case R_DATA:
					snprintf(rrdvalues, sizeof(rrdvalues)-1, "%d:%s", (int)tstamp, STRBUF(inbuf));
					rrdvalues[sizeof(rrdvalues)-1] = '\0';
					create_and_update_rrd(hostname, rrdfn, params, NULL);
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

		if (params) {
			for (paridx=2; (params[paridx] != NULL); paridx++) xfree(params[paridx]);
			xfree(params);
		}

		dbgprintf("%09d : Unlinking temp file\n", (int)mypid);
		unlink(fn);
		freestrbuffer(inbuf);

		exit(0);
	}
	else if (childpid > 0) {
		/* Parent continues */
	}
	else {
		errprintf("Fork failed in RRD handler: %s\n", strerror(errno));
	}

	dbgprintf("<- do_external(%s, %s)\n", hostname, testname);
	return 0;
}

