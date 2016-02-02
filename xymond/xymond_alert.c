/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This is the main alert module for xymond. It receives alert messages,      */
/* keeps track of active alerts, enable/disable, acks etc., and triggers      */
/* outgoing alerts by calling send_alert().                                   */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
 * Information from the Xymon docs about "page" modules:
 *
 *   page
 *   ----
 *   @@page|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime|location
 *   <message>
 *   @@
 *
 *   @@ack|timestamp|sender|hostname|testname|expiretime
 *   <ack message>
 *   @@
 *
 *   @@notify|timestamp|sender|hostname|testname|pagepath
 *   <notify message>
 *   @@
 *
 *   Note that "page" modules get messages whenever the alert-state of a test
 *   changes. I.e. a message is generated whenever a test goes from a color
 *   that is non-alerting to a color that is alerting, or vice versa.
 *
 *   How does the pager know when a test is disabled ? It will get a "page"
 *   message with color=blue, if the old color of the test was in an alert
 *   state. (If it wasn't, the pager module does not need to know that the
 *   test has been disabled). It should then clear any stored info about
 *   active alerts for this host.test combination.
 */

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>

#include "libxymon.h"

#include "xymond_worker.h"
#include "do_alert.h"



#define DEFAULT_RELOAD_INTERVAL 300
int reloadinterval = DEFAULT_RELOAD_INTERVAL;  /* Seconds - how often to check hosts.cfg for changes (which can be expensive) */

int loadhostsfromxymond = 0;
static int reloadconfig = 0;
static int running = 1;
static time_t nextcheckpoint = 0;
static int termsig = -1;

void * hostnames;
void * testnames;
void * locations;

typedef struct alertanchor_t {
	activealerts_t *head;
} alertanchor_t;

activealerts_t *ahead = NULL;

char *statename[] = {
	/* A_PAGING, A_NORECIP, A_ACKED, A_RECOVERED, A_DISABLED, A_NOTIFY, A_DEAD */
	"paging", "norecip", "acked", "recovered", "disabled", "notify", "dead"
};

char *find_name(void * tree, char *name)
{
	char *result;
	xtreePos_t handle;

	handle = xtreeFind(tree, name);
	if (handle == xtreeEnd(tree)) {
		result = strdup(name);
		if (tree == hostnames) {
			alertanchor_t *anchor = malloc(sizeof(alertanchor_t));
			anchor->head = NULL;
			xtreeAdd(tree, result, anchor);
		}
		else {
			xtreeAdd(tree, result, result);
		}
	}
	else {
		result = (char *)xtreeKey(tree, handle);
	}
	
	return result;
}

void add_active(char *hostname, activealerts_t *rec)
{
	xtreePos_t handle;
	alertanchor_t *anchor;

	handle = xtreeFind(hostnames, hostname);
	if (handle == xtreeEnd(hostnames)) return;
	anchor = (alertanchor_t *)xtreeData(hostnames, handle);
	rec->next = anchor->head;
	anchor->head = rec;
}

void clean_active(alertanchor_t *anchor)
{
	activealerts_t *newhead = NULL, *tmp, *curr;

	curr = anchor->head;
	while (curr) {
		tmp = curr;
		curr = curr->next;

		if (tmp->state == A_DEAD) {
			if (tmp->osname) xfree(tmp->osname);
			if (tmp->classname) xfree(tmp->classname);
			if (tmp->groups) xfree(tmp->groups);
			if (tmp->pagemessage) xfree(tmp->pagemessage);
			if (tmp->ackmessage) xfree(tmp->ackmessage);
			xfree(tmp);
		}
		else {
			tmp->next = newhead;
			newhead = tmp;
		}
	}

	anchor->head = newhead;
}

void clean_all_active(void)
{
	xtreePos_t handle;

	for (handle = xtreeFirst(hostnames); handle != xtreeEnd(hostnames); handle = xtreeNext(hostnames, handle)) {
		alertanchor_t *anchor = (alertanchor_t *)xtreeData(hostnames, handle);
		clean_active(anchor);
	}
}

