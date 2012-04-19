/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

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


/* pendingtests and donetests are a list of myconn_t records which holds the data for each ping test. */
static listhead_t *pendingtests = NULL;
static listhead_t *donetests = NULL;

/*
 * iptree is a cross-reference index that maps IP-adresses to record
 * in the "pendingtests" list.
 * It doesn't have any records on its own - it is just a tree indexed
 * by the myconn_t pendingtests records' IP-address.
 * We need it because fping returns only the IP's, not the hostnames that
 * we must use when reporting back the ping results.
 */
static void *iptree = NULL;

/* "activepings" is a list of the ping processes currently running */
typedef struct activepingrec_t {
	pid_t pid;
	char *basefn;
	int subid;
} activepingrec_t;
static listhead_t *activepings = NULL;


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

static void feed_ping(char *ip)
{
	/* Add an IP to the appropriate list of IP's fed to the next fping process */
	char l[100];

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
}


static void launch_ping(strbuffer_t *pingdata, int subid, char *basefn)
{
	/* 
	 * Fork a new fping process and feed it the IP's listed in "pingdata".
	 * "subid" indicates if this is for IPv4 or IPv6 tests.
	 */
	int pfd[2];
	pid_t pingpid;

	if (!pingdata || (STRBUFLEN(pingdata) == 0)) return;

	if (pipe(pfd) == -1) {
		errprintf("Cannot create ping to fping: %s\n", strerror(errno));
		return;
	}

	pingpid = fork();
	if (pingpid < 0) {
		errprintf("Cannot fork fping: %s\n", strerror(errno));
		return;
	}

	else if (pingpid == 0) {
		/* Child process */
		char *pingoutfn, *pingerrfn, *cmd;
		int outfile, errfile;
		char *cmdargs[4];

		dbgprintf("Running fping in pid %d\n", (int)getpid());

		pingoutfn = (char *)malloc(strlen(basefn) + 16);
		sprintf(pingoutfn, "%s.%010d.out", basefn, (int)getpid());
		pingerrfn = (char *)malloc(strlen(basefn) + 16);
		sprintf(pingerrfn, "%s.%010d.err", basefn, (int)getpid());

		outfile = open(pingoutfn, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
		if (outfile == -1) errprintf("Cannot create file %s : %s\n", pingoutfn, strerror(errno));
		errfile = open(pingerrfn, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
		if (errfile == -1) errprintf("Cannot create file %s : %s\n", pingerrfn, strerror(errno));

		if ((outfile == -1) || (errfile == -1)) {
			/* Ouch - cannot create our output files. Abort. */
			errprintf("Cannot create output/error files\n");
			return;
		}

		if (dup2(pfd[0], STDIN_FILENO) != 0) errprintf("Cannot dup2 stdin: %s\n", strerror(errno));
		if (dup2(outfile, STDOUT_FILENO) != 0) errprintf("Cannot dup2 stdout: %s\n", strerror(errno));
		if (dup2(errfile, STDERR_FILENO) != 0) errprintf("Cannot dup2 stderr: %s\n", strerror(errno));
		close(pfd[0]); close(pfd[1]); close(outfile); close(errfile);

		switch (subid) {
		  case 4: cmd = xgetenv("FPING4"); break;
		  case 6: cmd = xgetenv("FPING6"); break;
		  default: cmd = NULL; break;
		}

		if (cmd) {
			/* Setup command line args. Should probably be configurable - at least the count */
			cmdargs[0] = cmd;
			cmdargs[1] = "-C3";
			cmdargs[2] = "-q";
			cmdargs[3] = NULL;
			execvp(cmd, cmdargs);
		}

		/* Should never go here ... just kill the child */
		errprintf("Command '%s' failed: %s\n", cmd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	else if (pingpid > 0) {
		activepingrec_t *actrec;

		/* Parent - feed IP's to the child, and add the child PID to our list of running fping processes. */
		close(pfd[0]);
		write(pfd[1], STRBUF(pingdata), STRBUFLEN(pingdata));
		close(pfd[1]);
		clearstrbuffer(pingdata);

		actrec = (activepingrec_t *)calloc(1, sizeof(activepingrec_t));
		actrec->pid = pingpid;
		actrec->basefn = strdup(basefn);
		actrec->subid = subid;
		list_item_create(activepings, actrec, "");
	}
}


static int run_ping_queue(void)
{
	/*
	 * Scan the XYMONTMP directory for "pingbatch" files, and pick up the IP's we
	 * need to test now.
	 * pingbatch files are named "pingbatch.TIMESTAMP.SEQUENCE"
	 */
	int scanres;
	char filepath[PATH_MAX];
	glob_t globdata;
	int i;
	int tstampofs;
	time_t now = getcurrenttime(NULL);

	tstampofs = strlen(xgetenv("XYMONTMP")) + strlen("/pingbatch.");

	sprintf(filepath, "%s/pingbatch.??????????.?????", xgetenv("XYMONTMP"));
	scanres = glob(filepath, 0, NULL, &globdata);
	if (scanres == GLOB_NOMATCH) return 0;

	if (scanres != 0) {
		errprintf("Scanning for files %s failed, glob error\n", filepath);
		return 0;
	}

	for (i = 0; (i < globdata.gl_pathc); i++) {
		FILE *batchfd;
		char batchl[100];
		time_t tstamp;

		tstamp = (time_t)atoi(globdata.gl_pathv[i] + tstampofs);
		if ((now - tstamp) > 300) {
			errprintf("Dropping batch file %s - time now is %d, so it is stale\n", globdata.gl_pathv[i], (int)now);
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
			char *hname, *ip;
			void *hinfo;

			hname = strtok(batchl, "\t");
			ip = (hname ? strtok(NULL, "\t\r\n") : NULL);
			hinfo = (hname ? hostinfo(hname) : NULL);

			if (hinfo && ip && conn_is_ip(ip)) {
				xtreePos_t handle;

				/* Add the test only if we haven't got it already */
				handle = xtreeFind(iptree, ip);
				if (handle == xtreeEnd(iptree)) {
					/*
					 * Lots of list / queue manipulation here.
					 * 1) Create a "myconn_t" record for the test - this will be used to
					 *    collect test data and eventually submitted to send_test_results() for
					 *    reporting the test results back to xymond.
					 * 2) Add the myconn_t record to the "pingtests" list of active tests.
					 * 3) Create / update a record in the "iptree" tree, so we can map the IP
					 *    reported by fping back to the test record.
					 */
					myconn_t *testrec;

					testrec = (myconn_t *)calloc(1, sizeof(myconn_t));
					testrec->testspec = strdup("ping");
					testrec->talkprotocol = TALK_PROTO_PING;
					testrec->hostinfo = hinfo;
					testrec->listitem = list_item_create(pendingtests, testrec, testrec->testspec);

					testrec->netparams.destinationip = strdup(ip);
					xtreeAdd(iptree, testrec->netparams.destinationip, testrec);

					feed_ping(ip);
				}
			}
		}
		fclose(batchfd);
		launch_ping(ping4_data, 4, globdata.gl_pathv[i]);
		launch_ping(ping6_data, 6, globdata.gl_pathv[i]);
	}

	globfree(&globdata);

	return 1;
}

static int collect_results(void)
{
	pid_t pid;
	int status;
	listitem_t *actwalk;
	activepingrec_t *actrec;
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
	actwalk = (listitem_t *)activepings->head;
	do {
		actrec = (activepingrec_t *)actwalk->data;
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
		if (!fd) errprintf("Cannot open file %s\n", fn);
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
		fclose(fd);

		sprintf(fn, "%s.%010d.out", actrec->basefn, (int)pid);
		remove(fn);
		sprintf(fn, "%s.%010d.err", actrec->basefn, (int)pid);
		remove(fn);

		xfree(actrec->basefn);
		xfree(actrec);
	}

	return 1;
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

	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
	}

	errprintf("Setting up signal handlers\n");
	setup_signalhandler("pingqueue");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	iptree = xtreeNew(strcmp);
	activepings = list_create("activepings");
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

		if (activepings->len > 0) {
			dbgprintf("Collecting results\n");
			anyaction = collect_results();
		}

		if ((pendingtests->len == 0) && (donetests->len > 0)) {
			dbgprintf("Sending results\n");
			send_test_results(donetests, programname, 1);
			cleanup_donetests();
		}

		anyaction += run_ping_queue();

		if (anyaction == 0) sleep(1);
	}

	return 0;
}

