/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbgend_rcsid[] = "$Id: do_hobbitd.c,v 1.1 2004-12-19 22:37:41 henrik Exp $";

static char *bbgend_params[] = { "rrdcreate", rrdfn, 
				 "DS:inmessages:DERIVE:600:0:U", 
				 "DS:statusmessages:DERIVE:600:0:U", 
				 "DS:combomessages:DERIVE:600:0:U", 
				 "DS:pagemessages:DERIVE:600:0:U", 
				 "DS:summarymessages:DERIVE:600:0:U", 
				 "DS:datamessages:DERIVE:600:0:U", 
				 "DS:notesmessages:DERIVE:600:0:U", 
				 "DS:enablemessages:DERIVE:600:0:U", 
				 "DS:disablemessages:DERIVE:600:0:U", 
				 "DS:ackmessages:DERIVE:600:0:U", 
				 "DS:configmessages:DERIVE:600:0:U", 
				 "DS:querymessages:DERIVE:600:0:U", 
				 "DS:boardmessages:DERIVE:600:0:U", 
				 "DS:listmessages:DERIVE:600:0:U", 
				 "DS:logmessages:DERIVE:600:0:U", 
				 "DS:dropmessages:DERIVE:600:0:U", 
				 "DS:renamemessages:DERIVE:600:0:U", 
				 "DS:statuschmsgs:DERIVE:600:0:U", 
				 "DS:stachgchmsgs:DERIVE:600:0:U", 
				 "DS:pagechmsgs:DERIVE:600:0:U", 
				 "DS:datachmsgs:DERIVE:600:0:U", 
				 "DS:noteschmsgs:DERIVE:600:0:U", 
				 "DS:enadischmsgs:DERIVE:600:0:U", 
				 rra1, rra2, rra3, rra4, NULL };

int do_bbgend_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	struct {
		char *marker;
		unsigned long val;
	} bbgend_data[] = {
		{ "\nIncoming messages", 0 },
		{ "\n- status", 0 },
		{ "\n- combo", 0 },
		{ "\n- page", 0 },
		{ "\n- summary", 0 },
		{ "\n- data", 0 },
		{ "\n- notes", 0 },
		{ "\n- enable", 0 },
		{ "\n- disable", 0 },
		{ "\n- ack", 0 },
		{ "\n- config", 0 },
		{ "\n- query", 0 },
		{ "\n- bbgendboard", 0 },
		{ "\n- bbgendlist", 0 },
		{ "\n- bbgendlog", 0 },
		{ "\n- bbgenddrop", 0 },
		{ "\n- bbgendrename", 0 },
		{ "\nstatus channel messages", 0 },
		{ "\nstachg channel messages", 0 },
		{ "\npage   channel messages", 0 },
		{ "\ndata   channel messages", 0 },
		{ "\nnotes  channel messages", 0 },
		{ "\nenadis channel messages", 0 },
		{ NULL, 0 }
	};

	int	i, gotany = 0;
	char	*p;
	char	valstr[10];

	sprintf(rrdvalues, "%d", (int)tstamp);
	i = 0;
	while (bbgend_data[i].marker) {
		p = strstr(msg, bbgend_data[i].marker);
		if (p) {
			if (*p == '\n') p++;
			p += strcspn(p, ":\r\n");
			if (*p == ':') {
				bbgend_data[i].val = atol(p+1);
				gotany++;
				sprintf(valstr, ":%lu", bbgend_data[i].val);
				strcat(rrdvalues, valstr);
			}
			else strcat(rrdvalues, ":U");
		}
		else strcat(rrdvalues, ":U");

		i++;
	}

	if (gotany) {
		if (strcmp("bbgend", testname) != 0) {
			sprintf(rrdfn, "bbgend.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "bbgend.rrd");
		}

		return create_and_update_rrd(hostname, rrdfn, bbgend_params, update_params);
	}

	return 0;
}

