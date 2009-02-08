/*----------------------------------------------------------------------------*/
/* Hobbit CGI for reporting performance statisticsc from the RRD data         */
/*                                                                            */
/* Copyright (C) 2008 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-perfdata.c 5819 2008-09-30 16:37:31Z storner $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <dirent.h>

#include <rrd.h>
#include <pcre.h>

#include "libbbgen.h"

enum { O_XML, O_CSV } outform = O_XML;
char csvdelim = ',';
char *hostpattern = NULL;
char *starttime = NULL;
char *endtime = NULL;

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "HOST") == 0) {
			hostpattern = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "STARTTIME") == 0) {
			starttime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "ENDTIME") == 0) {
			endtime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CSV") == 0) {
			outform = O_CSV;
			if (*(cwalk->value)) csvdelim = *(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}


int oneset(char *hostname, char *rrdname, char *starttime, char *endtime, char *colname, double subfrom, char *dsdescr)
{
	static int firsttime = 1;
	time_t start, end, t;
	unsigned long step;
	unsigned long dscount;
	char **dsnames;
	rrd_value_t *data;
	int columnindex;
	char tstamp[30];
	int dataindex, rowcount, havemin, havemax, missingdata;
	double sum, min, max, val;

	char *rrdargs[10];
	int result;

	rrdargs[0] = "rrdfetch";
	rrdargs[1] = rrdname;
	rrdargs[2] = "AVERAGE";
	rrdargs[3] = "-s"; rrdargs[4] = starttime; rrdargs[5] = "00:00";
	rrdargs[6] = "-e"; rrdargs[7] = endtime; rrdargs[8] = "00:00";
	rrdargs[9] = NULL;

	optind = opterr = 0; rrd_clear_error();
	result = rrd_fetch(9, rrdargs,
			   &start, &end, &step, &dscount, &dsnames, &data);

	if (result != 0) {
		errprintf("RRD error: %s\n", rrd_get_error());
		return 1;
	}

	for (columnindex=0; ((columnindex < dscount) && strcmp(dsnames[columnindex], colname)); columnindex++) ;
	if (columnindex == dscount) {
		errprintf("RRD error: Cannot find column %s\n", colname);
		return 1;
	}

	sum = 0.0;
	havemin = havemax = 0;
	rowcount = 0;

	switch (outform) {
	  case O_XML:
		printf("  <dataset>\n");
		printf("     <hostname>%s</hostname>\n", hostname);
		printf("     <datasource>%s</datasource>\n", rrdname);
		printf("     <rrdcolumn>%s</rrdcolumn>\n", colname);
		printf("     <measurement>%s</measurement>\n", (dsdescr ? dsdescr : colname));
		printf("     <datapoints>\n");
		break;

	  case O_CSV:
		if (firsttime) {
			printf("\"hostname\"%c\"datasource\"%c\"rrdcolumn\"%c\"measurement\"%c\"time\"%c\"value\"\n",
				csvdelim, csvdelim, csvdelim, csvdelim, csvdelim);
			firsttime = 0;
		}
		break;
	}

	for (t=start+step, dataindex=columnindex, missingdata=0; (t <= end); t += step, dataindex += dscount) {
		if (isnan(data[dataindex]) || isnan(-data[dataindex])) {
			missingdata++;
			continue;
		}

		val = (subfrom != 0) ?  subfrom - data[dataindex] : data[dataindex];

		strftime(tstamp, sizeof(tstamp), "%Y%m%d%H%M%S", localtime(&t));

		switch (outform) {
		  case O_XML:
			printf("        <dataelement>\n");
			printf("           <time>%s</time>\n", tstamp);
			printf("           <value>%f</value>\n", val);
			printf("        </dataelement>\n");
			break;
		  case O_CSV:
			printf("\"%s\"%c\"%s\"%c\"%s\"%c\"%s\"%c\"%s\"%c%f\n",
				hostname, csvdelim, rrdname, csvdelim, colname, csvdelim, (dsdescr ? dsdescr : colname), csvdelim, tstamp, csvdelim, val);
			break;
		}

		if (!havemax || (val > max)) {
			max = val;
			havemax = 1;
		}
		if (!havemin || (val < min)) {
			min = val;
			havemin = 1;
		}
		sum += val;
		rowcount++;
	}

	if (outform == O_XML) {
		printf("     </datapoints>\n");
		printf("     <summary>\n");
		if (havemin) printf("          <minimum>%f</minimum>\n", min);
		if (havemax) printf("          <maximum>%f</maximum>\n", max);
		if (rowcount) printf("          <average>%f</average>\n", (sum / rowcount));
		printf("          <missingdatapoints>%d</missingdatapoints>\n", missingdata);
		printf("     </summary>\n");
		printf("  </dataset>\n");
	}

	return 0;
}


int onehost(char *hostname, char *starttime, char *endtime)
{
	struct stat st;
	DIR *d;
	struct dirent *de;

	if ((chdir(xgetenv("BBRRDS")) == -1) || (chdir(hostname) == -1)) {
		errprintf("Cannot cd to %s/%s\n", xgetenv("BBRRDS"), hostname);
		return 1;
	}

	/* 
	 * CPU busy data - use vmstat.rrd if it is there, 
	 * if not then assume it's a Windows box and report the la.rrd data.
	 */
	if (stat("vmstat.rrd", &st) == 0) {
		oneset(hostname, "vmstat.rrd", starttime, endtime, "cpu_idl", 100, "pctbusy");
	}
	else {
		/* No vmstat data, so use the la.rrd file */
		oneset(hostname, "la.rrd", starttime, endtime, "la", 0, "pctbusy");
	}

	/*
	 * Memory data - use "actual" memory if present, otherwise report
	 * the data from the "real" reading.
	 */
	if (stat("memory.actual.rrd", &st) == 0) {
		oneset(hostname, "memory.actual.rrd", starttime, endtime, "realmempct", 0, "RAM");
	}
	else {
		oneset(hostname, "memory.real.rrd", starttime, endtime, "realmempct", 0, "RAM");
	}

	if (stat("memory.swap.rrd", &st) == 0) {
		oneset(hostname, "memory.swap.rrd", starttime, endtime, "realmempct", 0, "Swap");
	}

	/*
	 * Report data for all filesystems.
	 */
	d = opendir(".");
	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "disk,", 5) != 0) continue;

		stat(de->d_name, &st);
		if (!S_ISREG(st.st_mode)) continue;

		if (strcmp(de->d_name, "disk,root.rrd") == 0) {
			oneset(hostname, de->d_name, starttime, endtime, "pct", 0, "/");
		}
		else {
			char *fsnam = strdup(de->d_name+4);
			char *p;

			while ((p = strchr(fsnam, ',')) != NULL) *p = '/';
			p = fsnam + strlen(fsnam) - 4; *p = '\0';
			oneset(hostname, de->d_name, starttime, endtime, "pct", 0, fsnam);
			xfree(fsnam);
		}
	}
	closedir(d);
	return 0;
}

