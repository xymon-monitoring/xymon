/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Francesco Duranti <fduranti@q8.it>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netapp_rcsid[] = "$Id: do_netapp.c,v 1.00 2006/05/31 20:28:44 henrik Rel $";

int do_netapp_stats_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
static char *netapp_stats_params[] = { "rrdcreate", rrdfn,
                                     "DS:NETread:GAUGE:600:0:U",
                                     "DS:NETwrite:GAUGE:600:0:U",
                                     "DS:DISKread:GAUGE:600:0:U",
                                     "DS:DISKwrite:GAUGE:600:0:U",
                                     "DS:TAPEread:GAUGE:600:0:U",
                                     "DS:TAPEwrite:GAUGE:600:0:U",
                                     "DS:FCPin:GAUGE:600:0:U",
                                     "DS:FCPout:GAUGE:600:0:U",
                                     rra1, rra2, rra3, rra4, NULL };
static char *netapp_stats_tpl      = NULL;

	static time_t starttime = 0;
	unsigned long netread=0, netwrite=0, diskread=0, diskwrite=0, taperead=0, tapewrite=0, fcpin=0, fcpout=0;
	time_t now = time(NULL);
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (starttime == 0) starttime = now;
	if (strstr(msg, "netapp.pl")) {
		sprintf(rrdfn, "%s.rrd",testname);
		if (netapp_stats_tpl == NULL) netapp_stats_tpl = setup_template(netapp_stats_params);
		netread=get_kb_data(msg, "NET_read");
		netwrite=get_kb_data(msg,"NET_write");
		diskread=get_kb_data(msg,"DISK_read");
		diskwrite=get_kb_data(msg,"DISK_write");
		taperead=get_kb_data(msg,"TAPE_read");
		tapewrite=get_kb_data(msg,"TAPE_write");
		fcpin=get_kb_data(msg,"FCP_in");
		fcpout=get_kb_data(msg,"FCP_out");
		dbgprintf("netapp: host %s test %s netread %ld netwrite %ld\n",
			hostname, testname, netread,netwrite);
		dbgprintf("netapp: host %s test %s diskread %ld diskwrite %ld\n",
			hostname, testname, diskread,diskwrite);
		dbgprintf("netapp: host %s test %s taperead %ld tapewrite %ld\n",
			hostname, testname, taperead,tapewrite);
		dbgprintf("netapp: host %s test %s fcpin %ld fcpout %ld\n",
			hostname, testname, fcpin,fcpout);
		sprintf(rrdvalues, "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
			(int) tstamp, netread, netwrite, diskread, diskwrite, 
			taperead, tapewrite, fcpin, fcpout);
		create_and_update_rrd(hostname, rrdfn, netapp_stats_params, netapp_stats_tpl);
	}
	return 0;
}


int do_netapp_cifs_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
static char *netapp_cifs_params[] = { "rrdcreate", rrdfn,
                                     "DS:sessions:GAUGE:600:0:U",
                                     "DS:openshares:GAUGE:600:0:U",
                                     "DS:openfiles:GAUGE:600:0:U",
                                     "DS:locks:GAUGE:600:0:U",
                                     "DS:credentials:GAUGE:600:0:U",
                                     "DS:opendirectories:GAUGE:600:0:U",
                                     "DS:ChangeNotifies:GAUGE:600:0:U",
                                     "DS:sessionsusingsecuri:GAUGE:600:0:U",
                                     rra1, rra2, rra3, rra4, NULL };
