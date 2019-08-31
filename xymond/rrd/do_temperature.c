/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.						      */
/*									      */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>		      */
/*									      */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.			      */
/*									      */
/*----------------------------------------------------------------------------*/

/* Sample input reports:


Device				Temp	LowShutdown	LowWarning	HighWarning	HighShutdown
----------------------------------------------------------------------------------------------------
&green hard-disk1 hard-disk1 	 44	  0		  5		 55		 60
&green CPU 0 Die             	 86	-10		  0		 95		100
&green CPU 1 Die             	 80	-10		  0		 95		100
&green CPU 0 Ambient         	 54	 -8		  0		 70		 75
&green System Outtake Ambient	 53	-10		  0		 70		 75
&green System Intake Ambient 	 33	-11		  0		 45		 70
&green CPU 1 Ambient         	 42	 -9		  0		 70		 75
----------------------------------------------------------------------------------------------------

Device				Temp	Warning	Critical
---------------------------------------------------------
&green CPU0                  	 60	 63	 68
&green CPU1                  	 58	 63	 68
&clear MB0                   	 46		
&clear MB1                   	 36		
&green PDB                   	 40	 53	 58
&clear SCSI                  	 33		
---------------------------------------------------------

Device				Temp	High	Crit
-----------------------------------------------------
&green Package id 0          	 42	 86	100
&green Core 0                	 41	 86	100
&green Core 1                	 42	 86	100
&green temp1                 	 37		 98
-----------------------------------------------------

Device                   Temp(C)  Temp(F)    Lo(C)  LoWarn(C)  HiWarn(C)  Hi(C)
-------------------------------------------------------------------------------
&green CPU 0 Die                79      174     (-10        0         88      90)
&green CPU 0 Ambient            23       73     (-10        0         40      60)
-------------------------------------------------------------------------------

Device                  Temp(C)  Temp(F)  Threshold(C)
------------------------------------------------------
&green System Board Inlet Temp    21       69     ( 42)
&green CPU1 Temp               32       89     ( 93)
&green CPU2 Temp               31       87     ( 93)
------------------------------------------------------

*/

static char temperature_rcsid[] = "$Id$";

int do_temperature_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *temperature_params[] = { "DS:temperature:GAUGE:600:1:U",
					      NULL };
	static void *temperature_tpl	  = NULL;

	char *bol, *eol, *comment, *p;
	int tmpC;

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

		if	(strncmp(bol, "&green", 6) == 0)  { bol += 6; gotone = 1; }
		else if (strncmp(bol, "&yellow", 7) == 0) { bol += 7; gotone = 1; }
		else if (strncmp(bol, "&red", 4) == 0)	  { bol += 4; gotone = 1; }
		else if (strncmp(bol, "&clear", 6) == 0)  { bol += 6; gotone = 1; }

		if (gotone) {
			char savech;

			/* Strip off any leading bold and italic HTML tags */
			if      (strncasecmp(bol, "<i><b>", 6) == 0)  { bol += 6; }
			else if (strncasecmp(bol, "<i>", 3) == 0)  { bol += 3; }
			else if (strncasecmp(bol, "<b><i>", 6) == 0)  { bol += 6; }
			else if (strncasecmp(bol, "<b>", 3) == 0)  { bol += 3; }

			bol += strspn(bol, " \t");

			p = strstr(bol, "\t");
			tmpC = atoi(p);
			while ((p > bol) && isspace((int)*p)) p--;

			savech = *(p+1); *(p+1) = '\0';
			setupfn2("%s.%s.rrd", "temperature", bol); *(p+1) = savech;

			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, tmpC);
			create_and_update_rrd(hostname, testname, classname, pagepaths, temperature_params, temperature_tpl);
		}

		if (comment) *comment = '(';
		if (eol) *eol = '\n';
	}

	return 0;
}
