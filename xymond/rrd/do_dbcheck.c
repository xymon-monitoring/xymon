/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2006 Francesco Duranti <fduranti@q8.it>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char dbcheck_rcsid[] = "$Id$";

int do_dbcheck_memreq_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *dbcheck_memreq_params[] = { 
                                     "DS:ResFree:GAUGE:600:0:U",
                                     "DS:ResAvgFree:GAUGE:600:0:U",
                                     "DS:ResUsed:GAUGE:600:0:U",
                                     "DS:ResAvgUsed:GAUGE:600:0:U",
                                     "DS:ReqFail:DERIVE:600:0:U",
                                     "DS:FailSize:GAUGE:600:0:U",
                                     NULL };
static void *dbcheck_memreq_tpl      = NULL;

	unsigned long free=0,used=0,reqf=0,fsz=0;	
	double avfr=0,avus=0;
	char *start,*end;
	dbgprintf("dbcheck: host %s test %s\n",hostname, testname);
	if (strstr(msg, "dbcheck.pl")) {
		if (dbcheck_memreq_tpl == NULL) dbcheck_memreq_tpl = setup_template(dbcheck_memreq_params);
                if ((start=strstr(msg, "<!--"))==NULL) return 0;
                if ((end=strstr(start,"-->"))==NULL) return 0;
		*end='\0';
		free=get_long_data(start,"ResFree");
		avfr=get_double_data(start,"ResAvgFree");
		used=get_long_data(start,"ResUsed");
		avus=get_double_data(start,"ResAvgUsed");
		reqf=get_long_data(start,"ReqFail");
		fsz=get_long_data(start,"FailSize");
		*end='-';
		dbgprintf("dbcheck: host %s test %s free %ld avgfree %f\n",
			hostname, testname, free, avfr);
		dbgprintf("dbcheck: host %s test %s used %ld avgused %f\n",
			hostname, testname, used, avus);
		dbgprintf("dbcheck: host %s test %s reqfail %ld failsize %ld\n",
			hostname, testname, reqf, fsz);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%f:%ld:%f:%ld:%ld",
			(int) tstamp, free, avfr, used, avus, reqf,fsz);
		setupfn("%s.rrd",testname);
		create_and_update_rrd(hostname, testname, classname, pagepaths, dbcheck_memreq_params, dbcheck_memreq_tpl);
	}
	return 0;
}

int do_dbcheck_hitcache_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
static char *dbcheck_hitcache_params[] = { "DS:PinSQLArea:GAUGE:600:0:100",
                                     "DS:PinTblProc:GAUGE:600:0:100",
                                     "DS:PinBody:GAUGE:600:0:100",
                                     "DS:PinTrigger:GAUGE:600:0:100",
                                     "DS:HitSQLArea:GAUGE:600:0:100",
                                     "DS:HitTblProc:GAUGE:600:0:100",
                                     "DS:HitBody:GAUGE:600:0:100",
                                     "DS:HitTrigger:GAUGE:600:0:100",
                                     "DS:BlBuffHit:GAUGE:600:0:100",
                                     "DS:RowCache:GAUGE:600:0:100",
                                     NULL };
static void *dbcheck_hitcache_tpl      = NULL;

	double pinsql=0, pintbl=0, pinbody=0, pintrig=0, hitsql=0, hittbl=0, hitbody=0, hittrig=0, blbuff=0, rowchache=0;
	dbgprintf("dbcheck: host %s test %s\n",hostname, testname);
	
	if (strstr(msg, "dbcheck.pl")) {
		setupfn("%s.rrd",testname);
		if (dbcheck_hitcache_tpl == NULL) dbcheck_hitcache_tpl = setup_template(dbcheck_hitcache_params);
		pinsql=get_double_data(msg,"PinSQLArea");
		pintbl=get_double_data(msg,"PinTblProc");
		pinbody=get_double_data(msg,"PinBody");
		pintrig=get_double_data(msg,"PinTrigger");
		hitsql=get_double_data(msg,"HitSQLArea");
		hittbl=get_double_data(msg,"HitTblProc");
		hitbody=get_double_data(msg,"HitBody");
		hittrig=get_double_data(msg,"HitTrigger");
		blbuff=get_double_data(msg,"BlBuffHit");
		rowchache=get_double_data(msg,"RowCache");
		dbgprintf("dbcheck: host %s test %s pinsql %5.2f pintbl %5.2f pinbody %5.2f pintrig %5.2f\n",
			hostname, testname, pinsql, pintbl, pinbody, pintrig);
		dbgprintf("dbcheck: host %s test %s hitsql %5.2f hittbl %5.2f hitbody %5.2f hittrig %5.2f\n",
			hostname, testname, hitsql, hittbl, hitbody, hittrig);
		dbgprintf("dbcheck: host %s test %s blbuff %5.2f rowchache %5.2f\n",
			hostname, testname, blbuff, rowchache);
		sprintf(rrdvalues, "%d:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f",
			(int) tstamp, pinsql, pintbl, pinbody, pintrig,
			hitsql, hittbl, hitbody, hittrig, blbuff, rowchache);
		create_and_update_rrd(hostname, testname, classname, pagepaths, dbcheck_hitcache_params, dbcheck_hitcache_tpl);
	}
	return 0;
}