static char *netapp_cifs_tpl      = NULL;

	static time_t starttime = 0;
	unsigned long sess=0, share=0, file=0, lock=0, cred=0, dir=0, change=0, secsess=0;
	time_t now = time(NULL);
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (starttime == 0) starttime = now;
	if (strstr(msg, "netapp.pl")) {
		sprintf(rrdfn, "%s.rrd",testname);
		if (netapp_cifs_tpl == NULL) netapp_cifs_tpl = setup_template(netapp_cifs_params);
		sess=get_long_data(msg, "sessions");
		share=get_long_data(msg,"open_shares");
		file=get_long_data(msg,"open_files");
		lock=get_long_data(msg,"locks");
		cred=get_long_data(msg,"credentials");
		dir=get_long_data(msg,"open_directories");
		change=get_long_data(msg,"ChangeNotifies");
		secsess=get_long_data(msg,"sessions_using_security_signatures");
		dbgprintf("netapp: host %s test %s Session %ld OpenShare %ld\n",
			hostname, testname, sess, share);
		dbgprintf("netapp: host %s test %s OpenFile %ld Locks %ld\n",
			hostname, testname, file, lock);
		dbgprintf("netapp: host %s test %s Cred %ld OpenDir %ld\n",
			hostname, testname, cred, dir);
		dbgprintf("netapp: host %s test %s ChangeNotif %ld SecureSess %ld\n",
			hostname, testname, change, secsess);
		sprintf(rrdvalues, "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
                      	(int) tstamp, sess, share, file, lock, cred, dir, change,secsess);
		create_and_update_rrd(hostname, rrdfn, netapp_cifs_params, netapp_cifs_tpl);
	}
	return 0;
}


int do_netapp_ops_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
static char *netapp_ops_params[] = { "rrdcreate", rrdfn,
                                     "DS:NFSops:GAUGE:600:0:U",
                                     "DS:CIFSops:GAUGE:600:0:U",
                                     "DS:iSCSIops:GAUGE:600:0:U",
                                     "DS:HTTPops:GAUGE:600:0:U",
                                     "DS:FCPops:GAUGE:600:0:U",
                                     "DS:Totalops:GAUGE:600:0:U",
                                     rra1, rra2, rra3, rra4, NULL };
static char *netapp_ops_tpl      = NULL;

	static time_t starttime = 0;
	unsigned long nfsops=0, cifsops=0, httpops=0, iscsiops=0, fcpops=0, totalops=0;
	time_t now = time(NULL);
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (starttime == 0) starttime = now;
	if (strstr(msg, "netapp.pl")) {
		sprintf(rrdfn, "%s.rrd",testname);
		if (netapp_ops_tpl == NULL) netapp_ops_tpl = setup_template(netapp_ops_params);
		nfsops=get_long_data(msg, "NFS_ops");
		cifsops=get_long_data(msg,"CIFS_ops");
		httpops=get_long_data(msg,"HTTP_ops");
		fcpops=get_long_data(msg,"FCP_ops");
		iscsiops=get_long_data(msg,"iSCSI_ops");
		totalops=get_long_data(msg,"Total_ops");
		dbgprintf("netapp: host %s test %s nfsops %ld cifsops %ld\n",
			hostname, testname, nfsops, cifsops);
		dbgprintf("netapp: host %s test %s httpops %ld fcpops %ld\n",
			hostname, testname, httpops, fcpops);
		dbgprintf("netapp: host %s test %s iscsiops %ld totalops %ld\n",
			hostname, testname, iscsiops, totalops);
		sprintf(rrdvalues, "%d:%ld:%ld:%ld:%ld:%ld:%ld",
                       	(int) tstamp, nfsops, cifsops, httpops, fcpops, iscsiops, totalops);
		create_and_update_rrd(hostname, rrdfn, netapp_ops_params, netapp_ops_tpl);
	}
	return 0;
}

int do_netapp_snapmirror_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
static char *netapp_snapmirror_params[] = { "rrdcreate", rrdfn,
                                     "DS:size:GAUGE:600:0:U",
                                     rra1, rra2, rra3, rra4, NULL };
