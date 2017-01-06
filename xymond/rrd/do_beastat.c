/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2006 Francesco Duranti <fduranti@q8.it>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char beastat_rcsid[] = "$Id$";

int do_beastat_jta_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *beastat_jta_params[] = { "DS:ActiveTrans:GAUGE:600:0:U",
					"DS:SecondsActive:DERIVE:600:0:U",
					"DS:TransAbandoned:DERIVE:600:0:U",
					"DS:TransCommitted:DERIVE:600:0:U",
					"DS:TransHeuristics:DERIVE:600:0:U",
					"DS:TransRBackApp:DERIVE:600:0:U",
					"DS:TransRBackResource:DERIVE:600:0:U",
					"DS:TransRBackSystem:DERIVE:600:0:U",
					"DS:TransRBackTimeout:DERIVE:600:0:U",
					"DS:TransRBack:DERIVE:600:0:U",
					"DS:TransTotCount:DERIVE:600:0:U",
                                     NULL };
static void *beastat_jta_tpl      = NULL;

	unsigned long heapfree=0, heapsize=0;
	unsigned long acttrans=0, secact=0, trab=0, trcomm=0, trheur=0, totot=0;
	unsigned long trrbapp=0, trrbres=0, trrbsys=0, trrbto=0, trrb=0, trtot=0;
	
	dbgprintf("beastat: host %s test %s\n",hostname, testname);

	if (strstr(msg, "beastat.pl")) {
		setupfn("%s.rrd",testname);
		if (beastat_jta_tpl == NULL) beastat_jta_tpl = setup_template(beastat_jta_params);
		acttrans=get_long_data(msg,"ActiveTransactionsTotalCount");
		secact=get_long_data(msg,"SecondsActiveTotalCount");
		trab=get_long_data(msg,"TransactionAbandonedTotalCount");
		trcomm=get_long_data(msg,"TransactionCommittedTotalCount");
		trheur=get_long_data(msg,"TransactionHeuristicsTotalCount");
		trrbapp=get_long_data(msg,"TransactionRolledBackAppTotalCount");
		trrbres=get_long_data(msg,"TransactionRolledBackResourceTotalCount");
		trrbsys=get_long_data(msg,"TransactionRolledBackSystemTotalCount");
		trrbto=get_long_data(msg,"TransactionRolledBackTimeoutTotalCount");
		trrb=get_long_data(msg,"TransactionRolledBackTotalCount");
		trtot=get_long_data(msg,"TransactionTotalCount");
		dbgprintf("beastat: host %s test %s acttrans %ld secact %ld\n",
			 hostname, testname, acttrans, secact);
		dbgprintf("beastat: host %s test %s TRANS: aband %ld comm %ld heur %ld total\n",
			hostname, testname, trab, trcomm, trheur, trtot);
		dbgprintf("beastat: host %s test %s RB: app %ld res %ld sys %ld timeout %ld total %ld\n",
			hostname, testname, trrbapp, trrbres, trrbsys, trrbto, trrb);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
			(int) tstamp, acttrans, secact, trab, trcomm, trheur, trrbapp, 
			trrbres, trrbsys, trrbto, trrb, trtot);
		create_and_update_rrd(hostname, testname, classname, pagepaths, beastat_jta_params, beastat_jta_tpl);
	}
	return 0;
}


int do_beastat_jvm_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *beastat_jvm_params[] = { "DS:HeapFreeCurrent:GAUGE:600:0:U",
                                     "DS:HeapSizeCurrent:GAUGE:600:0:U",
                                     NULL };
static void *beastat_jvm_tpl      = NULL;

	unsigned long heapfree=0, heapsize=0;
	
	dbgprintf("beastat: host %s test %s\n",hostname, testname);

	if (strstr(msg, "beastat.pl")) {
		setupfn("%s.rrd",testname);
		if (beastat_jvm_tpl == NULL) beastat_jvm_tpl = setup_template(beastat_jvm_params);
		heapfree=get_long_data(msg, "HeapFreeCurrent");
		heapsize=get_long_data(msg,"HeapSizeCurrent");
		dbgprintf("beastat: host %s test %s heapfree %ld heapsize %ld\n",
			hostname, testname, heapfree, heapsize);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld",
			(int) tstamp, heapfree, heapsize);
		create_and_update_rrd(hostname, testname, classname, pagepaths, beastat_jvm_params, beastat_jvm_tpl);
	}
	return 0;
}


int do_beastat_jms_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *beastat_jms_params[] = { "DS:CurrConn:GAUGE:600:0:U",
                                     "DS:HighConn:GAUGE:600:0:U",
                                     "DS:TotalConn:DERIVE:600:0:U",
                                     "DS:CurrJMSSrv:GAUGE:600:0:U",
                                     "DS:HighJMSSrv:GAUGE:600:0:U",
                                     "DS:TotalJMSSrv:DERIVE:600:0:U",
                                     NULL };
