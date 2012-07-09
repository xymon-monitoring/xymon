/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2006 Francesco Duranti <fduranti@q8.it>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netapp_rcsid[] = "$Id$";

int do_netapp_stats_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *netapp_stats_params[] = { "DS:NETread:GAUGE:600:0:U",
                                       "DS:NETwrite:GAUGE:600:0:U",
                                       "DS:DISKread:GAUGE:600:0:U",
                                       "DS:DISKwrite:GAUGE:600:0:U",
                                       "DS:TAPEread:GAUGE:600:0:U",
                                       "DS:TAPEwrite:GAUGE:600:0:U",
                                       "DS:FCPin:GAUGE:600:0:U",
                                       "DS:FCPout:GAUGE:600:0:U",
                                        NULL };
static void *netapp_stats_tpl      = NULL;

	unsigned long netread=0, netwrite=0, diskread=0, diskwrite=0, taperead=0, tapewrite=0, fcpin=0, fcpout=0;
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (strstr(msg, "netapp.pl")) {
		setupfn("%s.rrd", testname);
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
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
			(int) tstamp, netread, netwrite, diskread, diskwrite, 
			taperead, tapewrite, fcpin, fcpout);
		create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_stats_params, netapp_stats_tpl);
	}
	return 0;
}


int do_netapp_cifs_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *netapp_cifs_params[] = { "DS:sessions:GAUGE:600:0:U",
                                      "DS:openshares:GAUGE:600:0:U",
                                      "DS:openfiles:GAUGE:600:0:U",
                                      "DS:locks:GAUGE:600:0:U",
                                      "DS:credentials:GAUGE:600:0:U",
                                      "DS:opendirectories:GAUGE:600:0:U",
                                      "DS:ChangeNotifies:GAUGE:600:0:U",
                                      "DS:sessionsusingsecuri:GAUGE:600:0:U",
                                      NULL };
static void *netapp_cifs_tpl      = NULL;

	unsigned long sess=0, share=0, file=0, lock=0, cred=0, dir=0, change=0, secsess=0;
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (strstr(msg, "netapp.pl")) {
		setupfn("%s.rrd", testname);
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
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
                      	(int) tstamp, sess, share, file, lock, cred, dir, change,secsess);
		create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_cifs_params, netapp_cifs_tpl);
	}
	return 0;
}


int do_netapp_ops_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *netapp_ops_params[] = { "DS:NFSops:GAUGE:600:0:U",
                                     "DS:CIFSops:GAUGE:600:0:U",
                                     "DS:iSCSIops:GAUGE:600:0:U",
                                     "DS:HTTPops:GAUGE:600:0:U",
                                     "DS:FCPops:GAUGE:600:0:U",
                                     "DS:Totalops:GAUGE:600:0:U",
                                     NULL };
static void *netapp_ops_tpl      = NULL;

	unsigned long nfsops=0, cifsops=0, httpops=0, iscsiops=0, fcpops=0, totalops=0;
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
	if (strstr(msg, "netapp.pl")) {
		setupfn("%s.rrd",testname);
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
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld",
                       	(int) tstamp, nfsops, cifsops, httpops, fcpops, iscsiops, totalops);
		create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_ops_params, netapp_ops_tpl);
	}
	return 0;
}

int do_netapp_snapmirror_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *netapp_snapmirror_params[] = { "DS:size:GAUGE:600:0:U", NULL };
static void *netapp_snapmirror_tpl      = NULL;

        char *eoln, *curline, *start, *end;
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
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

				setupfn2("%s,%s.rrd", testname, curline);
				snprintf(rrdvalues, sizeof(rrdvalues), "%d:%lld", (int)tstamp, size);
				create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_snapmirror_params, netapp_snapmirror_tpl);
				*(--equalsign)='=';
			}
	                if (eoln) *eoln = '\n';
	                curline = (eoln ? (eoln+1) : NULL);
		}
                *end='-';
	}
	return 0;
}