static char *netapp_snapmirror_tpl      = NULL;

        char *eoln, *curline, *start, *end;
	static time_t starttime = 0;
	time_t now = time(NULL);
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (starttime == 0) starttime = now;
	if (strstr(msg, "netapp.pl")) {
		if (netapp_snapmirror_tpl == NULL) netapp_snapmirror_tpl = setup_template(netapp_snapmirror_params);
                if ((start=strstr(msg, "<!--"))==NULL) return 0;
                if ((end=strstr(start,"-->"))==NULL) return 0;
                *end='\0';
                eoln = strchr(start, '\n');
                curline = (eoln ? (eoln+1) : NULL);
		while (curline && (*curline))  {
                	char *equalsign;
			long long size;
                	eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';
			equalsign=strchr(curline,'='); 
			if (equalsign) {
				*(equalsign++)= '\0'; 
				size=str2ll(equalsign,NULL);
                		dbgprintf("netapp: host %s test %s SNAPMIRROR %s size %lld\n",
                       			hostname, testname, curline, size);

				snprintf(rrdfn, sizeof(rrdfn)-1, "%s,%s.rrd", testname, curline);
				rrdfn[sizeof(rrdfn)-1] = '\0';
				sprintf(rrdvalues, "%d:%lld", (int)tstamp, size);
				create_and_update_rrd(hostname, rrdfn, netapp_snapmirror_params, netapp_snapmirror_tpl);
				*(--equalsign)='=';
			}
	                if (eoln) *eoln = '\n';
	                curline = (eoln ? (eoln+1) : NULL);
		}
                *end='-';
	}
	return 0;
}


int do_netapp_snaplist_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
static char *netapp_snaplist_params[] = { "rrdcreate", rrdfn,
                                     "DS:youngsize:GAUGE:600:0:U",
                                     "DS:oldsize:GAUGE:600:0:U",
                                     rra1, rra2, rra3, rra4, NULL };
static char *netapp_snaplist_tpl      = NULL;

        char *eoln, *curline, *start, *end;
	static time_t starttime = 0;
	time_t now = time(NULL);
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (starttime == 0) starttime = now;
	if (strstr(msg, "netapp.pl")) {
		if (netapp_snaplist_tpl == NULL) netapp_snaplist_tpl = setup_template(netapp_snaplist_params);
                if ((start=strstr(msg, "<!--"))==NULL) return 0;
                if ((end=strstr(start,"-->"))==NULL) return 0;
                *end='\0';
                eoln = strchr(start, '\n');
                curline = (eoln ? (eoln+1) : NULL);
		while (curline && (*curline))  {
                	char *fsline, *p;
        	        char *columns[5];
	                int columncount;
	                char *volname = NULL;
			long long young,old;

                	eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';
		
			for (columncount=0; (columncount<5); columncount++) columns[columncount] = "";
			fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, "=");
			while (p && (columncount < 5)) { columns[columncount++] = p; p = strtok(NULL, ":"); }
			volname = xstrdup(columns[0]);
			young=str2ll(columns[1],NULL);
			old=str2ll(columns[2],NULL);
                	dbgprintf("netapp: host %s test %s vol  %s young %lld old %lld\n",
                       		hostname, testname, volname, young, old);

			snprintf(rrdfn, sizeof(rrdfn)-1, "%s,%s.rrd", testname, volname);
                        rrdfn[sizeof(rrdfn)-1] = '\0';
                        sprintf(rrdvalues, "%d:%lld:%lld", (int)tstamp, young, old);
                        create_and_update_rrd(hostname, rrdfn, netapp_snaplist_params, netapp_snaplist_tpl);
                	if (volname) { xfree(volname); volname = NULL; }

	                if (eoln) *eoln = '\n';
			xfree(fsline);
	                curline = (eoln ? (eoln+1) : NULL);
		}
                *end='-';
	}
	return 0;
}