int do_dbcheck_session_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{

static char *dbcheck_session_params[] = { "DS:MaxSession:GAUGE:600:0:U",
                                     "DS:CurrSession:GAUGE:600:0:U",
                                     "DS:SessUsedPct:GAUGE:600:0:100",
                                     "DS:MaxProcs:GAUGE:600:0:U",
                                     "DS:CurrProcs:GAUGE:600:0:U",
                                     "DS:ProcsUsedPct:GAUGE:600:0:100",
                                     NULL };
static void *dbcheck_session_tpl      = NULL;

        unsigned long maxsess=0, currsess=0, maxproc=0, currproc=0 ;
	double pctsess=0, pctproc=0;
	dbgprintf("dbcheck: host %s test %s\n",hostname, testname);
	
	if (strstr(msg, "dbcheck.pl")) {
		setupfn("%s.rrd",testname);
		if (dbcheck_session_tpl == NULL) dbcheck_session_tpl = setup_template(dbcheck_session_params);
		maxsess=get_long_data(msg, "MaxSession");
		currsess=get_long_data(msg,"CurrSession");
		pctsess=get_double_data(msg,"SessUsedPct");
		maxproc=get_long_data(msg,"MaxProcs");
		currproc=get_long_data(msg,"CurrProcs");
		pctproc=get_double_data(msg,"ProcsUsedPct");
		dbgprintf("dbcheck: host %s test %s maxsess %ld currsess %ld pctsess %5.2f\n",
		hostname, testname, maxsess, currsess, pctsess);
			dbgprintf("dbcheck: host %s test %s maxproc %ld currproc %ld pctproc %5.2f\n",
		hostname, testname, maxproc, currproc, pctproc);
                        sprintf(rrdvalues, "%d:%ld:%ld:%5.2f:%ld:%ld:%5.2f",
                       	(int) tstamp, maxsess, currsess, pctsess, maxproc, currproc, pctproc);
		create_and_update_rrd(hostname, testname, classname, pagepaths, dbcheck_session_params, dbcheck_session_tpl);
	}
	return 0;
}

int do_dbcheck_rb_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
/* This check can be done in slow mode so put a long heartbeat */
static char *dbcheck_rb_params[] = { "DS:pct:GAUGE:28800:0:100", NULL };
static void *dbcheck_rb_tpl    = NULL;

        char *curline;
        char *eoln;
        dbgprintf("dbcheck: host %s test %s\n",hostname, testname);

        if (strstr(msg, "dbcheck.pl")) {
                if (dbcheck_rb_tpl == NULL) dbcheck_rb_tpl = setup_template(dbcheck_rb_params);
                curline=strstr(msg, "Rollback Checking");
                if (curline) {
                        eoln = strchr(curline, '\n');
                        curline = (eoln ? (eoln+1) : NULL);
                }
                while (curline) {
                        float pct=0;
                        char *execname=NULL;
			char *start;
			if ((start = strstr(curline,"ROLLBACK")) == NULL) break;
                        if ((eoln = strchr(start, '\n')) == NULL) break;
                        *eoln = '\0';
			dbgprintf("dbcheck: host %s test %s line %s\n", hostname, testname, start);
			execname=xmalloc(strlen(start));
                        if ( sscanf(start,"ROLLBACK percentage for %s is %f",execname,&pct) !=2) goto nextline;
                        setupfn2("%s,%s.rrd",testname,execname);
                        dbgprintf("dbcheck: host %s test %s name %s pct %5.2f\n", hostname, testname, execname, pct);
                        sprintf(rrdvalues, "%d:%5.2f", (int) tstamp, pct);
                        create_and_update_rrd(hostname, testname, classname, pagepaths, dbcheck_rb_params, dbcheck_rb_tpl);
nextline:
                        if (execname) { xfree(execname); execname = NULL; }
                        if (eoln) *(eoln)='\n';
                        curline = (eoln ? (eoln+1) : NULL);
                }
        }
        return 0;
}

