/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char hobbitd_rcsid[] = "$Id: do_hobbitd.c,v 1.10 2006-06-09 22:23:49 henrik Exp $";

int do_hobbitd_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	static char *hobbitd_params[] = { "rrdcreate", rrdfn, 
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
	static char *hobbitd_tpl       = NULL;

	struct {
		char *marker;
		unsigned long val;
	} hobbitd_data[] = {
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
		{ "\n- hobbitdboard", 0 },
		{ "\n- hobbitdlist", 0 },
		{ "\n- hobbitdlog", 0 },
		{ "\n- drop", 0 },
		{ "\n- rename", 0 },
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

	MEMDEFINE(valstr);

	if (hobbitd_tpl == NULL) hobbitd_tpl = setup_template(hobbitd_params);

	sprintf(rrdvalues, "%d", (int)tstamp);
	i = 0;
	while (hobbitd_data[i].marker) {
		p = strstr(msg, hobbitd_data[i].marker);
		if (p) {
			if (*p == '\n') p++;
			p += strcspn(p, ":\r\n");
			if (*p == ':') {
				hobbitd_data[i].val = atol(p+1);
				gotany++;
				sprintf(valstr, ":%lu", hobbitd_data[i].val);
				strcat(rrdvalues, valstr);
			}
			else strcat(rrdvalues, ":U");
		}
		else strcat(rrdvalues, ":U");

		i++;
	}

	if (gotany) {
		if (strcmp("hobbitd", testname) != 0) {
			setupfn("hobbitd.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "hobbitd.rrd");
		}

		MEMUNDEFINE(valstr);
		return create_and_update_rrd(hostname, rrdfn, hobbitd_params, hobbitd_tpl);
	}

	MEMUNDEFINE(valstr);
	return 0;
}