static void *beastat_jms_tpl      = NULL;

        unsigned long conncurr=0, connhigh=0, conntotal=0, jmscurr=0, jmshigh=0, jmstotal=0;
	
	dbgprintf("beastat: host %s test %s\n",hostname, testname);

	if (strstr(msg, "beastat.pl")) {
		setupfn("%s.rrd",testname);
		if (beastat_jms_tpl == NULL) beastat_jms_tpl = setup_template(beastat_jms_params);
		conncurr=get_long_data(msg, "ConnectionsCurrentCount");
		connhigh=get_long_data(msg,"ConnectionsHighCount");
		conntotal=get_long_data(msg,"ConnectionsTotalCount");
		jmscurr=get_long_data(msg,"JMSServersCurrentCount");
		jmshigh=get_long_data(msg,"JMSServersHighCount");
		jmstotal=get_long_data(msg,"JMSServersTotalCount");
		dbgprintf("beastat: host %s test %s conncurr %ld connhigh %ld conntotal %ld\n",
			hostname, testname, conncurr, connhigh, conntotal);
		dbgprintf("beastat: host %s test %s jmscurr %ld jmshigh %ld jmstotal %ld\n",
			hostname, testname, jmscurr, jmshigh,jmstotal);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld",
			(int) tstamp, conncurr, connhigh, conntotal, jmscurr, jmshigh, jmstotal);
		create_and_update_rrd(hostname, testname, classname, pagepaths, beastat_jms_params, beastat_jms_tpl);
	}
	return 0;
}

int do_beastat_exec_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *beastat_exec_params[] = { "DS:ExecThrCurrIdleCnt:GAUGE:600:0:U",
					"DS:ExecThrTotalCnt:GAUGE:600:0:U",
					"DS:PendReqCurrCnt:GAUGE:600:0:U",
					"DS:ServReqTotalCnt:DERIVE:600:0:U",
					NULL };
static void *beastat_exec_tpl		= NULL;
static char *checktest			= "Type=ExecuteQueueRuntime";

	char *curline;
	char *eoln;		
	dbgprintf("beastat: host %s test %s\n",hostname, testname);

	if (strstr(msg, "beastat.pl")) {
		if (beastat_exec_tpl == NULL) beastat_exec_tpl = setup_template(beastat_exec_params);
/*
---- Full Status Report ----
	Type=ExecuteQueueRuntime - Location=admin - Name=weblogic.kernel.System
*/
                curline=strstr(msg, "---- Full Status Report ----");
                if (curline) {
			eoln = strchr(curline, '\n');
                	curline = (eoln ? (eoln+1) : NULL);
		}
		while (curline)	{
			unsigned long currthr=0, totthr=0,currprq=0,totservrq=0;
			char *start=NULL, *execname=NULL, *nameptr=NULL;
			if ((start = strstr(curline,checktest))==NULL) break;
			if ((eoln = strchr(start, '\n')) == NULL) break; 
			*eoln = '\0';
			if ((nameptr=strstr(start,"Name=")) == NULL ) {
				dbgprintf("do_beastat.c: No name found in  host %s test %s line %s\n",
					hostname,testname,start);
				goto nextline;
			}
			execname=xstrdup(nameptr+5);
	                *eoln = '\n';
			start=eoln+1;
			if ((eoln = strstr(start,checktest))==NULL) eoln=strstr(start,"dbcheck.pl");
			if (eoln)  *(--eoln)='\0';
			setupfn2("%s,%s.rrd",testname,execname);
			currthr=get_long_data(start, "ExecuteThreadCurrentIdleCount");
			totthr=get_long_data(start,"ExecuteThreadTotalCount");
			currprq=get_long_data(start,"PendingRequestCurrentCount");
			totservrq=get_long_data(start,"ServicedRequestTotalCount");
			dbgprintf("beastat: host %s test %s name %s currthr %ld totthr %ld currprq %ld totservrq %ld\n",
				hostname, testname, execname, currthr, totthr, currprq, totservrq);
			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld",
				(int) tstamp, currthr, totthr, currprq, totservrq);
			create_and_update_rrd(hostname, testname, classname, pagepaths, beastat_exec_params, beastat_exec_tpl);
			if (execname) { xfree(execname); execname = NULL; }
nextline:
			if (eoln) *(eoln)='\n';
			curline = (eoln ? (eoln+1) : NULL);
		}
	}
	return 0;
}