activealerts_t *find_active(char *hostname, char *testname)
{
	xtreePos_t handle;
	alertanchor_t *anchor;
	char *twalk;
	activealerts_t *awalk;

	handle = xtreeFind(hostnames, hostname);
	if (handle == xtreeEnd(hostnames)) return NULL;
	anchor = (alertanchor_t *)xtreeData(hostnames, handle);

	handle = xtreeFind(testnames, testname);
	if (handle == xtreeEnd(testnames)) return NULL;
	twalk = (char *)xtreeData(testnames, handle);

	for (awalk = anchor->head; (awalk && (awalk->testname != twalk)); awalk=awalk->next) ;

	return awalk;
}

static xtreePos_t alisthandle;
static activealerts_t *alistwalk;
activealerts_t *alistBegin(void)
{
	alisthandle = xtreeFirst(hostnames);
	alistwalk = NULL;

	while ((alisthandle != xtreeEnd(hostnames)) && (alistwalk == NULL)) {
		alertanchor_t *anchor = (alertanchor_t *)xtreeData(hostnames, alisthandle);
		alistwalk = anchor->head;
		if (alistwalk == NULL) alisthandle = xtreeNext(hostnames, alisthandle);
	}

	return alistwalk;
}

activealerts_t *alistNext(void)
{
	if (!alistwalk) return NULL;

	alistwalk = alistwalk->next;
	if (alistwalk) return alistwalk;

	alisthandle = xtreeNext(hostnames, alisthandle);
	alistwalk = NULL;

	while ((alisthandle != xtreeEnd(hostnames)) && (alistwalk == NULL)) {
		alertanchor_t *anchor = (alertanchor_t *)xtreeData(hostnames, alisthandle);
		alistwalk = anchor->head;
		if (alistwalk == NULL) alisthandle = xtreeNext(hostnames, alisthandle);
	}

	return alistwalk;
}

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGCHLD:
		  /* Pickup any finished child processes to avoid zombies */
		  while (waitpid(-1, NULL, WNOHANG) > 0) ;
		  break;

	  case SIGHUP:
		  reloadconfig = 1;
		  break;

	  case SIGUSR1:
		  nextcheckpoint = 0;
		  break;

	  default:
		  running = 0;
		  termsig = signum;
		  break;
	}
}

void save_checkpoint(char *filename)
{
	char *subfn;
	FILE *fd = fopen(filename, "w");
	activealerts_t *awalk;
	unsigned char *pgmsg, *ackmsg;

	if (fd == NULL) return;

	for (awalk = alistBegin(); (awalk); awalk = alistNext()) {
		if (awalk->state == A_DEAD) continue;

		pgmsg = ackmsg = "";

		fprintf(fd, "%s|%s|%s|%s|%s|%d|%d|%s|",
			awalk->hostname, awalk->testname, awalk->location, awalk->ip,
			colorname(awalk->maxcolor),
			(int) awalk->eventstart,
			(int) awalk->nextalerttime,
			statename[awalk->state]);
		if (awalk->pagemessage) pgmsg = nlencode(awalk->pagemessage);
		fprintf(fd, "%s|", pgmsg);
		if (awalk->ackmessage) ackmsg = nlencode(awalk->ackmessage);
		fprintf(fd, "%s\n", ackmsg);
	}
	fclose(fd);

	subfn = (char *)malloc(strlen(filename)+5);
	sprintf(subfn, "%s.sub", filename);
	save_state(subfn);
	xfree(subfn);
}

