/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char snmpmib_rcsid[] = "$Id$";

static time_t snmp_nextreload = 0;

typedef struct snmpmib_param_t {
	char *name;
	char **valnames;
	char **dsdefs;
	rrdtpldata_t *tpl;
	int valcount;
} snmpmib_param_t;
static void * snmpmib_paramtree;


int is_snmpmib_rrd(char *testname)
{
	time_t now = getcurrenttime(NULL);
	xtreePos_t handle;
	mibdef_t *mib;
	oidset_t *swalk;
	int i;

	if (now > snmp_nextreload) {
		int updated = readmibs(NULL, 0);

		if (updated) {
			if (snmp_nextreload > 0) {
				/* Flush the old params and templates */
				snmpmib_param_t *walk;
				int i;

				for (handle = xtreeFirst(snmpmib_paramtree); (handle != xtreeEnd(snmpmib_paramtree)); handle = xtreeNext(snmpmib_paramtree, handle)) {
					walk = (snmpmib_param_t *)xtreeData(snmpmib_paramtree, handle);
					if (walk->valnames) xfree(walk->valnames);
					for (i=0; (i < walk->valcount); i++) xfree(walk->dsdefs[i]);
					if (walk->dsdefs) xfree(walk->dsdefs);

					/* 
					 * We don't free the "tpl" data here. We cannot do it, because
					 * there are probably cached RRD updates waiting to use this
					 * template - so freeing it would cause all sorts of bad behaviour.
					 * It DOES cause a memory leak ...
					 */

					xfree(walk);
				}
				xtreeDestroy(snmpmib_paramtree);
			}

			snmpmib_paramtree = xtreeNew(strcasecmp);
		}

		snmp_nextreload = now + 600;
	}

	mib = find_mib(testname);
	if (!mib) return 0;

	handle = xtreeFind(snmpmib_paramtree, mib->mibname);
	if (handle == xtreeEnd(snmpmib_paramtree)) {
		snmpmib_param_t *newitem = (snmpmib_param_t *)calloc(1, sizeof(snmpmib_param_t));
		int totalvars;

		newitem->name = mib->mibname;

		for (swalk = mib->oidlisthead, totalvars = 1; (swalk); swalk = swalk->next) totalvars += swalk->oidcount;
		newitem->valnames = (char **)calloc(totalvars, sizeof(char *));
		newitem->dsdefs = (char **)calloc(totalvars, sizeof(char *));
		for (swalk = mib->oidlisthead, newitem->valcount = 0; (swalk); swalk = swalk->next) {
			for (i=0; (i <= swalk->oidcount); i++) {
				char *datatypestr, *minimumstr;

				if (swalk->oids[i].rrdtype == RRD_NOTRACK) continue;

				switch (swalk->oids[i].rrdtype) {
				  case RRD_TRACK_GAUGE:
					datatypestr = "GAUGE";
					minimumstr = "U";
					break;

				  case RRD_TRACK_ABSOLUTE:
					datatypestr = "ABSOLUTE";
					minimumstr = "U";
					break;

				  case RRD_TRACK_COUNTER:
					datatypestr = "COUNTER";
					minimumstr = "0";
					break;

				  case RRD_TRACK_DERIVE:
					datatypestr = "DERIVE";
					minimumstr = "0";
					break;

				  case RRD_NOTRACK:
					break;
				}

				newitem->valnames[newitem->valcount] = swalk->oids[i].dsname;
				newitem->dsdefs[newitem->valcount] = (char *)malloc(strlen(swalk->oids[i].dsname) + 20);
				sprintf(newitem->dsdefs[newitem->valcount], "DS:%s:%s:600:%s:U",
					swalk->oids[i].dsname, datatypestr, minimumstr);
				newitem->valcount++;
			}
		}

		newitem->valnames[newitem->valcount] = NULL;
		newitem->dsdefs[newitem->valcount] = NULL;
		newitem->tpl = setup_template(newitem->dsdefs);
		xtreeAdd(snmpmib_paramtree, newitem->name, newitem);
	}

	return 1;
}