int do_dbcheck_invobj_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
/* This check can be done in slow mode so put a long heartbeat */
static char *dbcheck_invobj_params[] = { "DS:red:GAUGE:28800:0:U",
                                        "DS:yellow:GAUGE:28800:0:U",
                                        "DS:green:GAUGE:28800:0:U",
                                        NULL };
static void *dbcheck_invobj_tpl    = NULL;

        char *curline;
        char *eoln;
	unsigned long yellow=0,red=0,green=0;
        dbgprintf("dbcheck: host %s test %s\n",hostname, testname);

        if (strstr(msg, "dbcheck.pl")) {
                if (dbcheck_invobj_tpl == NULL) dbcheck_invobj_tpl = setup_template(dbcheck_invobj_params);
                curline=strstr(msg, "Invalid Object Checking");
                if (curline) {
                        eoln = strchr(curline, '\n');
                        curline = (eoln ? (eoln+1) : NULL);
                }
                while (curline) {
			if ( *curline == '\n') { curline++; continue; }
                        if ((eoln = strchr(curline, '\n')) == NULL) break;
                        *eoln = '\0';
			if ( *curline =='&' ) curline++;
			if ( strstr(curline,"red") == curline) red++;
			if ( strstr(curline,"yellow") == curline) yellow++;
			if ( strstr(curline,"green") == curline) green++;
nextline:
                        if (eoln) *(eoln)='\n';
                        curline = (eoln ? (eoln+1) : NULL);
                }
                setupfn("%s.rrd",testname);
                dbgprintf("dbcheck: host %s test %s  red %ld yellow %ld green %ld\n", 
			hostname, testname, red,yellow,green);
                snprintf(rrdvalues, sizeof(rrdvalues), "%d:%ld:%ld:%ld", (int) tstamp, red,yellow,green);
                        create_and_update_rrd(hostname, testname, classname, pagepaths, dbcheck_invobj_params, dbcheck_invobj_tpl);
        }
        return 0;
}

int do_dbcheck_tablespace_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
       static char *tablespace_params[] = { "DS:pct:GAUGE:600:0:U", "DS:used:GAUGE:600:0:U", NULL };
       static rrdtpldata_t *tablespace_tpl      = NULL;

       char *eoln, *curline;
       static int ptnsetup = 0;
       static pcre *inclpattern = NULL;
       static pcre *exclpattern = NULL;

       if (tablespace_tpl == NULL) tablespace_tpl = setup_template(tablespace_params);

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
       /* FD: For dbcheck.pl move after the Header */
       curline=strstr(curline, "TableSpace/DBSpace");
       if (curline) {
               eoln = strchr(curline, '\n');
               curline = (eoln ? (eoln+1) : NULL);
       }

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

               /* FD: Exit if doing DBCHECK and the end of the tablespaces are reached */
               if (strstr(eoln+1, "dbcheck.pl") == (eoln+1)) break;

               /* red/yellow filesystems show up twice */
               if (*curline == '&') goto nextline;
               if ((strstr(curline, " red ") || strstr(curline, " yellow "))) goto nextline;

               for (columncount=0; (columncount<20); columncount++) columns[columncount] = "";
               fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, " ");
               while (p && (columncount < 20)) { columns[columncount++] = p; p = strtok(NULL, " "); }

               /* FD: Check TableSpace from dbcheck.pl */
               /* FD: Add an initial "/" to TblSpace Name so they're reported in the trends column */
               diskname=xmalloc(strlen(columns[0])+2);
               sprintf(diskname,"/%s",columns[0]);
               p = strchr(columns[4], '%'); if (p) *p = ' ';
               pused = atoi(columns[4]);
               p = columns[2] + strspn(columns[2], "0123456789.");
               /* FD: Using double instead of long long because we can have decimal */
               dused = str2ll(columns[2], NULL);
               /* FD: dbspace report contains M/G/T
                  Convert to KB if there's a modifier after the numbers
               */
               if (*p == 'M') dused *= 1024;
               else if (*p == 'G') dused *= (1024*1024);
               else if (*p == 'T') dused *= (1024*1024*1024);
               aused=(long long)dused;


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
                       setupfn2("%s%s.rrd", testname,diskname);
                       snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d:%lld", (int)tstamp, pused, aused);
                       create_and_update_rrd(hostname, testname, classname, pagepaths, tablespace_params, tablespace_tpl);
               }
               if (diskname) { xfree(diskname); diskname = NULL; }

               if (eoln) *eoln = '\n';
               xfree(fsline);

nextline:
               curline = (eoln ? (eoln+1) : NULL);
       }

       return 0;
}


