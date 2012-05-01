/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <glob.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include "libxymon.h"

#include "tcptalk.h"
#include "sendresults.h"
#include "pingtalk.h"

int timeout = 0;

/* pendingtests and donetests are a list of myconn_t records which holds the data for each test. */
static listhead_t *pendingtests = NULL;
static listhead_t *donetests = NULL;

/*
 * iptree is a cross-reference index that maps IP-adresses to record
 * in the "pendingtests" list.
 * It doesn't have any records on its own - it is just a tree indexed
 * by the myconn_t pendingtests records' IP-address.
 * We need it because fping returns only the IP's, not the hostnames that
 * we must use when reporting back the results.
 */
static void *iptree = NULL;

/* "activeprocesses" is a list of the processes currently running */
typedef struct activeprocessrec_t {
	pid_t pid;
	char *basefn;
	int subid;
	void *datap;
	time_t timeout;
	int killcount;
	struct timespec starttime;
} activeprocessrec_t;
static listhead_t *activeprocesses = NULL;


int running = 1;
time_t nextconfigreload = 0;


/* ping{4,6}_data is a list of the IP's we will submit to the next fping process */
static strbuffer_t *ping4_data = NULL;
static strbuffer_t *ping6_data = NULL;


void sig_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
		running = 0;
		break;

	  case SIGHUP:
		nextconfigreload = 0;
		break;
	}

}


static void feed_queue(int talkproto, char *ip)
{
	/* Add an IP to the appropriate list of IP's fed to the next worker process */
	char l[100];

	switch (talkproto) {
	  case TALK_PROTO_PING:
		switch (conn_is_ip(ip)) {
		  case 4:
			if (!ping4_data) ping4_data = newstrbuffer(0);
			sprintf(l, "%s\n", ip);
			addtobuffer(ping4_data, l);
			break;
		  case 6:
			if (!ping6_data) ping6_data = newstrbuffer(0);
			sprintf(l, "%s\n", ip);
			addtobuffer(ping6_data, l);
			break;
		  default:
			break;
		}
		break;

	  case TALK_PROTO_RPC:
	  case TALK_PROTO_LDAP:
		break;

	  default:
		break;
	}
}


