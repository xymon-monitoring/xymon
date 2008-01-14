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

static char snmpcollect_rcsid[] = "$Id: snmpcollect.c,v 1.3 2008-01-14 21:27:13 henrik Exp $";

/*
 * Right now, this module simply takes each of the sections in the client
 * message re-posts it as a status message to hobbitd. This lets us use
 * SNMP data for feeding graphs.
 *
 * At some point in the future it would be very nice to analyze the data
 * and make a real (colored) status from it. Perhaps also feed into some
 * of the standard columns (cpu, memory, disk, procs) based on data from
 * various mibs.
 */
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
}

