/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char temperature_rcsid[] = "$Id$";

int do_temperature_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	static char *temperature_params[] = { "DS:temperature:GAUGE:600:1:U",
					      NULL };
	static void *temperature_tpl      = NULL;

	/* Sample input report:
	Device             Temp(C)  Temp(F)
	-----------------------------------
	&green Motherboard#0      31       87
	&green Motherboard#1      28       82
	&green AMBIENT            25       77
	&green CPU0               40      104
	&green CPU1               40      104
	&green CPU2               40      104
	&green CPU3               40      104
	&green Board 0            29       84
	&green Board 1            35       95
	&green Board 2            30       86
	&green Board 3            37       98
	&green Board 4            28       82
	&green Board 6            28       82
	&green Board CLK          27       80
	&green MB                 24       75
	&green IOB                19       66
	&green DBP0               19       66
	&green CPU 0 Die          79      174
	&green CPU 0 Ambient      27       80
	&green CPU 1 Die          73      163
	&green CPU 1 Ambient      26       78
	-----------------------------------
	Status green: All devices look okay
	*/

	char *bol, *eol, *comment, *p;
	int tmpF, tmpC;

	if (temperature_tpl == NULL) temperature_tpl = setup_template(temperature_params);

	bol = eol = msg;
	while (eol && ((p = strstr(eol, "\n&")) != NULL)) {
		int gotone = 0;

		bol = p + 1;
		eol = strchr(bol, '\n'); if (eol) *eol = '\0';

		/* See if there's a comment in parenthesis */
		comment = strchr(bol, '('); /* Begin comment */
		p = strchr(bol, ')');       /* End comment */
		if (comment && p && (comment < p)) *comment = '\0'; /* Cut off the comment */

		if      (strncmp(bol, "&green", 6) == 0)  { bol += 6; gotone = 1; }
		else if (strncmp(bol, "&yellow", 7) == 0) { bol += 7; gotone = 1; }
		else if (strncmp(bol, "&red", 4) == 0)    { bol += 4; gotone = 1; }
		else if (strncmp(bol, "&clear", 6) == 0)  { bol += 6; gotone = 1; }

		if (gotone) {
			char savech;

			bol += strspn(bol, " \t");

			p = bol + strlen(bol) - 1;
			while ((p > bol) && isspace((int)*p)) p--;
			while ((p > bol) && isdigit((int)*p)) p--;
			tmpF = atoi(p);
			while ((p > bol) && isspace((int)*p)) p--;
			while ((p > bol) && isdigit((int)*p)) p--;
			tmpC = atoi(p);
			while ((p > bol) && isspace((int)*p)) p--;

			savech = *(p+1); *(p+1) = '\0'; 
			setupfn2("%s.%s.rrd", "temperature", bol); *(p+1) = savech;

			sprintf(rrdvalues, "%d:%d", (int)tstamp, tmpC);
			create_and_update_rrd(hostname, testname, classname, pagepaths, temperature_params, temperature_tpl);
		}

		if (comment) *comment = '(';
		if (eol) *eol = '\n';
	}

	return 0;
}
