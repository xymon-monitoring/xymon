static char *bbproxy_params[]       = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_bbproxy_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{ 
	char	*p;
	float	runtime;

	p = strstr(msg, "Average queue time");
	if (p && (sscanf(p, "Average queue time : %f", &runtime) == 1)) {
		sprintf(rrdfn, "%s/%s.%s.rrd", rrddir, commafy(hostname), testname);
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(rrdfn, bbproxy_params, update_params);
	}

	return 0;
}

