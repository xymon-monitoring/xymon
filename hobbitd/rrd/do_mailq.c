static char *mailq_params[]       = { "rrdcreate", rrdfn, "DS:mailq:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_mailq_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char	*p;
	int	mailq;

	/* Looking for "... N requests ... " */
	p = strstr(msg, "requests");
	if (p) {
		while ((p > msg) && (isspace((int) *(p-1)) || isdigit((int) *(p-1)))) p--;
		mailq = atoi(p);

		sprintf(rrdfn, "%s/%s.%s.rrd", rrddir, commafy(hostname), testname);
		sprintf(rrdvalues, "%d:%df", (int)tstamp, mailq);
		return create_and_update_rrd(rrdfn, bbgen_params, update_params);
	}

	return 0;
}

