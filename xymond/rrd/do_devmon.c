/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module for Devmon                                        */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2008 Buchan Milne                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char devmon_rcsid[] = "$Id $";

int do_devmon_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
#define MAXCOLS 20
	char *devmon_params[MAXCOLS+7] = { NULL, };

	char *eoln, *curline;
	static int ptnsetup = 0;
	static pcre2_code *inclpattern = NULL;
	static pcre2_code *exclpattern = NULL;
	int in_devmon = 1;
	int metrics_block = 0;	/* current block was opened by XYMON METRICS, not the legacy banner */
	int numds = 0;
	char *rrdbasename;
	char *ownedbasename = NULL;	/* xstrdup'ed fallback name, freed here; rrdbasename otherwise points into msg */
	int lineno = 0;
	strbuffer_t *thrspec = newstrbuffer(0);	/* the current block's THRESHOLD relations */

	rrdbasename = NULL;
	curline = msg;
	while (curline)  {
		char *fsline = NULL;
		char *p;
		char *columns[MAXCOLS];
		int columncount;
		char *ifname = NULL;
		int pused = -1;
		int wanteddisk = 1;
		long long aused = 0;
		char *dsval;
		int i;
		int rrdvalused;

		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';
		lineno++;

		/* Tolerate CRLF messages: values and banner names must not
		 * carry a trailing CR into RRD updates or filenames. */
		i = strlen(curline);
		if (i && (curline[i-1] == '\r')) curline[i-1] = '\0';

		if(!strncmp(curline, "<!--DEVMON RRD: ",16)) {
			char *slash;
			/* A banner carrying its own "-->" is an empty, self-closed
			 * block: it must not leave the block open and consume the
			 * rest of the status text as instance data. */
			int selfclosed = (strstr(curline, "-->") != NULL);

			in_devmon = (selfclosed ? 1 : 0);
			metrics_block = 0;
			/* A new block never inherits the previous block's creation
			 * params: a block without its own DS line writes nothing. */
			for (i = 0; (devmon_params[i]); i++) { xfree(devmon_params[i]); devmon_params[i] = NULL; }
			rrdbasename = strtok(curline+16," ");
			if (rrdbasename == NULL) {
				if (ownedbasename) xfree(ownedbasename);
				ownedbasename = rrdbasename = xstrdup(testname);
			}
			/* The banner name becomes an RRD filename prefix; setupfn2()
			 * only sanitizes the instance part, so strip path separators
			 * here - devmon's own names never contain them. */
			while ((slash = strchr(rrdbasename, '/')) != NULL) *slash = ',';
			dbgprintf("DEVMON: changing testname from %s to %s\n",testname,rrdbasename);
			numds = 0;
			fsidx_set_units(NULL);
			fsidx_set_dsnames(NULL);
			fsidx_set_heartbeats(NULL);
			fsidx_set_thresholds(NULL);
			clearstrbuffer(thrspec);
			goto nextline;
		}
		if(!strncmp(curline, XYMON_METRICS_MARKER, strlen(XYMON_METRICS_MARKER))) {
			/* Same block format as the devmon banner. The name is optional
			 * (defaults to the test); the shared marker helper handles the
			 * (un)named / self-closed forms identically to the display parser
			 * (lib/xymonmarkers.c). A named block's name is [A-Za-z0-9_-]. */
			int selfclosed = (strstr(curline, "-->") != NULL);
			char *name = NULL;

			if (xymon_metrics_marker(curline, &name)) {
				char *slash;

				/* Self-closed one-line banner: an empty block. */
				in_devmon = (selfclosed ? 1 : 0);
				metrics_block = (selfclosed ? 0 : 1);
				for (i = 0; (devmon_params[i]); i++) { xfree(devmon_params[i]); devmon_params[i] = NULL; }
				if (ownedbasename) xfree(ownedbasename);
				ownedbasename = (name ? name : xstrdup(testname));
				rrdbasename = ownedbasename;
				while ((slash = strchr(rrdbasename, '/')) != NULL) *slash = ',';
				dbgprintf("METRICS: changing testname from %s to %s\n",testname,rrdbasename);
				numds = 0;
				fsidx_set_units(NULL);
				fsidx_set_dsnames(NULL);
				fsidx_set_heartbeats(NULL);
				fsidx_set_thresholds(NULL);
				clearstrbuffer(thrspec);
			}
			else {
				dbgprintf("METRICS: not a valid marker, skipping\n");
			}
			goto nextline;
		}
		if(in_devmon == 0 && !strncmp(curline, "-->",3)) {
			in_devmon = 1;
			goto nextline;
		}
		if (in_devmon != 0 ) goto nextline;

		for (columncount=0; (columncount<MAXCOLS); columncount++) columns[columncount] = "";
		fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, " ");
		while (p && (columncount < MAXCOLS)) { columns[columncount++] = p; p = strtok(NULL, " "); }

		/* DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U */
		if (!strncmp(curline, "DS:",3)) {
			strbuffer_t *unitspec = newstrbuffer(0);
			strbuffer_t *dsnspec = newstrbuffer(0);
			strbuffer_t *hbspec = newstrbuffer(0);
			int startds = numds;

			dbgprintf("Looking for DS definitions in %s\n",curline);
			while ( numds < MAXCOLS) {
				char *spec, *cp, *unit = NULL;
				int ncolon = 0;

				dbgprintf("Seeing if column %d that has %s is a DS\n",numds,columns[numds]);
				if (strncmp(columns[numds],"DS:",3)) break;
				spec = xstrdup(columns[numds]);
				/* A DS spec may declare a unit as an optional 7th
				 * colon field (DS:name:GAUGE:600:0:U:ms). rrdtool
				 * accepts only the 6-field spec, so cut the suffix
				 * before it reaches rrdcreate - and record the unit
				 * in the fileset index, where the renderer looks. */
				for (cp = spec; (*cp); cp++) {
					if (*cp != ':') continue;
					if (++ncolon == 6) { *cp = '\0'; unit = cp+1; break; }
				}
				if (unit && *unit) {
					char *dsname = spec + 3;
					char *dsend = strchr(dsname, ':');
					char *uc;
					int unitok = (strlen(unit) <= 15);

					/* Unit syntax: printable, no ':' ',' or blank -
					 * anything else would corrupt the index spec */
					for (uc = unit; (unitok && *uc); uc++) {
						if (!isprint((unsigned char)*uc) || (*uc == ':') || (*uc == ',') || (*uc == ' ')) unitok = 0;
					}
					if (!unitok) {
						dbgprintf("Ignoring invalid unit on DS %s\n", spec);
					}
					else if (dsend) {
						if (STRBUFLEN(unitspec)) addtobuffer(unitspec, ",");
						addtobufferraw(unitspec, dsname, dsend - dsname);
						addtobuffer(unitspec, ":");
						addtobuffer(unitspec, unit);
					}
				}
				{
					/* positional DS names, for the AGGDS census */
					char *dsname = spec + 3;
					char *dsend = strchr(dsname, ':');
					if (dsend) {
						if (STRBUFLEN(dsnspec)) addtobuffer(dsnspec, ",");
						addtobufferraw(dsnspec, dsname, dsend - dsname);
					}
					/* ... and the declared heartbeat (colon field 4,
					 * DS:name:type:HB:min:max), recorded so the
					 * reconcile tool can compare files against the
					 * current declaration. All-digit or skipped -
					 * rrdcreate rejects anything else anyway. */
					if (dsend) {
						char *hb = strchr(dsend+1, ':');
						char *hbend = (hb ? strchr(hb+1, ':') : NULL);
						if (hbend && (hbend > hb+1)) {
							char *dc;
							int hbok = 1;
							for (dc = hb+1; (hbok && (dc < hbend)); dc++) {
								if (!isdigit((unsigned char)*dc)) hbok = 0;
							}
							if (hbok) {
								if (STRBUFLEN(hbspec)) addtobuffer(hbspec, ",");
								addtobufferraw(hbspec, dsname, dsend - dsname);
								addtobuffer(hbspec, ":");
								addtobufferraw(hbspec, hb+1, hbend - (hb+1));
							}
						}
					}
				}
				devmon_params[numds] = spec;
				numds++;
			}
			dbgprintf("Found %d DS definitions\n",numds);
			devmon_params[numds] = NULL;
			/* Only a DS line that actually declared something may set
			 * (or clear) the units - a later, ignored DS line must not
			 * wipe the first one's declarations. */
			if (numds > startds) {
				fsidx_set_units(STRBUFLEN(unitspec) ? STRBUF(unitspec) : NULL);
				fsidx_set_dsnames(STRBUFLEN(dsnspec) ? STRBUF(dsnspec) : NULL);
				fsidx_set_heartbeats(STRBUFLEN(hbspec) ? STRBUF(hbspec) : NULL);
			}
			freestrbuffer(unitspec);
			freestrbuffer(dsnspec);
			freestrbuffer(hbspec);

			goto nextline;
		}

		/* THRESHOLD:<base-ds>:<relop><operand>[:<severity>] - a declared
		 * relation between a metric and its threshold (a DS of the same
		 * block, or a literal). Validated against the declared DSes and
		 * recorded in the fileset index for the renderer; severity is
		 * the generic warn|crit (default crit). Invalid lines are
		 * ignored with a debug note - wire content, not config. */
		if (metrics_block && !strncmp(curline, "THRESHOLD:", 10)) {
			/* columns[0] is the whole declaration (the grammar has no
			 * spaces) in a mutable copy - curline stays intact. */
			char *base = columns[0] + 10;
			char *expr = strchr(base, ':');
			char *sev = NULL, *operand;
			size_t rlen;
			int i, baseok = 0, opok = 0;

			if (expr) {
				*expr = '\0'; expr++;
				sev = strchr(expr, ':');
				if (sev) { *sev = '\0'; sev++; }
			}
			operand = (expr ? expr + strspn(expr, "<>=") : NULL);
			rlen = (expr ? (size_t)(operand - expr) : 0);
			if (!expr || !(*operand) ||
			    !(((rlen == 1) && ((*expr == '>') || (*expr == '<'))) ||
			      ((rlen == 2) && ((*expr == '>') || (*expr == '<')) && (expr[1] == '='))) ||
			    (sev && strcasecmp(sev, "warn") && strcasecmp(sev, "crit"))) {
				dbgprintf("Skipping malformed THRESHOLD on line %d\n", lineno);
				goto nextline;
			}
			for (i = 0; devmon_params[i]; i++) {
				char *dsname = devmon_params[i] + 3;
				char *dsend = strchr(dsname, ':');
				size_t dlen = (dsend ? (size_t)(dsend - dsname) : strlen(dsname));

				if ((strlen(base) == dlen) && (strncmp(base, dsname, dlen) == 0)) baseok = 1;
				if ((strlen(operand) == dlen) && (strncmp(operand, dsname, dlen) == 0)) opok = 1;
			}
			if (!opok) {
				/* Not a declared DS: legal only as a number */
				char *endp;
				strtod(operand, &endp);
				opok = ((endp != operand) && (*endp == '\0'));
			}
			if (!baseok || !opok) {
				dbgprintf("Skipping THRESHOLD on line %d: base or operand not declared in this block\n", lineno);
				goto nextline;
			}
			if (STRBUFLEN(thrspec)) addtobuffer(thrspec, ",");
			addtobuffer(thrspec, base);
			addtobuffer(thrspec, ":");
			addtobuffer(thrspec, expr);
			addtobuffer(thrspec, ":");
			addtobuffer(thrspec, ((sev && (strcasecmp(sev, "warn") == 0)) ? "warn" : "crit"));
			fsidx_set_thresholds(STRBUF(thrspec));
			goto nextline;
		}

		/* A METRICS block line whose first token is an ALL-CAPS keyword
		 * ending in ':' is a declaration - DS: is one, handled above.
		 * Declarations the writer does not know are ignored by contract,
		 * so the block dialect can grow keyword lines without breaking
		 * deployed writers; instance names must not look like one.
		 * METRICS only: legacy DEVMON blocks predate the contract and
		 * may carry instances named like a keyword (e.g. "CPU:1"). */
		if (metrics_block) {
			int kwlen = strspn(columns[0], "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
			if ((kwlen > 0) && (columns[0][kwlen] == ':')) {
				dbgprintf("Skipping unknown declaration on line %d (%s)\n",lineno,columns[0]);
				goto nextline;
			}
		}

		dbgprintf("Found %d columns in devmon rrd data\n",columncount);

		/* Now we should be on to values:
		 * eth0.0 4678222:9966777
		 */
		if (numds == 0) {
			/* No DS line in this block: nothing can be created. */
			dbgprintf("Skipping line %d, block has no DS definitions\n",lineno);
			goto nextline;
		}
		/* Split the instance from its values. The value token is the
		 * LAST whitespace field - colon-separated DS values never carry
		 * a space - so the instance is everything before it, and a mount
		 * point or folder name may thus contain spaces (encoded to %20
		 * for the filename by rrdinstance_encode). Legacy DEVMON blocks
		 * keep the strict two-column form: their repeater names never had
		 * spaces, and a >2-column line was always dropped as malformed. */
		if (metrics_block) {
			char *vp = strrchr(curline, ' ');
			size_t ilen;

			if (!vp || (vp == curline)) {
				dbgprintf("Skipping line %d, no instance/value split\n",lineno);
				goto nextline;
			}
			ilen = (size_t)(vp - curline);
			while ((ilen > 0) && (curline[ilen-1] == ' ')) ilen--;	/* trim the split run */
			if (ilen == 0) {
				dbgprintf("Skipping line %d, empty instance\n",lineno);
				goto nextline;
			}
			ifname = (char *)xmalloc(ilen + 1);
			memcpy(ifname, curline, ilen); ifname[ilen] = '\0';
			dsval = strtok(vp + 1, ":");
		}
		else {
			if (columncount > 2) {
				dbgprintf("Skipping line %d, found %d (max 2) columns in devmon rrd data, space in repeater name?\n",lineno,columncount);
				goto nextline;
			}
			ifname = xstrdup(columns[0]);
			dsval = strtok(columns[1],":");
		}
		if (dsval == NULL) {
			dbgprintf("Skipping line %d, line is malformed\n",lineno);
			goto nextline;
		}
		/* Values come from the message, so every append is bounded -
		 * rrdvalues is a fixed static buffer and messages can be far
		 * larger than it. An oversized line is skipped, not truncated. */
		rrdvalused = snprintf(rrdvalues, sizeof(rrdvalues), "%d:", (int)tstamp);
		if ((rrdvalused < 0) || (rrdvalused + strlen(dsval) + 1 > sizeof(rrdvalues))) {
			dbgprintf("Skipping line %d, values too long\n",lineno);
			goto nextline;
		}
		strcpy(rrdvalues + rrdvalused, dsval); rrdvalused += strlen(dsval);
		for (i=1;i < numds;i++) {
			dsval = strtok(NULL,":");
			if (dsval == NULL) {
				dbgprintf("Skipping line %d, %d tokens present, expecting %d\n",lineno,i,numds);
				goto nextline;
			}
			if (rrdvalused + strlen(dsval) + 2 > sizeof(rrdvalues)) {
				dbgprintf("Skipping line %d, values too long\n",lineno);
				goto nextline;
			}
			rrdvalues[rrdvalused++] = ':';
			strcpy(rrdvalues + rrdvalused, dsval); rrdvalused += strlen(dsval);
		}
		/* File names in the format if_load.eth0.0.rrd; a lazy banner
		 * attribute is enforced by the generic creation gate in
		 * create_and_update_rrd(). METRICS blocks reversibly encode the
		 * instance (rrdinstance_encode) so an arbitrary instance - a mount
		 * point, a name with a comma - round-trips to one unambiguous file;
		 * the legacy banner keeps setupfn2()'s lossy '/'->',' so its
		 * existing files are untouched. */
		{
		char *encinst = NULL;

		if (metrics_block) {
			encinst = rrdinstance_encode(ifname);

			/* setupfn2() before the migration below: it owns the final
			 * filename, including the md5 shortening of over-long
			 * encoded names - the rename target must be the file the
			 * writer will actually update. */
			setupfn2("%s.%s.rrd", rrdbasename, encinst);

			/* One-time legacy migration, ported from do_disk: a block
			 * that replaced a legacy writer must carry the pre-cutover
			 * file across, or every instance graphs twice (frozen legacy
			 * curve + restarting encoded one). Two writers produced
			 * legacy names for these blocks - do_disk appended the
			 * mangled mount with NO separator ('/'->',' and bare "/" as
			 * ",root"), a legacy DEVMON block went through setupfn2()
			 * dot-separated - so both shapes are candidates. Whenever
			 * the encoded name differs from a legacy shape (any unsafe
			 * character, not just '/') the legacy file may exist. */
			{
				char legacy[PATH_MAX], oldfn[PATH_MAX], oldpath[PATH_MAX], newpath[PATH_MAX];
				char *lp;
				struct stat st;
				int shape;

				int fits = ((size_t)snprintf(legacy, sizeof(legacy), "%s", ifname) < sizeof(legacy));
				for (lp = legacy; ((lp = strchr(lp, '/')) != NULL); ) *lp = ',';
				fits = fits && ((size_t)snprintf(newpath, sizeof(newpath), "%s/%s/%s", rrddir, hostname, rrdfn) < sizeof(newpath));
				for (shape = 0; (fits && (shape < 2)); shape++) {
					int ofits;
					if (shape == 0)
						ofits = ((size_t)snprintf(oldfn, sizeof(oldfn), "%s%s.rrd", rrdbasename,
							 ((strcmp(legacy, ",") == 0) ? ",root" : legacy)) < sizeof(oldfn));
					else
						ofits = ((size_t)snprintf(oldfn, sizeof(oldfn), "%s.%s.rrd", rrdbasename, legacy) < sizeof(oldfn));
					legacyfn_finish(oldfn);
					if (!ofits) continue;
					if (strcmp(oldfn, rrdfn) == 0) continue;	/* same name - nothing to migrate */
					if ((size_t)snprintf(oldpath, sizeof(oldpath), "%s/%s/%s", rrddir, hostname, oldfn) >= sizeof(oldpath)) continue;
					if ((stat(newpath, &st) != 0) && (stat(oldpath, &st) == 0)) {
						if (rename(oldpath, newpath) != 0)
							errprintf("block RRD migrate: rename %s -> %s failed: %s\n",
								  oldpath, newpath, strerror(errno));
					}
				}
			}
		}
		else {
			setupfn2("%s.%s.rrd", rrdbasename, ifname);
		}
		dbgprintf("Sending from devmon to RRD for %s %s: %s\n",rrdbasename,ifname,rrdvalues);
		create_and_update_rrd(hostname, testname, classname, pagepaths, devmon_params, NULL);
		/* setupfn2() published encinst/ifname in fnparams[], which the
		 * external-processor branch of create_and_update_rrd() reads -
		 * neither may be freed before the call returns. */
		if (encinst) { xfree(encinst); }
		if (ifname) { xfree(ifname); ifname = NULL; }
		}

		if (eoln) *eoln = '\n';

nextline:
		if (fsline) { xfree(fsline); fsline = NULL; }
		if (ifname) { xfree(ifname); ifname = NULL; }
		curline = (eoln ? (eoln+1) : NULL);
	}
	fsidx_set_units(NULL);
	fsidx_set_dsnames(NULL);
	fsidx_set_heartbeats(NULL);
	fsidx_set_thresholds(NULL);
	freestrbuffer(thrspec);
	if (ownedbasename) xfree(ownedbasename);

	{
		int i;
		for (i = 0; (devmon_params[i]); i++) xfree(devmon_params[i]);
	}

	return 0;
}
