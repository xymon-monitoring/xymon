static char *citrix_params[] = { "rrdcreate", rrdfn, "DS:users:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_citrix_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *p;
	int users;

	p = strstr(msg, " users active\n");
	if (p) p = strrchr(p, '\n');
	if (p && (sscanf(p+1, "%d users active\n", &users) == 1)) {
		sprintf(rrdfn, "%s/%s.%s.rrd", rrddir, commafy(hostname), testname);
		sprintf(rrdvalues, "%d:%d", (int)tstamp, users);
		return create_and_update_rrd(rrdfn, citrix_params, update_params);
	}

	return 0;
}