int do_netapp_extratest_rrd(char *hostname, char *testname, char *msg, time_t tstamp, char *params[],char *varlist[])
{
static char *netapp_tpl      = NULL;

	static time_t starttime = 0;
	time_t now = time(NULL);
	char *outp;	
	char *eoln,*curline;
	char *rrdp;
	if (starttime == 0) starttime = now;
	/* Setup the update string */

	netapp_tpl = setup_template(params);
        curline = msg;
	dbgprintf("MESSAGE=%s\n",msg);
        rrdp = rrdvalues + sprintf(rrdvalues, "%d", (int)tstamp);
        while (curline && (*curline))  {
		char *fsline, *p, *sep, *fname=NULL;
		char *columns[30];
		int columncount;
		char *volname = NULL;
		int i,flag,l,first,totnum;
		char *val;
		outp=rrdp;	
		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';

		fsline = xstrdup(curline); 
		dbgprintf("LINE=%s\n",fsline);
		
		for (columncount=0; (columncount<30); columncount++) columns[columncount] = NULL;
		for (totnum=0; varlist[totnum]; totnum++) ;
		columncount = 0; 
		p = strtok(fsline, ";");
		

		first=0;
		while (p) {
			if (first==0)  {
				fname=p;
				first=1;
			} else {
				sep=strchr(p,':');
				if (sep) {
					l=sep-p;
					sep++;
					dbgprintf("Checking %s len=%d\n",p,l);
					flag=0;
			        	i = 0; 
					while ((flag==0) && (varlist[i])) {
						dbgprintf("with %s\n",varlist[i]);
						if (strncmp(varlist[i], p, l) == 0) {
							columns[i]=sep;
							flag=1;
						}
						i++;
					}
				}
			}
			p = strtok(NULL, ";"); 
		}
		volname = xstrdup(fname);
		p = volname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }

		dbgprintf("netapp: host %s test %s name  %s \n",
			hostname, testname, volname);
		snprintf(rrdfn, sizeof(rrdfn)-1, "%s,%s.rrd", testname, volname);
		rrdfn[sizeof(rrdfn)-1] = '\0';
		for (i=0; varlist[i]; i++) {
			val=columns[i];
			if (val) {
				outp += sprintf(outp, ":%s",val);
				dbgprintf("var %s value %s \n", varlist[i], columns[i]);
			} else {
				outp += sprintf(outp, ":%s","U");
			}
		}

                create_and_update_rrd(hostname, rrdfn, params, netapp_tpl);

		if (volname) { xfree(volname); volname = NULL; }

		if (eoln) *eoln = '\n';
		xfree(fsline);
		curline = (eoln ? (eoln+1) : NULL);
	}
	return 0;

}