int load_checkpoint(char *filename)
{
	char *subfn;
	FILE *fd;
	strbuffer_t *inbuf;
	char statuscmd[1024];
	char *statusbuf = NULL;
	sendreturn_t *sres;
	int xymondresult;


	sprintf(statuscmd, "xymondboard color=%s fields=hostname,testname,color", xgetenv("ALERTCOLORS"));
	sres = newsendreturnbuf(1, NULL);
	xymondresult = sendmessage(statuscmd, NULL, XYMON_TIMEOUT, sres);
	statusbuf = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);

	if ((xymondresult != XYMONSEND_OK) || (statusbuf == NULL) || (*statusbuf == '\0')) {
		errprintf("xymond_alert: xymond not available or had empty response; error: %d\n", xymondresult);
		running = 0;
		return -1;
	}

	fd = fopen(filename, "r");
	if (fd == NULL) {
		errprintf("xymond_alert: Couldn't open checkpoint file '%s', but continuing: %s\n", filename, strerror(errno));
		return 0;
	}

	initfgets(fd);
	inbuf = newstrbuffer(0);
	while (unlimfgets(inbuf, fd)) {
		char *item[20], *p;
		int i;

		sanitize_input(inbuf, 0, 0);

		i = 0; p = gettok(STRBUF(inbuf), "|");
		while (p && (i < 20)) {
			item[i++] = p;
			p = gettok(NULL, "|");
		}

		if (i == 9) {
			/* There was no ack message */
			item[i++] = "";
		}

		if (i > 9) {
			char *valid = NULL;

			activealerts_t *newalert = (activealerts_t *)calloc(1, sizeof(activealerts_t));
			newalert->hostname = find_name(hostnames, item[0]);
			newalert->testname = find_name(testnames, item[1]);
			newalert->location = find_name(locations, item[2]);
			strcpy(newalert->ip, item[3]);
			newalert->color = newalert->maxcolor = parse_color(item[4]);
			newalert->eventstart = (time_t) atoi(item[5]);
			newalert->nextalerttime = (time_t) atoi(item[6]);
			newalert->state = A_PAGING;

			if (statusbuf) {
				char *key;

				key = (char *)malloc(strlen(newalert->hostname) + strlen(newalert->testname) + 100);
				sprintf(key, "\n%s|%s|%s\n", newalert->hostname, newalert->testname, colorname(newalert->color));
				valid = strstr(statusbuf, key);
				if (!valid && (strncmp(statusbuf, key+1, strlen(key+1)) == 0)) valid = statusbuf;
				xfree(key);
			}
			if (!valid) {
				errprintf("Stale alert for %s:%s dropped\n", newalert->hostname, newalert->testname);
				xfree(newalert);
				continue;
			}

			while (strcmp(item[7], statename[newalert->state]) && (newalert->state < A_DEAD)) 
				newalert->state++;
			/* Config might have changed while we were down */
			if (newalert->state == A_NORECIP) newalert->state = A_PAGING;
			newalert->pagemessage = newalert->ackmessage = NULL;
			if (strlen(item[8])) {
				nldecode(item[8]);
				newalert->pagemessage = strdup(item[8]);
			}
			if (strlen(item[9])) {
				nldecode(item[9]);
				newalert->ackmessage = strdup(item[9]);
			}
			add_active(newalert->hostname, newalert);
		}
	}
	fclose(fd);
	freestrbuffer(inbuf);

	subfn = (char *)malloc(strlen(filename)+5);
	sprintf(subfn, "%s.sub", filename);
	load_state(subfn, statusbuf);
	xfree(subfn);
	if (statusbuf) xfree(statusbuf);
	return 0;
}