int do_netapp_snaplist_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *netapp_snaplist_params[] = { "DS:youngsize:GAUGE:600:0:U", "DS:oldsize:GAUGE:600:0:U", NULL };
static void *netapp_snaplist_tpl      = NULL;

        char *eoln, *curline, *start, *end;
	dbgprintf("netapp: host %s test %s\n",hostname, testname);
	
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

			setupfn2("%s,%s.rrd", testname, volname);
                        snprintf(rrdvalues, sizeof(rrdvalues), "%d:%lld:%lld", (int)tstamp, young, old);
                        create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_snaplist_params, netapp_snaplist_tpl);
                	if (volname) { xfree(volname); volname = NULL; }

	                if (eoln) *eoln = '\n';
			xfree(fsline);
	                curline = (eoln ? (eoln+1) : NULL);
		}
                *end='-';
	}
	return 0;
}

int do_netapp_extratest_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp, char *params[],char *varlist[])
{
static void *netapp_tpl      = NULL;

	char *outp;	
	char *eoln,*curline;
	char *rrdp;
	/* Setup the update string */

	netapp_tpl = setup_template(params);
        curline = msg;

	dbgprintf("MESSAGE=%s\n",msg);
        rrdp = rrdvalues + snprintf(rrdvalues, sizeof(rrdvalues), "%d", (int)tstamp);
        while (curline && (*curline))  {
		char *fsline, *p, *sep, *fname=NULL;
		char *columns[30];
		int columncount;
		char *volname = NULL;
		int i,flag,l,first,totnum;
		char *val;
		outp=rrdp;	
		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';
		if ((eoln == curline) || (strstr(curline,"netapp.pl"))) {
			dbgprintf("SKIP LINE=\n",curline);
			goto nextline;
		}
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
		setupfn2("%s,%s.rrd", testname, volname);
		for (i=0; varlist[i]; i++) {
			val=columns[i];
			if (val) {
				outp += snprintf(outp, sizeof(rrdvalues)-(outp-rrdvalues), ":%s",val);
				dbgprintf("var %s value %s \n", varlist[i], columns[i]);
			} else {
				outp += snprintf(outp, sizeof(rrdvalues)-(outp-rrdvalues), ":%s","U");
			}
		}

                create_and_update_rrd(hostname, testname, classname, pagepaths, params, netapp_tpl);

		if (volname) { xfree(volname); volname = NULL; }

		if (eoln) *eoln = '\n';
		xfree(fsline);
nextline:
		curline = (eoln ? (eoln+1) : NULL);
	}
	return 0;

}