int main(int argc, char **argv)
{
	pcre *ptn;
	void *hwalk;
	char *hostname;

	if (getenv("QUERY_STRING") == NULL) {
		/* Not invoked through the CGI */
		if (argc < 4) {
			errprintf("Usage:\n%s HOSTNAME-PATTERN STARTTIME ENDTIME", argv[0]);
			return 1;
		}

		hostpattern = argv[1];
		starttime = argv[2];
		endtime = argv[3];
		if (argc > 4) {
			if (strncmp(argv[4], "--csv", 5) == 0) {
				char *p;

				outform = O_CSV;
				if ((p = strchr(argv[4], '=')) != NULL) csvdelim = *(p+1);
			}
		}
	}
	else {
		/* Parse CGI parameters */
		parse_query();
		switch (outform) {
		  case O_XML:
			printf("Content-type: application/xml\n\n");
			break;

		  case O_CSV:
			printf("Content-type: text/csv\n\n");
			break;
		}
	}

	ptn = compileregex(hostpattern);
	if (!ptn) {
		errprintf("Invalid pattern '%s'\n", hostpattern);
		return 1;
	}

	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

	switch (outform) {
	  case O_XML:
		printf("<?xml version='1.0' encoding='ISO-8859-1'?>\n");
		printf("<datasets>\n");
		break;
	  case O_CSV:
		break;
	}

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		hostname = bbh_item(hwalk, BBH_HOSTNAME);

		if (!matchregex(hostname, ptn)) continue;

		onehost(hostname, starttime, endtime);
	}

	switch (outform) {
	  case O_XML:
		printf("</datasets>\n");
		break;
	  case O_CSV:
		break;
	}

	return 0;
}