int do_beastat_jdbc_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *beastat_jdbc_params[] = { "DS:ActConnAvgCnt:GAUGE:600:0:U",
					"DS:ActConnCurrCnt:GAUGE:600:0:U",
					"DS:ActConnHighCnt:GAUGE:600:0:U",
					"DS:WtForConnCurrCnt:GAUGE:600:0:U",
					"DS:ConnDelayTime:GAUGE:600:0:U",
					"DS:ConnLeakProfileCnt:GAUGE:600:0:U",
					"DS:LeakedConnCnt:GAUGE:600:0:U",
					"DS:MaxCapacity:GAUGE:600:0:U",
					"DS:NumAvailable:GAUGE:600:0:U",
					"DS:NumUnavailable:GAUGE:600:0:U",
					"DS:HighNumAvailable:GAUGE:600:0:U",
					"DS:HighNumUnavailable:GAUGE:600:0:U",
					"DS:WaitSecHighCnt:GAUGE:600:0:U",
					"DS:ConnTotalCnt:DERIVE:600:0:U",
					"DS:FailToReconnCnt:DERIVE:600:0:U",
					"DS:WaitForConnHighCnt:GAUGE:600:0:U",
					NULL };
static void *beastat_jdbc_tpl		= NULL;
static char *checktest			= "Type=JDBCConnectionPoolRuntime";

	char *curline;
	char *eoln;		
	dbgprintf("beastat: host %s test %s\n",hostname, testname);

	if (strstr(msg, "beastat.pl")) {
		if (beastat_jdbc_tpl == NULL) beastat_jdbc_tpl = setup_template(beastat_jdbc_params);
/*
---- Full Status Report ----
	Type=ExecuteQueueRuntime - Location=admin - Name=weblogic.kernel.System
*/
                curline=strstr(msg, "---- Full Status Report ----");
                if (curline) {
			eoln = strchr(curline, '\n');
                	curline = (eoln ? (eoln+1) : NULL);
		}
		while (curline)	{
			unsigned long acac=0, accc=0, achc=0, wfccc=0, cdt=0, clpc=0, lcc=0; 
			unsigned long mc=0, na=0, nu=0, hna=0, hnu=0, wshc=0, ctc=0, ftrc=0, wfchc=0;
			char *start=NULL, *execname=NULL, *nameptr=NULL;
			if ((start = strstr(curline,checktest))==NULL) break;
			if ((eoln = strchr(start, '\n')) == NULL) break; 
			*eoln = '\0';
			if ((nameptr=strstr(start,"Name=")) == NULL ) {
				dbgprintf("do_beastat.c: No name found in  host %s test %s line %s\n",
					hostname,testname,start);
				goto nextline;
			}
			execname=xstrdup(nameptr+5);
	                *eoln = '\n';
			start=eoln+1;
			if ((eoln = strstr(start,checktest))==NULL) eoln=strstr(start,"dbcheck.pl");
			if (eoln)  *(--eoln)='\0';
			setupfn2("%s,%s.rrd",testname,execname);
			acac=get_long_data(start,"ActiveConnectionsAverageCount");
			accc=get_long_data(start,"ActiveConnectionsCurrentCount");
			achc=get_long_data(start,"ActiveConnectionsHighCount");
			wfccc=get_long_data(start,"WaitingForConnectionCurrentCount");
			cdt=get_long_data(start,"ConnectionDelayTime");
			clpc=get_long_data(start,"ConnectionLeakProfileCount");
			lcc=get_long_data(start,"LeakedConnectionCount");
			mc=get_long_data(start,"MaxCapacity");
			na=get_long_data(start,"NumAvailable");
			nu=get_long_data(start,"NumUnavailable");
			hna=get_long_data(start,"HighestNumAvailable");
			hnu=get_long_data(start,"HighestNumUnavailable");
			wshc=get_long_data(start,"WaitSecondsHighCount");
			ctc=get_long_data(start,"ConnectionsTotalCount");
			ftrc=get_long_data(start,"FailuresToReconnectCount");
			wfchc=get_long_data(start,"WaitingForConnectionHighCount");


			dbgprintf("beastat: host %s test %s name %s acac %ld accc %ld achc %ld wfccc %ld cdt %ld clpc %ld lcc %ld\n", hostname, testname, execname, acac, accc, achc, wfccc, cdt, clpc, lcc);
			dbgprintf("beastat: host %s test %s name %s mc %ld na %ld nu %ld hna %ld hnu %ld wshc %ld ctc %ld ftrc %ld wfchc %ld\n",hostname, testname, execname, mc, na, nu, hna, hnu, wshc, ctc, ftrc, wfchc);

			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld",
				(int) tstamp, acac, accc, achc, wfccc, cdt, clpc, lcc, 
				mc, na, nu, hna, hnu, wshc, ctc, ftrc, wfchc);
			create_and_update_rrd(hostname, testname, classname, pagepaths, beastat_jdbc_params, beastat_jdbc_tpl);
			if (execname) { xfree(execname); execname = NULL; }
nextline:
			if (eoln) *(eoln)='\n';
			curline = (eoln ? (eoln+1) : NULL);
		}
	}
	return 0;
}