static void do_simple_snmpmib(char *hostname, char *testname, char *classname, char *pagepaths, char *fnkey,
			      char *msg, time_t tstamp, 
			      snmpmib_param_t *params, int *pollinterval)
{
	char *bol, *eoln;
	char **values;
	int valcount = 0;

	values = (char **)calloc(params->valcount, sizeof(char *));

	bol = msg;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		bol += strspn(bol, " \t");
		if (*bol == '\0') {
			/* Nothing */
		}
		else if (strncmp(bol, "Interval=", 9) == 0) {
			*pollinterval = atoi(bol+9);
		}
		else if (strncmp(bol, "ActiveIP=", 9) == 0) {
			/* Nothing */
		}
		else {
			char *valnam, *valstr = NULL;

			valnam = strtok(bol, " =");
			if (valnam) valstr = strtok(NULL, " =");

			if (valnam && valstr) {
				int validx;
				for (validx = 0; (params->valnames[validx] && strcmp(params->valnames[validx], valnam)); validx++) ;
				/* Note: There may be items which are not RRD data (eg text strings) */
				if (params->valnames[validx]) {
					values[validx] = (isdigit(*valstr) ? valstr : "U");
					valcount++;
				}
			}
		}

		bol = (eoln ? eoln+1 : NULL);
	}

	if (valcount == params->valcount) {
		int i;
		char *ptr;

		if (fnkey) setupfn2("%s.%s.rrd", testname, fnkey); else setupfn("%s.rrd", testname);
		setupinterval(*pollinterval);

		ptr = rrdvalues + snprintf(rrdvalues, sizeof(rrdvalues), "%d", (int)tstamp);
		for (i = 0; (i < valcount); i++) {
			ptr += snprintf(ptr, sizeof(rrdvalues)-(ptr-rrdvalues), ":%s", values[i]);
		}
		create_and_update_rrd(hostname, testname, classname, pagepaths, params->dsdefs, params->tpl);
	}

	xfree(values);
}

static void do_tabular_snmpmib(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp, snmpmib_param_t *params)
{
	char *fnkey;
	int pollinterval = 0;
	char *boset, *eoset, *intvl;

	boset = strstr(msg, "\n[");
	if (!boset) return;

	/* See if there's a poll interval value */
	*boset = '\0'; boset++;
	intvl = strstr(msg, "Interval=");
	if (intvl) pollinterval = atoi(intvl+9);

	while (boset) {
		fnkey = boset+1;
		boset = boset + strcspn(boset, "]\n"); *boset = '\0'; boset++;
		eoset = strstr(boset, "\n["); if (eoset) *eoset = '\0';
		do_simple_snmpmib(hostname, testname, classname, pagepaths, fnkey, boset, tstamp, params, &pollinterval);
		boset = (eoset ? eoset+1 : NULL);
	}
}

int do_snmpmib_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	time_t now = getcurrenttime(NULL);
	mibdef_t *mib;
	xtreePos_t handle;
	snmpmib_param_t *params;
	int pollinterval = 0;
	char *datapart;

	if (now > snmp_nextreload) readmibs(NULL, 0);

	mib = find_mib(testname); if (!mib) return 0;
	handle = xtreeFind(snmpmib_paramtree, mib->mibname);
	if (handle == xtreeEnd(snmpmib_paramtree)) return 0;
	params = (snmpmib_param_t *)xtreeData(snmpmib_paramtree, handle);
	if (params->valcount == 0) return 0;

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "data", 4) == 0)) {
		/* Skip the first line of full status- and data-messages. */
		datapart = strchr(msg, '\n');
		if (datapart) datapart++; else datapart = msg;
	}

	if (mib->tabular) 
		do_tabular_snmpmib(hostname, testname, classname, pagepaths, datapart, tstamp, params);
	else
		do_simple_snmpmib(hostname, testname, classname, pagepaths, NULL, datapart, tstamp, params, &pollinterval);

	return 0;
}

