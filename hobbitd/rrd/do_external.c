/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char external_rcsid[] = "$Id: do_external.c,v 1.21 2008-01-03 10:13:50 henrik Exp $";

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
		int paridx = 0;
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
						if (params) {
							for (paridx=0; (params[paridx] != NULL); paridx++) 
								xfree(params[paridx]);
							xfree(params);
							params = NULL;
						}
						pstate = R_DEFS;
					}
					else pstate = R_FN;
				}

				switch (pstate) {
				  case R_DEFS:
					if (params == NULL) {
						params = (char **)calloc(1, sizeof(char *));
						paridx = 0;
					}

					if (strncasecmp(STRBUF(inbuf), "DS:", 3) == 0) {
						/* Dataset definition */
						params[paridx] = strdup(STRBUF(inbuf));
						paridx++;
						params = (char **)realloc(params, (1 + paridx)*sizeof(char *));
						params[paridx] = NULL;
						break;
					}
					else {
						/* No more DS defs */
						pstate = R_FN;
					}
					/* Fall through */
				  case R_FN:
					setupfn("%s", STRBUF(inbuf));
					pstate = R_DATA;
					break;

				  case R_DATA:
					snprintf(rrdvalues, sizeof(rrdvalues)-1, "%d:%s", (int)tstamp, STRBUF(inbuf));
					rrdvalues[sizeof(rrdvalues)-1] = '\0';
					create_and_update_rrd(hostname, testname, params, NULL);
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
			for (paridx=0; (params[paridx] != NULL); paridx++) xfree(params[paridx]);
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