int do_netapp_extrastats_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{

static char *netapp_qtree_params[] = { "DS:nfs_ops:GAUGE:600:0:U", "DS:cifs_ops:GAUGE:600:0:U", NULL };

static char *netapp_aggregate_params[] = {
					"DS:total_transfers:GAUGE:600:0:U",
					"DS:user_reads:GAUGE:600:0:U",
					"DS:user_writes:GAUGE:600:0:U",
					"DS:cp_reads:GAUGE:600:0:U",
					"DS:user_read_blocks:GAUGE:600:0:U",
					"DS:user_write_blocks:GAUGE:600:0:U",
					"DS:cp_read_blocks:GAUGE:600:0:U",
					NULL };

static char *netapp_iscsi_params[] = { "DS:iscsi_ops:GAUGE:600:0:U",
					"DS:iscsi_write_data:GAUGE:600:0:U",
					"DS:iscsi_read_data:GAUGE:600:0:U",
					NULL };

static char *netapp_fcp_params[] = { "DS:fcp_ops:GAUGE:600:0:U",
					"DS:fcp_write_data:GAUGE:600:0:U",
					"DS:fcp_read_data:GAUGE:600:0:U",
					NULL };

static char *netapp_cifs_params[] = { "DS:cifs_ops:GAUGE:600:0:U",
					"DS:cifs_latency:GAUGE:600:0:U",
					NULL };

static char *netapp_volume_params[] = { "DS:avg_latency:GAUGE:600:0:U",
					"DS:total_ops:GAUGE:600:0:U",
					"DS:read_data:GAUGE:600:0:U",
					"DS:read_latency:GAUGE:600:0:U",
					"DS:read_ops:GAUGE:600:0:U",
					"DS:write_data:GAUGE:600:0:U",
					"DS:write_latency:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					"DS:other_latency:GAUGE:600:0:U",
					"DS:other_ops:GAUGE:600:0:U",
					NULL };

static char *netapp_lun_params[] = { "DS:read_ops:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					"DS:other_ops:GAUGE:600:0:U",
					"DS:read_data:GAUGE:600:0:U",
					"DS:write_data:GAUGE:600:0:U",
					"DS:queue_full:GAUGE:600:0:U",
					"DS:avg_latency:GAUGE:600:0:U",
					"DS:total_ops:GAUGE:600:0:U",
					NULL };

static char *netapp_nfsv3_params[] = { "DS:ops:GAUGE:600:0:U",
					"DS:read_latency:GAUGE:600:0:U",
					"DS:read_ops:GAUGE:600:0:U",
					"DS:write_latency:GAUGE:600:0:U",
					"DS:write_ops:GAUGE:600:0:U",
					NULL };

static char *netapp_ifnet_params[] = { "DS:recv_packets:GAUGE:600:0:U",
					"DS:recv_errors:GAUGE:600:0:U",
					"DS:send_packets:GAUGE:600:0:U",
					"DS:send_errors:GAUGE:600:0:U",
					"DS:collisions:GAUGE:600:0:U",
					"DS:recv_data:GAUGE:600:0:U",
					"DS:send_data:GAUGE:600:0:U",
					"DS:recv_mcasts:GAUGE:600:0:U",
					"DS:send_mcasts:GAUGE:600:0:U",
					"DS:recv_drop_packets:GAUGE:600:0:U",
					NULL };

static char *netapp_processor_params[] = { "DS:proc_busy:GAUGE:600:0:U", NULL };

static char *netapp_disk_params[] = { "DS:total_transfers:GAUGE:600:0:U",
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
					NULL };

static char *netapp_system_params[] = { "DS:nfs_ops:GAUGE:600:0:U",
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
					NULL };
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
	do_netapp_extratest_rrd(hostname,"xstatifnet",classname,pagepaths,ifnetstr,tstamp,netapp_ifnet_params,ifnet_test);
	do_netapp_extratest_rrd(hostname,"xstatqtree",classname,pagepaths,qtreestr,tstamp,netapp_qtree_params,qtree_test);
	do_netapp_extratest_rrd(hostname,"xstataggregate",classname,pagepaths,aggregatestr,tstamp,netapp_aggregate_params,aggregate_test);
	do_netapp_extratest_rrd(hostname,"xstatvolume",classname,pagepaths,volumestr,tstamp,netapp_volume_params,volume_test);
	do_netapp_extratest_rrd(hostname,"xstatlun",classname,pagepaths,lunstr,tstamp,netapp_lun_params,lun_test);
	do_netapp_extratest_rrd(hostname,"xstatdisk",classname,pagepaths,diskstr,tstamp,netapp_disk_params,disk_test);

	return 0;
}


