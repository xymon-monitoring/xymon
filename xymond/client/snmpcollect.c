/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for SNMP collector                                   */
/*                                                                            */
/* Copyright (C) 2009-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char snmpcollect_rcsid[] = "$Id$";

/*
 * Split the snmpcollect client-message into individual mib-datasets.
 * Run each dataset through the mib-value configuration module, and
 * generate a status-message for each dataset.
 */

void handle_snmpcollect_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	void *ns1var, *ns1sects;
	char *onemib;
	char *mibname;
	char fromline[1024], msgline[1024];
	strbuffer_t *summary = newstrbuffer(0);

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	onemib = nextsection_r(clientdata, &mibname, &ns1var, &ns1sects);
	while (onemib) {
		char *bmark, *emark;
		char *oneds, *dskey;
		void *ns2var, *ns2sects;
		int color, rulecolor, anyrules;
		char *groups;

		if (strcmp(mibname, "proxy") == 0) {
			/*
			 * Data was forwarded through a proxy - skip this section.
			 * We dont want a "proxy" status for all SNMP-enabled hosts.
			 */
			goto sectiondone;
		}

		/* Convert the "<NNN>" markers to "[NNN]" */
		bmark = onemib;
		while ((bmark = strstr(bmark, "\n<")) != NULL) {
			emark = strchr(bmark, '>');
			*(bmark+1) = '[';
			if (emark) *emark = ']';

			bmark += 2;
		}

		/* Match the mib data against the configuration */
		anyrules = 1;
		color = COL_GREEN;
		clearalertgroups();
		clearstrbuffer(summary);
		oneds = nextsection_r(onemib, &dskey, &ns2var, &ns2sects);
		if (oneds) {
			/* Tabular MIB data. Handle each of the rows in the table. */
			while (oneds && anyrules) {
				rulecolor = check_mibvals(hinfo, clienttype, mibname, dskey, oneds, summary, &anyrules);

				if (rulecolor > color) color = rulecolor;
				oneds = nextsection_r(NULL, &dskey, &ns2var, &ns2sects);
			}
			nextsection_r_done(ns2sects);
		}
		else {
			/* Non-tabular MIB data - no key */
			rulecolor = check_mibvals(hinfo, clienttype, mibname, NULL, onemib, summary, &anyrules);
			if (rulecolor > color) color = rulecolor;
		}

		/* Generate the status message */
		groups = getalertgroups();
		init_status(color);
		if (groups) sprintf(msgline, "status/group:%s ", groups); else strcpy(msgline, "status ");
		addtostatus(msgline);
		sprintf(msgline, "%s.%s %s %s\n", hostname, mibname, colorname(color), ctime(&timestamp));
		addtostatus(msgline);
		if (STRBUFLEN(summary) > 0) {
			addtostrstatus(summary);
			addtostatus("\n");
		}
		addtostatus(onemib);
		addtostatus(fromline);
		finish_status();

sectiondone:
		onemib = nextsection_r(NULL, &mibname, &ns1var, &ns1sects);
	}
	nextsection_r_done(ns1sects);

	freestrbuffer(summary);
}