static void launch_worker(strbuffer_t *workerdata, int talkproto, int subid, char *basefn, void *datap, char *opt1, char *opt2)
{
	/* 
	 * Fork a new worker process.
	 *
	 * For ping workers: 
	 * - Feed it the IP's listed in "workerdata".
	 * - "subid" indicates if this is for IPv4 or IPv6 tests.
	 * For rpc workers:
	 * - pass IP of host to test on commandline.
	 */
	int pfd[2];
	pid_t workerpid;

	if (pipe(pfd) == -1) {
		errprintf("Cannot create pipe to worker process: %s\n", strerror(errno));
		return;
	}

	workerpid = fork();
	if (workerpid < 0) {
		errprintf("Cannot fork worker process: %s\n", strerror(errno));
		return;
	}

	else if (workerpid == 0) {
		/* Child process */
		char *workeroutfn, *workererrfn, *cmd;
		int outfile, errfile;
		char *cmdargs[6];
		char timeoutstr[10];

		close(pfd[1]); /* Close write end of pipe */

		cmd = NULL;
		memset(cmdargs, 0, sizeof(cmdargs));
		sprintf(timeoutstr, "%d", timeout);

		dbgprintf("Running worker process in pid %d\n", (int)getpid());

		workeroutfn = (char *)malloc(strlen(basefn) + 16);
		sprintf(workeroutfn, "%s.%010d.out", basefn, (int)getpid());
		workererrfn = (char *)malloc(strlen(basefn) + 16);
		sprintf(workererrfn, "%s.%010d.err", basefn, (int)getpid());

		outfile = open(workeroutfn, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
		if (outfile == -1) errprintf("Cannot create file %s : %s\n", workeroutfn, strerror(errno));
		errfile = open(workererrfn, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
		if (errfile == -1) errprintf("Cannot create file %s : %s\n", workererrfn, strerror(errno));

		if ((outfile == -1) || (errfile == -1)) {
			/* Ouch - cannot create our output files. Abort. */
			errprintf("Cannot create output/error files\n");
			exit(EXIT_FAILURE);
		}

		/* Assign stdin to the pipe */
		close(STDIN_FILENO);
		if (dup2(pfd[0], STDIN_FILENO) != 0) dbgprintf("Cannot dup2 stdin: %s\n", strerror(errno));
		close(pfd[0]); /* No longer needed */

		/* Assign stdout to the output logfile */
		close(STDOUT_FILENO);
		if (dup2(outfile, STDOUT_FILENO) != 0) dbgprintf("Cannot dup2 stdout: %s\n", strerror(errno));

		/* Assign stderr to the error logfile */
		close(STDERR_FILENO);
		if (dup2(errfile, STDERR_FILENO) != 0) dbgprintf("Cannot dup2 stderr: %s\n", strerror(errno));

		switch (talkproto) {
		  case TALK_PROTO_PING:
			switch (subid) {
			  case 4: cmd = xgetenv("FPING4"); break;
			  case 6: cmd = xgetenv("FPING6"); break;
			  default: break;
			}

			/* Setup command line args. Should probably be configurable - at least the count */
			cmdargs[1] = "-C3";
			cmdargs[2] = "-q";
			break;

		  case TALK_PROTO_RPC:
			cmd = xgetenv("RPCINFO");
			cmdargs[1] = "-p";
			cmdargs[2] = STRBUF(workerdata);
			break;

		  case TALK_PROTO_LDAP:
			cmd = "ldaptalk";
			cmdargs[1] = "--timeout";
			cmdargs[2] = timeoutstr;
			cmdargs[3] = STRBUF(workerdata);
			cmdargs[4] = opt1;
			cmdargs[5] = opt2;
			break;

		  default:
			break;
		}

		if (cmd) {
			cmdargs[0] = cmd;
			execvp(cmd, cmdargs);
		}

		/* Should never go here ... just kill the child */
		errprintf("Command '%s' failed: %s\n", cmd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	else if (workerpid > 0) {
		activeprocessrec_t *actrec;
		char *outp;
		int wres, outremain;

		/* Parent - feed IP's to the child, and add the child PID to our list of running worker processes. */

		close(pfd[0]); /* Close read end of pipe */

		switch (talkproto) {
		  case TALK_PROTO_PING:
			outp = STRBUF(workerdata); outremain = STRBUFLEN(workerdata);
			do {
				wres = write(pfd[1], outp, outremain);
				if (wres < 0) {
					errprintf("Failed to feed all IP's to child ping process: %s\n", strerror(errno));
					outremain = 0;
				}
				else {
					outremain -= wres;
					outp += wres;
				}
			} while (outremain > 0);
			break;

		  case TALK_PROTO_RPC:
		  case TALK_PROTO_LDAP:
			break;

		  default:
			break;
		}
		close(pfd[1]);
		clearstrbuffer(workerdata);

		actrec = (activeprocessrec_t *)calloc(1, sizeof(activeprocessrec_t));
		actrec->pid = workerpid;
		actrec->basefn = strdup(basefn);
		actrec->subid = subid;
		actrec->datap = datap;
		actrec->timeout = gettimer() + timeout;
		actrec->killcount = 0;
		getntimer(&actrec->starttime);
		list_item_create(activeprocesses, actrec, "");
	}
}


static int scan_queue(char *id, int talkproto)
{
	/*
	 * Scan the XYMONTMP directory for "batch" files, and pick up the IP's we
	 * need to test now.
	 * batch files are named "<ID>batch.TIMESTAMP.SEQUENCE"
	 */
	int scanres;
	char filepath[PATH_MAX];
	glob_t globdata;
	int i;
	strbuffer_t *queue_data;
	int tstampofs;
	time_t now = getcurrenttime(NULL);

	tstampofs = strlen(xgetenv("XYMONTMP")) + strlen(id) + strlen("/batch.");

	sprintf(filepath, "%s/%sbatch.??????????.?????", xgetenv("XYMONTMP"), id);
	scanres = glob(filepath, 0, NULL, &globdata);
	if (scanres == GLOB_NOMATCH) return 0;

	if (scanres != 0) {
		errprintf("Scanning for files %s failed, glob error\n", filepath);
		return 0;
	}

	queue_data = newstrbuffer(0);

	for (i = 0; (i < globdata.gl_pathc); i++) {
		FILE *batchfd;
		char batchl[100];
		time_t tstamp;

		tstamp = (time_t)atoi(globdata.gl_pathv[i] + tstampofs);
		if ((now - tstamp) > 300) {
			errprintf("Ignoring batch file %s - time now is %d, so it is stale\n", globdata.gl_pathv[i], (int)now);
			remove(globdata.gl_pathv[i]);
			continue;
		}

		batchfd = fopen(globdata.gl_pathv[i], "r");
		if (batchfd == NULL) {
			errprintf("Cannot open file %s\n", globdata.gl_pathv[i]);
			continue;
		}

		/* Unlink the file so we wont process it again */
		remove(globdata.gl_pathv[i]);

		while (fgets(batchl, sizeof(batchl), batchfd)) {
			char *hname, *ip, *testspec, *opt1, *opt2;
			void *hinfo;

			hname = strtok(batchl, "\t\r\n");
			ip = (hname ? strtok(NULL, "\t\r\n") : NULL);
			testspec = (ip ? strtok(NULL, "\t\r\n") : NULL);
			opt1 = (testspec ? strtok(NULL, "\t\r\n") : NULL);
			opt2 = (opt1 ? strtok(NULL, "\t\r\n") : NULL);
			hinfo = (hname ? hostinfo(hname) : NULL);

			if (hinfo && ip && conn_is_ip(ip) && testspec) {
				xtreePos_t handle;

				/* Add the test only if we haven't got it already */
				handle = xtreeFind(iptree, ip);
				if (handle == xtreeEnd(iptree)) {
					/*
					 * Lots of list / queue manipulation here.
					 * 1) Create a "myconn_t" record for the test - this will be used to
					 *    collect test data and eventually submitted to send_test_results() for
					 *    reporting the test results back to xymond.
					 * 2) Add the myconn_t record to the "pendingtests" list of active tests.
					 * 3) Create / update a record in the "iptree" tree, so we can map the IP
					 *    reported by fping back to the test record.
					 */
					myconn_t *testrec;

					testrec = (myconn_t *)calloc(1, sizeof(myconn_t));
					testrec->testspec = strdup(id);
					testrec->talkprotocol = talkproto;
					testrec->hostinfo = hinfo;
					testrec->listitem = list_item_create(pendingtests, testrec, testrec->testspec);

					testrec->netparams.destinationip = strdup(ip);
					xtreeAdd(iptree, testrec->netparams.destinationip, testrec);

					switch (talkproto) {
					  case TALK_PROTO_PING:
						feed_queue(talkproto, ip);
						break;
					  case TALK_PROTO_RPC:
						/* We do one ping process per test */
						clearstrbuffer(queue_data);
						addtobuffer(queue_data, ip);
						launch_worker(queue_data, TALK_PROTO_RPC, 0, globdata.gl_pathv[i], testrec, opt1, opt2);
						break;
					  case TALK_PROTO_LDAP:
						/* We do one ldaptalk process per test */
						clearstrbuffer(queue_data);
						addtobuffer(queue_data, testspec);
						launch_worker(queue_data, TALK_PROTO_LDAP, 0, globdata.gl_pathv[i], testrec, opt1, opt2);
						break;
					  default:
						break;
					}
				}
			}
		}
		fclose(batchfd);

		switch (talkproto) {
		  case TALK_PROTO_PING:
			/* We do one ping process per IP protocol for the entire batch */
			if (ping4_data && STRBUFLEN(ping4_data)) 
				launch_worker(ping4_data, TALK_PROTO_PING, 4, globdata.gl_pathv[i], NULL, NULL, NULL);
			if (ping6_data && STRBUFLEN(ping6_data))
				launch_worker(ping6_data, TALK_PROTO_PING, 6, globdata.gl_pathv[i], NULL, NULL, NULL);
			break;

		  case TALK_PROTO_RPC:
		  case TALK_PROTO_LDAP:
			/* Have already forked the children */
			break;

		  default:
			break;
		}
	}

	globfree(&globdata);
	freestrbuffer(queue_data);

	return 1;
}

static int collect_ping_results(void)
{
	pid_t pid;
	int status;
	listitem_t *actwalk;
	activeprocessrec_t *actrec;
	int found = 0;

	/* Wait for one of the childs to finish */
	pid = waitpid(-1, &status, WNOHANG);
	if (pid == -1) {
		errprintf("waitpid failed: %s\n", strerror(errno));
		return 0;
	}
	else if (pid == 0) {
		return 0;
	}

	dbgprintf("waitpid returned pid %d\n", (int)pid);

	/* Find the data about the process that finished */
	actwalk = (listitem_t *)activeprocesses->head;
	do {
		actrec = (activeprocessrec_t *)actwalk->data;
		found = (actrec->pid == pid);
		if (!found) actwalk = actwalk->next;
	} while (actwalk && !found);
	if (found) list_item_delete(actwalk, "");

	if (WIFEXITED(status)) {
		char fn[PATH_MAX];
		FILE *fd;
		char l[1024];
		xtreePos_t handle;

		dbgprintf("Process %d terminated with status %d, testing IPv%d adresses\n", (int)pid, WEXITSTATUS(status), actrec->subid);
		/*
		 * fping - when run with the -q option - sends all data to stderr (!)
		 */
		sprintf(fn, "%s.%010d.err", actrec->basefn, (int)pid);
		fd = fopen(fn, "r");
		if (!fd) {
			errprintf("Cannot open file %s\n", fn);
			goto collectioncleanup;
		}

		while (fgets(l, sizeof(l), fd)) {
			char *ip, *delim, *results;

			dbgprintf("%s", l);

			ip = strtok(l, " \t");
			delim = (ip ? strtok(NULL, " \t") : NULL);
			results = (delim ? strtok(NULL, "\r\n") : NULL);

			if (ip && results) {
				handle = xtreeFind(iptree, ip);
				if (handle != xtreeEnd(iptree)) {
					myconn_t *testrec = (myconn_t *)xtreeData(iptree, handle);
					int testcount = 0;
					double pingtime = 0.0;
					char *tok;

					if (!testrec->textlog) testrec->textlog = newstrbuffer(0);
					addtobuffer(testrec->textlog, results);

					tok = strtok(results, " \t");
					while (tok) {
						if (strcmp(tok, "-") != 0) {
							testcount++;
							pingtime += atof(tok);
						}
						tok = strtok(NULL, " \t");
					}

					if (testcount > 0) {
						testrec->elapsedus = ((1000.0 * pingtime) / testcount);
						testrec->talkresult = TALK_OK;
					}
					else {
						testrec->talkresult = TALK_CONN_FAILED;
					}

					list_item_move(donetests, testrec->listitem, ip);
				}
			}
		}

collectioncleanup:
		if (fd) fclose(fd);

		sprintf(fn, "%s.%010d.out", actrec->basefn, (int)pid);
		remove(fn);
		sprintf(fn, "%s.%010d.err", actrec->basefn, (int)pid);
		remove(fn);

		xfree(actrec->basefn);
		xfree(actrec);
	}

	return 1;
}

static int collect_generic_results(char *toolid)
{
	pid_t pid;
	int status;
	listitem_t *actwalk;
	activeprocessrec_t *actrec;
	myconn_t *testrec = NULL;
	int found = 0;
	char fn[PATH_MAX];
	FILE *fd = NULL;
	char l[1024];

	/* Wait for one of the childs to finish */
	pid = waitpid(-1, &status, WNOHANG);
	if (pid == -1) {
		errprintf("waitpid failed: %s\n", strerror(errno));
		return 0;
	}
	else if (pid == 0) {
		return 0;
	}

	dbgprintf("waitpid returned pid %d\n", (int)pid);

	/* Find the data about the process that finished */
	actwalk = (listitem_t *)activeprocesses->head;
	do {
		actrec = (activeprocessrec_t *)actwalk->data;
		found = (actrec->pid == pid);
		if (!found) actwalk = actwalk->next;
	} while (actwalk && !found);

	if (found) {
		list_item_delete(actwalk, "");
		testrec = (myconn_t *)actrec->datap;
	}

	if (!testrec) goto collectioncleanup;

	if (!testrec->textlog) testrec->textlog = newstrbuffer(0);

	if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
		struct timespec now;

		testrec->talkresult = TALK_OK;
		getntimer(&now);
		testrec->elapsedus = ntimerus(&actrec->starttime, &now);

		sprintf(fn, "%s.%010d.out", actrec->basefn, (int)pid);
		fd = fopen(fn, "r");
		if (!fd) {
			errprintf("Cannot open file %s\n", fn);
		}
		else {
			while (fgets(l, sizeof(l), fd)) addtobuffer(testrec->textlog, l);
			fclose(fd);
		}
	}
	else {
		testrec->talkresult = TALK_CONN_FAILED;

		*l = '\0';
		if (WIFEXITED(status)) sprintf(l, "%s process terminated with error status %d\n", toolid, WEXITSTATUS(status));
		if (WIFSIGNALED(status)) sprintf(l, "%s process terminated by signal %d\n", toolid, WTERMSIG(status));

		if (*l != '\0') {
			errprintf("%s", l);
			addtobuffer(testrec->textlog, l);
		}

		sprintf(fn, "%s.%010d.err", actrec->basefn, (int)pid);
		fd = fopen(fn, "r");
		if (!fd) {
			errprintf("Cannot open file %s\n", fn);
		}
		else {
			while (fgets(l, sizeof(l), fd)) addtobuffer(testrec->textlog, l);
			fclose(fd);
		}
	}

	list_item_move(donetests, testrec->listitem, "");

collectioncleanup:
	sprintf(fn, "%s.%010d.out", actrec->basefn, (int)pid);
	remove(fn);
	sprintf(fn, "%s.%010d.err", actrec->basefn, (int)pid);
	remove(fn);

	if (actrec) {
		xfree(actrec->basefn);
		xfree(actrec);
	}

	return 1;
}

static int collect_rpc_results(void)
{
	return collect_generic_results("rpcinfo");
}

static int collect_ldap_results(void)
{
	return collect_generic_results("ldaptalk");
}


static void kill_stalled_tasks(void)
{
	listitem_t *actwalk;
	time_t now = gettimer();

	actwalk = (listitem_t *)activeprocesses->head;
	while (actwalk) {
		activeprocessrec_t *actrec = (activeprocessrec_t *)actwalk->data;
		if (actrec->timeout < now) {
			/* Too old - kill it. On the next loop, collect_*_results() will pick it up. */
			kill(actrec->pid, (actrec->killcount == 0) ? SIGTERM : SIGKILL);
			actrec->killcount++;
		}

		actwalk = actwalk->next;
	}
}


static void cleanup_donetests(void)
{
	listitem_t *walk, *nextlistitem;
	myconn_t *testrec;

	walk = donetests->head;
	while (walk) {
		nextlistitem = walk->next;
		testrec = (myconn_t *)walk->data;

		if (testrec->netparams.destinationip) {
			xtreeDelete(iptree, testrec->netparams.destinationip);
			xfree(testrec->netparams.destinationip);
		}
		if (testrec->netparams.sourceip) xfree(testrec->netparams.sourceip);
		if (testrec->testspec) xfree(testrec->testspec);
		if (testrec->textlog) freestrbuffer(testrec->textlog);
		xfree(testrec);

		list_item_delete(walk, "");
		walk = nextlistitem;
	}
}

int main(int argc, char **argv)
{
	int argi;
	struct sigaction sa;
	char *queueid = NULL;
	int mytalkprotocol = TALK_PROTO_PING;

	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			timeout = atoi(p+1);
		}
		else if (strcmp(argv[argi], "ping") == 0) {
			queueid = argv[argi];
			mytalkprotocol = TALK_PROTO_PING;
			if (!timeout) timeout = 200;
		}
		else if (strcmp(argv[argi], "rpc") == 0) {
			queueid = argv[argi];
			mytalkprotocol = TALK_PROTO_RPC;
			if (!timeout) timeout = 30;
		}
		else if (strcmp(argv[argi], "ldap") == 0) {
			queueid = argv[argi];
			mytalkprotocol = TALK_PROTO_LDAP;
			if (!timeout) timeout = 30;
		}
	}

	if (!queueid) {
		errprintf("Unknown queue, aborting\n");
		return 1;
	}

	errprintf("Setting up signal handlers\n");
	setup_signalhandler(programname);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	iptree = xtreeNew(strcmp);
	activeprocesses = list_create("activeprocesses");
	pendingtests = list_create("pending");
	donetests = list_create("done");

	while (running) {
		time_t now = gettimer();
		int anyaction = 0;

		dbgprintf("Main loop\n");

		if (now > nextconfigreload) {
			nextconfigreload = now + 600;
			load_hostnames("@", NULL, get_fqdn());
		}

		if (activeprocesses->len > 0) {
			dbgprintf("Collecting results\n");
			switch (mytalkprotocol) {
			  case TALK_PROTO_PING:
				while (collect_ping_results()) anyaction++;
				break;
			  case TALK_PROTO_RPC:
				while (collect_rpc_results()) anyaction++;
				break;
			  case TALK_PROTO_LDAP:
				while (collect_ldap_results()) anyaction++;
				break;
			  default:
				break;
			}

			/* Nuke any tasks that have stalled */
			kill_stalled_tasks();
		}

		if ((pendingtests->len == 0) && (donetests->len > 0)) {
			dbgprintf("Sending results\n");
			send_test_results(donetests, programname, 1);
			cleanup_donetests();
		}

		anyaction += scan_queue(queueid, mytalkprotocol);

		if (anyaction == 0) sleep(1);
	}

	return 0;
}