int main(int argc, char *argv[])
{
	char *msg;
	int seq;
	int argi;
	int alertcolors, alertinterval;
	char *configfn = NULL;
	char *checkfn = NULL;
	int loadresult;
	int reloadconfigtime = 0;
	int checkpointinterval = 900;
	char acklogfn[PATH_MAX];
	FILE *acklogfd = NULL;
	char notiflogfn[PATH_MAX];
	FILE *notiflogfd = NULL;
	char *tracefn = NULL;
	struct sigaction sa;
	int configchanged;
	time_t lastxmit = 0;

	MEMDEFINE(acklogfn);
	MEMDEFINE(notiflogfn);

	/* Don't save the error buffer */
	save_errbuf = 0;

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	alertinterval = 60*atoi(xgetenv("ALERTREPEAT"));

	/* Create our loookup-trees */
	hostnames = xtreeNew(strcasecmp);
	testnames = xtreeNew(strcasecmp);
	locations = xtreeNew(strcasecmp);

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--config=")) {
			configfn = strdup(strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--checkpoint-file=")) {
			checkfn = strdup(strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--checkpoint-interval=")) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointinterval = atoi(p);
		}
		else if (argnmatch(argv[argi], "--reload-interval=")) {
			char *p = strchr(argv[argi], '=') + 1;
			reloadinterval = atoi(p);
		}
		else if (argnmatch(argv[argi], "--loadhostsfromxymond")) {
			loadhostsfromxymond = 1;
		}
		else if (argnmatch(argv[argi], "--dump-config")) {
			load_alertconfig(configfn, alertcolors, alertinterval);
			dump_alertconfig(1);
			return 0;
		}
		else if (argnmatch(argv[argi], "--cfid")) {
			include_configid = 1;
		}
		else if (argnmatch(argv[argi], "--test")) {
			char *testhost = NULL, *testservice = NULL, *testpage = NULL, 
			     *testcolor = "red", *testgroups = NULL;
			void *hinfo;
			int testdur = 0;
			FILE *logfd = NULL;
			activealerts_t *awalk = NULL;
			int paramno = 0;

			set_localalertmode(1); /* create a dummy hostinfo record to try to match against */
			argi++; if (argi < argc) testhost = argv[argi];
			argi++; if (argi < argc) testservice = argv[argi];
			argi++; 
			while (argi < argc) {
				if (strncasecmp(argv[argi], "--duration=", 11) == 0) {
					testdur = durationvalue(strchr(argv[argi], '=')+1);
				}
				else if (strncasecmp(argv[argi], "--color=", 8) == 0) {
					testcolor = strchr(argv[argi], '=')+1;
				}
				else if (strncasecmp(argv[argi], "--group=", 8) == 0) {
					testgroups = strchr(argv[argi], '=')+1;
				}
				else if (strncasecmp(argv[argi], "--time=", 7) == 0) {
					fakestarttime = (time_t)atoi(strchr(argv[argi], '=')+1);
				}
				else {
					paramno++;
					if (paramno == 1) testdur = atoi(argv[argi]);
					else if (paramno == 2) testcolor = argv[argi];
					else if (paramno == 3) fakestarttime = (time_t) atoi(argv[argi]);
				}

				argi++;
			}

			if ((testhost == NULL) || (testservice == NULL)) {
				printf("Usage: xymond_alert --test HOST SERVICE [options]\n");
				printf("Possible options:\n\t[--duration=MINUTES]\n\t[--color=COLOR]\n\t[--group=GROUPNAME]\n\t[--time=TIMESPEC]\n");

				return 1;
			}

			load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
			hinfo = hostinfo(testhost);
			if (hinfo) {
				testpage = strdup(xmh_item(hinfo, XMH_ALLPAGEPATHS));
			}
			else {
				errprintf("Host not found in hosts.cfg - assuming it is on the top page\n");
				testpage = "";
			}

			awalk = (activealerts_t *)calloc(1, sizeof(activealerts_t));
			awalk->hostname = find_name(hostnames, testhost);
			awalk->testname = find_name(testnames, testservice);
			awalk->location = find_name(locations, testpage);
			strcpy(awalk->ip, "127.0.0.1");
			awalk->color = awalk->maxcolor = parse_color(testcolor);
			awalk->pagemessage = "Test of the alert configuration";
			awalk->eventstart = getcurrenttime(NULL) - testdur*60;
			awalk->groups = (testgroups ? strdup(testgroups) : NULL);
			awalk->state = A_PAGING;
			awalk->cookie = 12345;
			awalk->next = NULL;

			logfd = fopen("/dev/null", "w");
			starttrace(NULL);
			testonly = 1;

			load_alertconfig(configfn, alertcolors, alertinterval);
			load_holidays(0);
			send_alert(awalk, logfd);
			return 0;
		}
		else if (argnmatch(argv[argi], "--trace=")) {
			tracefn = strdup(strchr(argv[argi], '=')+1);
			starttrace(tracefn);
		}
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
		else {
			errprintf("Unknown option '%s'\n", argv[argi]);
		}
	}

	/* Do the network stuff if needed */
	net_worker_run(ST_ALERT, LOC_SINGLESERVER, NULL);


	/* Load our hostnames */
	loadresult = load_hostnames( (loadhostsfromxymond ? "@" : xgetenv("HOSTSCFG")) , NULL, get_fqdn() );
	if (loadresult != 0) {
		errprintf("Cannot load host configuration from %s\n", (loadhostsfromxymond ? "xymond" : xgetenv("HOSTSCFG")));
		running = 0;
		return -1;
        }
	else {
		reloadconfig = 0;
		reloadconfigtime = getcurrenttime(NULL) + reloadinterval;
	}

	if (checkfn) {
		if ( load_checkpoint(checkfn) != 0 ) {
			errprintf("xymond_alert: cannot load checkpoint file or current state; aborting\n");
			return -1;
		}
		nextcheckpoint = gettimer() + checkpointinterval;
		dbgprintf("Next checkpoint at %d, interval %d\n", (int) nextcheckpoint, checkpointinterval);
	}

	setup_signalhandler("xymond_alert");
	/* Need to handle these ourselves, so we can shutdown and save state-info */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);

	if (xgetenv("XYMONSERVERLOGS")) {
		sprintf(acklogfn, "%s/acknowledge.log", xgetenv("XYMONSERVERLOGS"));
		acklogfd = fopen(acklogfn, "a");
		sprintf(notiflogfn, "%s/notifications.log", xgetenv("XYMONSERVERLOGS"));
		notiflogfd = fopen(notiflogfn, "a");
	}

	/*
	 * The general idea here is that this loop handles receiving of alert-
	 * and ack-messages from the master daemon, and maintains a list of 
	 * host+test combinations that may have alerts going out.
	 *
	 * This module does not deal with any specific alert-configuration, 
	 * it just picks up the alert messages, maintains the list of 
	 * known tests that are in some sort of critical condition, and
	 * periodically pushes alerts to the do_alert.c module for handling.
	 *
	 * The only modification of alerts that happen here is the handling
	 * of when the next alert is due. It calls into the next_alert() 
	 * routine to learn when an alert should be repeated, and also 
	 * deals with Acknowledgments that stop alerts from going out for
	 * a period of time.
	 */
	while (running) {
		char *eoln, *restofmsg;
		char *metadata[20];
		char *p;
		int metacount;
		char *hostname = NULL, *testname = NULL;
		struct timespec timeout;
		time_t now, nowtimer;
		int anytogo;
		activealerts_t *awalk;
		int childstat;

		nowtimer = gettimer();
		if (checkfn && (nowtimer > nextcheckpoint)) {
			dbgprintf("Saving checkpoint\n");
			nextcheckpoint = nowtimer + checkpointinterval;
			save_checkpoint(checkfn);

			if (acklogfd) acklogfd = freopen(acklogfn, "a", acklogfd);
			if (notiflogfd) notiflogfd = freopen(notiflogfn, "a", notiflogfd);
		}

		if (reloadconfig || (nowtimer > reloadconfigtime)) {
			dbgprintf("Reloading hostnames\n");
			reloadconfig = 0;
			loadresult = load_hostnames( (loadhostsfromxymond ? "@" : xgetenv("HOSTSCFG")) , NULL, get_fqdn() );
			if (loadresult == -1) {
				errprintf("Cannot load host configuration from %s; postponing for 90s\n", (loadhostsfromxymond ? "xymond" : xgetenv("HOSTSCFG")));
				reloadconfigtime = nowtimer + 90;
			}
			else reloadconfigtime = nowtimer + reloadinterval;
		}

		timeout.tv_sec = 60; timeout.tv_nsec = 0;
		msg = get_xymond_message(C_PAGE, "xymond_alert", &seq, &timeout);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		/* See what time it is - must happen AFTER the timeout */
		now = getcurrenttime(NULL);

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		/* 
		 * Now parse the meta-data into elements.
		 * We use our own "gettok()" routine which works
		 * like strtok(), but can handle empty elements.
		 */
		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < 19)) {
			metadata[metacount] = p;
			metacount++;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if (metacount > 3) hostname = metadata[3];
		if (metacount > 4) testname = metadata[4];

		if ((metacount > 10) && (strncmp(metadata[0], "@@page", 6) == 0)) {
			/* @@page|timestamp|sender|hostname|testname|hostip|expiretime|color|prevcolor|changetime|location|cookie|osname|classname|grouplist|modifiers */

			int newcolor, newalertstatus, oldalertstatus;

			dbgprintf("Got page message from %s:%s\n", hostname, testname);
			traceprintf("@@page %s:%s:%s=%s\n", hostname, testname, metadata[10], metadata[7]);

			awalk = find_active(hostname, testname);
			if (awalk == NULL) {
				char *hwalk = find_name(hostnames, hostname);
				char *twalk = find_name(testnames, testname);
				char *pwalk = find_name(locations, metadata[10]);

				awalk = (activealerts_t *)calloc(1, sizeof(activealerts_t));
				awalk->hostname = hwalk;
				awalk->testname = twalk;
				awalk->location = pwalk;
				awalk->cookie = -1;
				awalk->state = A_DEAD;
				/*
				 * Use changetime here, if we restart the alert module then
				 * this gets the duration values more right than using "now".
				 * Also, define this only when a new alert arrives - we should
				 * NOT clear this when a status goes yellow->red, or if it
				 * flaps between yellow and red.
				 */
				awalk->eventstart = atoi(metadata[9]);
				add_active(awalk->hostname, awalk);
				traceprintf("New record\n");
			}

			newcolor = parse_color(metadata[7]);
			oldalertstatus = ((alertcolors & (1 << awalk->color)) != 0);
			newalertstatus = ((alertcolors & (1 << newcolor)) != 0);

			traceprintf("state %d->%d\n", oldalertstatus, newalertstatus);

			if (newalertstatus) {
				/* It's in an alert state. */
				awalk->color = newcolor;
				awalk->state = A_PAGING;

				if (newcolor > awalk->maxcolor) {
					if (awalk->maxcolor != 0) {
						/*
						 * Severity has increased (yellow -> red).
						 * Clear the repeat-interval, and set maxcolor to
						 * the new color. If it drops to yellow again,
						 * maxcolor stays at red, so a test that flaps
						 * between yellow and red will only alert on red
						 * the first time, and then follow the repeat
						 * interval.
						 */
						dbgprintf("Severity increased, cleared repeat interval: %s/%s %s->%s\n",
							awalk->hostname, awalk->testname,
							colorname(awalk->maxcolor), colorname(newcolor));
						clear_interval(awalk);
					}

					awalk->maxcolor = newcolor;
				}
			}
			else {
				/* 
				 * Send one "recovered" message out now, then go to A_DEAD.
				 * Don't update the color here - we want recoveries to go out 
				 * only if the alert color triggered an alert
				 */
				awalk->state = (newcolor == COL_BLUE) ? A_DISABLED : A_RECOVERED;
			}

			if (oldalertstatus != newalertstatus) {
				dbgprintf("Alert status changed from %d to %d\n", oldalertstatus, newalertstatus);
				clear_interval(awalk);
			}

			strcpy(awalk->ip, metadata[5]);
			awalk->cookie = atoi(metadata[11]);
			if (awalk->osname) xfree(awalk->osname);
			awalk->osname    = (metadata[12] ? strdup(metadata[12]) : NULL);
			if (awalk->classname) xfree(awalk->classname);
			awalk->classname = (metadata[13] ? strdup(metadata[13]) : NULL);
			if (awalk->groups) xfree(awalk->groups);
			awalk->groups    = (metadata[14] ? strdup(metadata[14]) : NULL);
			if (awalk->pagemessage) xfree(awalk->pagemessage);
			if (metadata[15]) {
				/* Modifiers are more interesting than the message itself */
				awalk->pagemessage = (char *)malloc(strlen(awalk->hostname) + strlen(awalk->testname) + strlen(colorname(awalk->color)) + strlen(metadata[15]) + strlen(restofmsg) + 10);
				sprintf(awalk->pagemessage, "%s:%s %s\n%s\n%s",
					awalk->hostname, awalk->testname, colorname(awalk->color), metadata[15], restofmsg);
			}
			else {
				awalk->pagemessage = strdup(restofmsg);
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@ack", 5) == 0)) {
 			/* @@ack|timestamp|sender|hostname|testname|hostip|expiretime */

			/*
			 * An ack is handled simply by setting the next
			 * alert-time to when the ack expires.
			 */
			time_t nextalert = atoi(metadata[6]);

			dbgprintf("Got ack message from %s:%s\n", hostname, testname);
			traceprintf("@@ack: %s:%s now=%d, ackeduntil %d\n",
				     hostname, testname, (int)now, (int)nextalert);

			awalk = find_active(hostname, testname);

			if (acklogfd) {
				int cookie = (awalk ? awalk->cookie : -1);
				int color  = (awalk ? awalk->color : 0);

				fprintf(acklogfd, "%d\t%d\t%d\t%d\t%s\t%s.%s\t%s\t%s\n",
					(int)now, cookie, 
					(int)((nextalert - now) / 60), cookie,
					"np_filename_not_used", 
					hostname, testname, 
					colorname(color),
					nlencode(restofmsg));
				fflush(acklogfd);
			}

			if (awalk && (awalk->state == A_PAGING)) {
				traceprintf("Record updated\n");
				awalk->state = A_ACKED;
				awalk->nextalerttime = nextalert;
				if (awalk->ackmessage) xfree(awalk->ackmessage);
				awalk->ackmessage = strdup(restofmsg);
			}
			else {
				traceprintf("No record\n");
			}
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@notify", 5) == 0)) {
			/* @@notify|timestamp|sender|hostname|testname|pagepath */

			char *hwalk = find_name(hostnames, hostname);
			char *twalk = find_name(testnames, testname);
			char *pwalk = find_name(locations, (metadata[5] ? metadata[5] : ""));

			awalk = (activealerts_t *)calloc(1, sizeof(activealerts_t));
			awalk->hostname = hwalk;
			awalk->testname = twalk;
			awalk->location = pwalk;
			awalk->cookie = -1;
			awalk->pagemessage = strdup(restofmsg);
			awalk->eventstart = getcurrenttime(NULL);
			awalk->state = A_NOTIFY;
			add_active(awalk->hostname, awalk);
		}
		else if ((metacount > 3) && 
			 ((strncmp(metadata[0], "@@drophost", 10) == 0) || (strncmp(metadata[0], "@@dropstate", 11) == 0))) {
			/* @@drophost|timestamp|sender|hostname */
			/* @@dropstate|timestamp|sender|hostname */
			xtreePos_t handle;

			handle = xtreeFind(hostnames, hostname);
			if (handle != xtreeEnd(hostnames)) {
				alertanchor_t *anchor = (alertanchor_t *)xtreeData(hostnames, handle);
				for (awalk = anchor->head; (awalk); awalk = awalk->next) awalk->state = A_DEAD;
			}
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			/* @@droptest|timestamp|sender|hostname|testname */

			awalk = find_active(hostname, testname);
			if (awalk) awalk->state = A_DEAD;
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */

			/* 
			 * We handle rename's simply by dropping the alert. If there is still an
			 * active alert for the host, it will have to be dealt with when the next
			 * status update arrives.
			 */
			xtreePos_t handle;

			handle = xtreeFind(hostnames, hostname);
			if (handle != xtreeEnd(hostnames)) {
				alertanchor_t *anchor = (alertanchor_t *)xtreeData(hostnames, handle);
				for (awalk = anchor->head; (awalk); awalk = awalk->next) awalk->state = A_DEAD;
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */

			/* 
			 * We handle rename's simply by dropping the alert. If there is still an
			 * active alert for the host, it will have to be dealt with when the next
			 * status update arrives.
			 */
			awalk = find_active(hostname, testname);
			if (awalk) awalk->state = A_DEAD;
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			errprintf("Got a shutdown message\n");
			continue;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);

				if (tracefn) {
					stoptrace();
					starttrace(tracefn);
				}
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			logprintf("Received reload request\n");
			reloadconfig = 1;
			/* Nothing ... right now */
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Timeout */
		}

		/*
		 * When a burst of alerts happen, we get lots of alert messages
		 * coming in quickly. So lets handle them in bunches and only 
		 * do the full alert handling once every 10 secs - that lets us
		 * combine a bunch of alerts into one transmission process.
		 */
		if (nowtimer < (lastxmit+10)) continue;
		lastxmit = nowtimer;

		/* 
		 * Loop through the activealerts list and see if anything is pending.
		 * This is an optimization, we could just as well just fork off the
		 * notification child and let it handle all of it. But there is no
		 * reason to fork a child process unless it is going to do something.
		 */
		configchanged = load_alertconfig(configfn, alertcolors, alertinterval);
		configchanged += load_holidays(0);
		if (configchanged) reloadconfig = 1;

		anytogo = 0;
		for (awalk = alistBegin(); (awalk); awalk = alistNext()) {
			int anymatch = 0;

			switch (awalk->state) {
			  case A_NORECIP:
				if (!configchanged) break;

				/* The configuration has changed - switch NORECIP -> PAGING */
				awalk->state = A_PAGING;
				clear_interval(awalk);
				/* Fall through */

			  case A_PAGING:
				if (have_recipient(awalk, &anymatch)) {
					if (awalk->nextalerttime <= now) anytogo++;
				}
				else {
					if (!anymatch) {
						awalk->state = A_NORECIP;
						cleanup_alert(awalk);
					}
				}
				break;

			  case A_ACKED:
				if (awalk->nextalerttime <= now) {
					/* An ack has expired, so drop the ack message and switch to A_PAGING */
					anytogo++;
					if (awalk->ackmessage) xfree(awalk->ackmessage);
					awalk->state = A_PAGING;
				}
				break;

			  case A_RECOVERED:
			  case A_DISABLED:
			  case A_NOTIFY:
				anytogo++;
				break;

			  case A_DEAD:
				break;
			}
		}
		dbgprintf("%d alerts to go\n", anytogo);

		if (anytogo) {
			pid_t childpid;

			childpid = fork();
			if (childpid == 0) {
				/* The child */
				start_alerts();
				for (awalk = alistBegin(); (awalk); awalk = alistNext()) {
					switch (awalk->state) {
					  case A_PAGING:
						if (awalk->nextalerttime <= now) {
							send_alert(awalk, notiflogfd);
						}
						break;

					  case A_ACKED:
						/* Cannot be A_ACKED unless the ack is still valid, so no alert. */
						break;

					  case A_RECOVERED:
					  case A_DISABLED:
					  case A_NOTIFY:
						send_alert(awalk, notiflogfd);
						break;

					  case A_NORECIP:
					  case A_DEAD:
						break;
					}
				}
				finish_alerts();

				/* Child does not continue */
				exit(0);
			}
			else if (childpid < 0) {
				errprintf("Fork failed, cannot send alerts: %s\n", strerror(errno));
			}
		}

		/* Update the state flag and the next-alert timestamp */
		for (awalk = alistBegin(); (awalk); awalk = alistNext()) {
			switch (awalk->state) {
			  case A_PAGING:
				if (awalk->nextalerttime <= now) awalk->nextalerttime = next_alert(awalk);
				break;

			  case A_NORECIP:
				break;

			  case A_ACKED:
				/* Still cannot get here except if ack is still valid */
				break;

			  case A_RECOVERED:
			  case A_DISABLED:
			  case A_NOTIFY:
				awalk->state = A_DEAD;
				/* Fall through */

			  case A_DEAD:
				cleanup_alert(awalk); 
				break;
			}
		}

		clean_all_active();

	}

	if (checkfn) save_checkpoint(checkfn);
	if (acklogfd) fclose(acklogfd);
	if (notiflogfd) fclose(notiflogfd);
	stoptrace();

	MEMUNDEFINE(notiflogfn);
	MEMUNDEFINE(acklogfn);

	if (termsig >= 0) {
		errprintf("Terminated by signal %d\n", termsig);
	}

	return 0;
}

