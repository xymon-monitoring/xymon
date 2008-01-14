/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for SNMP collector                                   */
/*                                                                            */
/* Copyright (C) 2008 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char snmpcollect_rcsid[] = "$Id: snmpcollect.c,v 1.1 2008-01-14 14:46:36 henrik Exp $";

void handle_snmpcollect_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	char *onemib;
	char *mibname;
	char fromline[1024], msgline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	onemib = nextsection(clientdata, &mibname);
	while (onemib) {
		/* Convert the "<NNN>" markers to "[NNN]" */
		char *bmark, *emark;

		bmark = onemib;
		while ((bmark = strstr(bmark, "\n<")) != NULL) {
			emark = strchr(bmark, '>');
			*(bmark+1) = '[';
			if (emark) *emark = ']';

			bmark += 2;
		}

		init_status(COL_GREEN);
		sprintf(msgline, "status %s.%s green %s\n", hostname, mibname, ctime(&timestamp));
		addtostatus(msgline);
		addtostatus(onemib);
		addtostatus(fromline);
		finish_status();

		onemib = nextsection(NULL, &mibname);
	}

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, 
			uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
}