int do_netapp_extrastats_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{

static char *netapp_qtree_params[] = { "rrdcreate", rrdfn,
					"DS:nfs_ops:GAUGE:600:0:U",
					"DS:cifs_ops:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_aggregate_params[] = { "rrdcreate", rrdfn,
					"DS:total_transfers:GAUGE:600:0:U",
					"DS:user_reads:GAUGE:600:0:U",
					"DS:user_writes:GAUGE:600:0:U",
					"DS:cp_reads:GAUGE:600:0:U",
					"DS:user_read_blocks:GAUGE:600:0:U",
					"DS:user_write_blocks:GAUGE:600:0:U",
					"DS:cp_read_blocks:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_iscsi_params[] = { "rrdcreate", rrdfn,
					"DS:iscsi_ops:GAUGE:600:0:U",
					"DS:iscsi_write_data:GAUGE:600:0:U",
					"DS:iscsi_read_data:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_fcp_params[] = { "rrdcreate", rrdfn,
					"DS:fcp_ops:GAUGE:600:0:U",
					"DS:fcp_write_data:GAUGE:600:0:U",
					"DS:fcp_read_data:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_cifs_params[] = { "rrdcreate", rrdfn,
					"DS:cifs_ops:GAUGE:600:0:U",
					"DS:cifs_latency:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_volume_params[] = { "rrdcreate", rrdfn,
					"DS:avg_latency:GAUGE:600:0:U",
					"DS:total_ops:GAUGE:600:0:U",
					"DS:read_data:GAUGE:600:0:U",
					"DS:read_latency:GAUGE:600:0:U",
					"DS:read_ops:GAUGE:600:0:U",
					"DS:write_data:GAUGE:600:0:U",
					"DS:write_latency:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					"DS:other_latency:GAUGE:600:0:U",
					"DS:other_ops:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_lun_params[] = { "rrdcreate", rrdfn,
					"DS:read_ops:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					"DS:other_ops:GAUGE:600:0:U",
					"DS:read_data:GAUGE:600:0:U",
					"DS:write_data:GAUGE:600:0:U",
					"DS:queue_full:GAUGE:600:0:U",
					"DS:avg_latency:GAUGE:600:0:U",
					"DS:total_ops:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_nfsv3_params[] = { "rrdcreate", rrdfn,
					"DS:ops:GAUGE:600:0:U",
					"DS:read_latency:GAUGE:600:0:U",
					"DS:read_ops:GAUGE:600:0:U",
					"DS:write_latency:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_ifnet_params[] = { "rrdcreate", rrdfn,
					"DS:recv_packets:GAUGE:600:0:U",
					"DS:recv_errors:GAUGE:600:0:U",
					"DS:send_packets:GAUGE:600:0:U",
					"DS:send_errors:GAUGE:600:0:U",
					"DS:collisions:GAUGE:600:0:U",
					"DS:recv_data:GAUGE:600:0:U",
					"DS:send_data:GAUGE:600:0:U",
					"DS:recv_mcasts:GAUGE:600:0:U",
					"DS:send_mcasts:GAUGE:600:0:U",
					"DS:recv_drop_packets:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_processor_params[] = { "rrdcreate", rrdfn,
					"DS:proc_busy:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_disk_params[] = { "rrdcreate", rrdfn,
					"DS:total_transfers:GAUGE:600:0:U",
					"DS:user_read_chain:GAUGE:600:0:U",
					"DS:user_reads:GAUGE:600:0:U",
					"DS:user_write_chain:GAUGE:600:0:U",
					"DS:user_writes:GAUGE:600:0:U",
					"DS:cp_read_chain:GAUGE:600:0:U",
					"DS:cp_reads:GAUGE:600:0:U",
					"DS:gar_read_chain:GAUGE:600:0:U",
					"DS:gar_reads:GAUGE:600:0:U",
					"DS:gar_write_chain:GAUGE:600:0:U",
					"DS:gar_writes:GAUGE:600:0:U",
					"DS:user_read_latency:GAUGE:600:0:U",
					"DS:user_read_blocks:GAUGE:600:0:U",
					"DS:user_write_latency:GAUGE:600:0:U",
					"DS:user_write_blocks:GAUGE:600:0:U",
					"DS:cp_read_latency:GAUGE:600:0:U",
					"DS:cp_read_blocks:GAUGE:600:0:U",
					"DS:gar_read_latency:GAUGE:600:0:U",
					"DS:gar_read_blocks:GAUGE:600:0:U",
					"DS:gar_write_latency:GAUGE:600:0:U",
					"DS:gar_write_blocks:GAUGE:600:0:U",
					"DS:disk_busy:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };

static char *netapp_system_params[] = { "rrdcreate", rrdfn,
					"DS:nfs_ops:GAUGE:600:0:U",
					"DS:cifs_ops:GAUGE:600:0:U",
					"DS:http_ops:GAUGE:600:0:U",
					"DS:dafs_ops:GAUGE:600:0:U",
					"DS:fcp_ops:GAUGE:600:0:U",
					"DS:iscsi_ops:GAUGE:600:0:U",
					"DS:net_data_recv:GAUGE:600:0:U",
					"DS:net_data_sent:GAUGE:600:0:U",
					"DS:disk_data_read:GAUGE:600:0:U",
					"DS:disk_data_written:GAUGE:600:0:U",
					"DS:cpu_busy:GAUGE:600:0:U",
					"DS:avg_proc_busy:GAUGE:600:0:U",
					"DS:total_proc_busy:GAUGE:600:0:U",
					"DS:num_proc:GAUGE:600:0:U",
					"DS:time:GAUGE:600:0:U",
					"DS:uptime:GAUGE:600:0:U",
					rra1, rra2, rra3, rra4, NULL };
static char *qtree_test[] = { "nfs_ops", "cifs_ops" ,NULL };
static char *aggregate_test[] = { "total_transfers", "user_reads", "user_writes", "cp_reads", "user_read_blocks", "user_write_blocks", "cp_read_blocks" ,NULL };
static char *iscsi_test[] = { "iscsi_ops", "iscsi_write_data", "iscsi_read_data" ,NULL };
static char *fcp_test[] = { "fcp_ops", "fcp_write_data", "fcp_read_data" ,NULL };
static char *cifs_test[] = { "cifs_ops", "cifs_latency" ,NULL };
static char *volume_test[] = { "avg_latency", "total_ops", "read_data", "read_latency", "read_ops", "write_data", "write_latency", "write_ops", "other_latency", "other_ops" ,NULL };
static char *lun_test[] = { "read_ops", "write_ops", "other_ops", "read_data", "write_data", "queue_full", "avg_latency", "total_ops" ,NULL };
static char *nfsv3_test[] = { "nfsv3_ops", "nfsv3_read_latency", "nfsv3_read_ops", "nfsv3_write_latency", "nfsv3_write_ops" ,NULL };
static char *ifnet_test[] = { "recv_packets", "recv_errors", "send_packets", "send_errors", "collisions", "recv_data", "send_data", "recv_mcasts", "send_mcasts", "recv_drop_packets" ,NULL };
static char *processor_test[] = { "processor_busy" ,NULL };
static char *disk_test[] = { "total_transfers", "user_read_chain", "user_reads", "user_write_chain", "user_writes", "cp_read_chain", "cp_reads", "guarenteed_read_chain", "guaranteed_reads", "guarenteed_write_chain", "guaranteed_writes", "user_read_latency", "user_read_blocks", "user_write_latency", "user_write_blocks", "cp_read_latency", "cp_read_blocks", "guarenteed_read_latency", "guarenteed_read_blocks", "guarenteed_write_latency", "guarenteed_write_blocks", "disk_busy" ,NULL };
static char *system_test[] = { "nfs_ops", "cifs_ops", "http_ops", "dafs_ops", "fcp_ops", "iscsi_ops", "net_data_recv", "net_data_sent", "disk_data_read", "disk_data_written", "cpu_busy", "avg_processor_busy", "total_processor_busy", "num_processors", "time", "uptime" ,NULL };

        char *ifnetstr;
        char *qtreestr;
        char *aggregatestr;
        char *volumestr;
        char *lunstr;
        char *diskstr;

        splitmsg(msg);
        ifnetstr = getdata("ifnet");
        qtreestr = getdata("qtree");
        aggregatestr = getdata("aggregate");
        volumestr = getdata("volume");
        lunstr = getdata("lun");
        diskstr = getdata("disk");
	do_netapp_extratest_rrd(hostname,"xstatifnet",ifnetstr,tstamp,netapp_ifnet_params,ifnet_test);
	do_netapp_extratest_rrd(hostname,"xstatqtree",qtreestr,tstamp,netapp_qtree_params,qtree_test);
	do_netapp_extratest_rrd(hostname,"xstataggregate",aggregatestr,tstamp,netapp_aggregate_params,aggregate_test);
	do_netapp_extratest_rrd(hostname,"xstatvolume",volumestr,tstamp,netapp_volume_params,volume_test);
	do_netapp_extratest_rrd(hostname,"xstatlun",lunstr,tstamp,netapp_lun_params,lun_test);
	do_netapp_extratest_rrd(hostname,"xstatdisk",diskstr,tstamp,netapp_disk_params,disk_test);

	return 0;
}