int do_netapp_disk_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
       static char *netapp_disk_params[] = { "DS:pct:GAUGE:600:0:U", "DS:used:GAUGE:600:0:U", NULL };
       static rrdtpldata_t *netapp_disk_tpl      = NULL;

       char *eoln, *curline;
       static int ptnsetup = 0;
       static pcre *inclpattern = NULL;
       static pcre *exclpattern = NULL;
       int newdfreport;

	newdfreport = (strstr(msg,"netappnewdf") != NULL);

       if (netapp_disk_tpl == NULL) netapp_disk_tpl = setup_template(netapp_disk_params);

       if (!ptnsetup) {
               const char *errmsg;
               int errofs;
               char *ptn;

               ptnsetup = 1;
               ptn = getenv("RRDDISKS");
               if (ptn && strlen(ptn)) {
                       inclpattern = pcre_compile(ptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
                       if (!inclpattern) errprintf("PCRE compile of RRDDISKS='%s' failed, error %s, offset %d\n",
                                                   ptn, errmsg, errofs);
               }
               ptn = getenv("NORRDDISKS");
               if (ptn && strlen(ptn)) {
                       exclpattern = pcre_compile(ptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
                       if (!exclpattern) errprintf("PCRE compile of NORRDDISKS='%s' failed, error %s, offset %d\n",
                                                   ptn, errmsg, errofs);
               }
       }

       /*
        * Francesco Duranti noticed that if we use the "/group" option
        * when sending the status message, this tricks the parser to
        * create an extra filesystem called "/group". So skip the first
        * line - we never have any disk reports there anyway.
        */
       curline = strchr(msg, '\n'); if (curline) curline++;

       while (curline)  {
               char *fsline, *p;
               char *columns[20];
               int columncount;
               char *diskname = NULL;
               int pused = -1;
               int wanteddisk = 1;
               long long aused = 0;
               /* FD: Using double instead of long long because we can have decimal on Netapp and DbCheck */
               double dused = 0;
               /* FD: used to add a column if the filesystem is named "snap reserve" for netapp.pl */
               int snapreserve=0;

               eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';


               /* FD: netapp.pl snapshot line that start with "snap reserve" need a +1 */
               if (strstr(curline, "snap reserve")) snapreserve=1;

               /* All clients except AS/400 and DBCHECK report the mount-point with slashes - ALSO Win32 clients. */
               if (strchr(curline, '/') == NULL) goto nextline;

               /* red/yellow filesystems show up twice */
               if (*curline == '&') goto nextline;
               if ((strstr(curline, " red ") || strstr(curline, " yellow "))) goto nextline;

               for (columncount=0; (columncount<20); columncount++) columns[columncount] = "";
               fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, " ");
               while (p && (columncount < 20)) { columns[columncount++] = p; p = strtok(NULL, " "); }

               /* FD: Name column can contain "spaces" so it could be split in multiple
                  columns, create a unique string from columns[5] that point to the
                  complete disk name
               */
               while (columncount-- > 6+snapreserve) {
                       p = strchr(columns[columncount-1],0);
                       if (p) *p = '_';
               }
               /* FD: Add an initial "/" to qtree and quotas */
	       if (newdfreport) {
			diskname = xstrdup(columns[0]);
		} else if (*columns[5+snapreserve] != '/') {
                       diskname=xmalloc(strlen(columns[5+snapreserve])+2);
                       sprintf(diskname,"/%s",columns[5+snapreserve]);
               } else {
                       diskname = xstrdup(columns[5+snapreserve]);
               }
               p = strchr(columns[4+snapreserve], '%'); if (p) *p = ' ';
               pused = atoi(columns[4+snapreserve]);
               p = columns[2+snapreserve] + strspn(columns[2+snapreserve], "0123456789.");
               /* Using double instead of long long because we can have decimal */
               dused = str2ll(columns[2+snapreserve], NULL);
               /* snapshot and qtree contains M/G/T
                  Convert to KB if there's a modifier after the numbers
               */
               if (*p == 'M') dused *= 1024;
               else if (*p == 'G') dused *= (1024*1024);
               else if (*p == 'T') dused *= (1024*1024*1024);
               aused=(long long) dused;


               /* Check include/exclude patterns */
               wanteddisk = 1;
               if (exclpattern) {
                       int ovector[30];
                       int result;

                       result = pcre_exec(exclpattern, NULL, diskname, strlen(diskname),
                                          0, 0, ovector, (sizeof(ovector)/sizeof(int)));

                       wanteddisk = (result < 0);
               }
               if (wanteddisk && inclpattern) {
                       int ovector[30];
                       int result;

                       result = pcre_exec(inclpattern, NULL, diskname, strlen(diskname),
                                          0, 0, ovector, (sizeof(ovector)/sizeof(int)));

                       wanteddisk = (result >= 0);
               }

               if (wanteddisk && diskname && (pused != -1)) {
                       p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
                       if (strcmp(diskname, ",") == 0) {
                               diskname = xrealloc(diskname, 6);
                               strcpy(diskname, ",root");
                       }

                       /*
                        * Use testname here.
                        * The disk-handler also gets data from NetAPP inode- and qtree-messages,
                        * that are virtually identical to the disk-messages. So lets just handle
                        * all of it by using the testname as part of the filename.
                        */
                       setupfn2("%s%s.rrd", testname, diskname);
                       snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d:%lld", (int)tstamp, pused, aused);
                       create_and_update_rrd(hostname, testname, classname, pagepaths, netapp_disk_params, netapp_disk_tpl);
               }
               if (diskname) { xfree(diskname); diskname = NULL; }

               if (eoln) *eoln = '\n';
               xfree(fsline);

nextline:
               curline = (eoln ? (eoln+1) : NULL);
       }

       return 0;
}


