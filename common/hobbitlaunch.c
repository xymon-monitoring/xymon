/*----------------------------------------------------------------------------*/
/* Hobbit application launcher.                                               */
/*                                                                            */
/* This is used to launch various parts of the Hobbit system. Some programs   */
/* start up once and keep running, other must run at various intervals.       */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitlaunch.c,v 1.41 2006-07-20 16:06:41 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "libbbgen.h"

/*
 * config file format:
 *
 * [hobbitd]
 * 	CMD hobbitd --no-daemon
 * 	LOGFILE /var/log/hobbitd.log
 *
 * [bbdisplay]
 * 	CMD bb-display.sh
 * 	INTERVAL 5m
 */

#define MAX_FAILS 5

typedef struct grouplist_t {
	char *groupname;
	int currentuse, maxuse;
	struct grouplist_t *next;
} grouplist_t;

typedef struct tasklist_t {
	char *key;
	int disabled;
	grouplist_t *group;
	char *cmd;
	int interval;
	char *logfile;
	char *envfile, *envarea;
	pid_t pid;
	time_t laststart;
	int exitcode;
	int failcount;
	int cfload;	/* Used while reloading a configuration */
	int beingkilled;
	struct tasklist_t *depends;
	struct tasklist_t *next;
} tasklist_t;
tasklist_t *taskhead = NULL;
tasklist_t *tasktail = NULL;
grouplist_t *grouphead = NULL;

volatile time_t nextcfgload = 0;
volatile int running = 1;
volatile int dologswitch = 0;

void update_task(tasklist_t *newtask)
{
	tasklist_t *twalk;
	int freeit = 1;
	int logfilechanged = 0;
	int envfilechanged = 0;

	for (twalk = taskhead; (twalk && (strcmp(twalk->key, newtask->key))); twalk = twalk->next);

	if (twalk) {
		if (twalk->logfile && newtask->logfile) {
			logfilechanged = strcmp(twalk->logfile, newtask->logfile);
		}
		else if (twalk->logfile || newtask->logfile) {
			logfilechanged = 1;
		}
		else {
			logfilechanged = 0;
		}

		if (twalk->envfile && newtask->envfile) {
			envfilechanged = strcmp(twalk->envfile, newtask->envfile);
		}
		else if (twalk->envfile || newtask->envfile) {
			envfilechanged = 1;
		}
		else {
			envfilechanged = 0;
		}

		if (twalk->envarea && newtask->envarea) {
			envfilechanged = strcmp(twalk->envarea, newtask->envarea);
		}
		else if (twalk->envarea || newtask->envarea) {
			envfilechanged = 1;
		}
		else {
			envfilechanged = 0;
		}
	}

	if (newtask->cmd == NULL) {
		errprintf("Configuration error, no command for task %s\n", newtask->key);
	}
	else if (twalk == NULL) {
		/* New task, just add it to the list */
		newtask->cfload = 0;

		if (taskhead == NULL) taskhead = newtask;
		else tasktail->next = newtask;

		tasktail = newtask;
		freeit = 0;
	}
	else if (strcmp(twalk->cmd, newtask->cmd) || logfilechanged || envfilechanged) {
		/* Task changed. */
		xfree(twalk->cmd); 
		if (twalk->logfile) xfree(twalk->logfile);
		if (twalk->envfile) xfree(twalk->envfile);
		if (twalk->envarea) xfree(twalk->envarea);
		twalk->cmd = strdup(newtask->cmd);
		if (newtask->logfile) twalk->logfile = strdup(newtask->logfile); else twalk->logfile = NULL;
		if (newtask->envfile) twalk->envfile = strdup(newtask->envfile); else twalk->envfile = NULL;
		if (newtask->envarea) twalk->envarea = strdup(newtask->envarea); else twalk->envarea = NULL;

		/* Must bounce the task */
		twalk->cfload = 1;
	}
	else if (twalk->interval != newtask->interval) {
		twalk->interval = newtask->interval;
		twalk->cfload = 0;
	}
	else if (twalk->disabled != newtask->disabled) {
		twalk->disabled = newtask->disabled;
		twalk->cfload = 1;
	}
	else {
		/* Task was unchanged */
		twalk->cfload = 0;
	}

	if (freeit) {
		xfree(newtask->key);
		if (newtask->cmd) xfree(newtask->cmd);
		if (newtask->logfile) xfree(newtask->logfile);
		if (newtask->envfile) xfree(newtask->envfile);
		if (newtask->envarea) xfree(newtask->envarea);
		xfree(newtask);
	}
}

void load_config(char *conffn)
{
	static void *configfiles = NULL;
	tasklist_t *twalk, *curtask = NULL;
	FILE *fd;
	strbuffer_t *inbuf;
	char *p;

	/* First check if there were no modifications at all */
	if (configfiles) {
		if (!stackfmodified(configfiles)){
			dbgprintf("No files modified, skipping reload of %s\n", conffn);
			return;
		}
		else {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
	}

	errprintf("Loading tasklist configuration from %s\n", conffn);

	/* The cfload flag: -1=delete task, 0=old task unchanged, 1=new/changed task */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		twalk->cfload = -1;
		twalk->group = NULL;
	}

	fd = stackfopen(conffn, "r", &configfiles);
	if (fd == NULL) {
		errprintf("Cannot open configuration file %s: %s\n", conffn, strerror(errno));
		return;
	}

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		sanitize_input(inbuf, 1, 0); if (STRBUFLEN(inbuf) == 0) continue;

		p = STRBUF(inbuf);
		if (*p == '[') {
			/* New task */
			char *endp;

			if (curtask) {
				update_task(curtask);
				curtask = NULL;
			}

			p++; endp = strchr(p, ']');
			if (endp == NULL) continue;
			*endp = '\0';

			curtask = (tasklist_t *)calloc(1, sizeof(tasklist_t));
			curtask->key = strdup(p);
		}
		else if (curtask && (strncasecmp(p, "CMD ", 4) == 0)) {
			p += 3;
			p += strspn(p, " \t");
			curtask->cmd = strdup(p);
		}
		else if (strncasecmp(p, "GROUP ", 6) == 0) {
			/* Note: GROUP can be used by itself to define a group, or inside a task definition */
			char *groupname;
			int maxuse;
			grouplist_t *gwalk;

			p += 3;
			p += strspn(p, " \t");
			groupname = p;
			p += strcspn(p, " \t");
			if (isdigit((int) *p)) maxuse = atoi(p); else maxuse = 1;

			/* Find or create the grouplist entry */
			for (gwalk = grouphead; (gwalk && (strcmp(gwalk->groupname, groupname))); gwalk = gwalk->next);
			if (gwalk == NULL) {
				gwalk = (grouplist_t *)malloc(sizeof(grouplist_t));
				gwalk->groupname = strdup(groupname);
				gwalk->maxuse = maxuse;
				gwalk->currentuse = 0;
				gwalk->next = grouphead;
				grouphead = gwalk;
			}

			if (curtask) curtask->group = gwalk;
		}
		else if (curtask && (strncasecmp(p, "INTERVAL ", 9) == 0)) {
			char *tspec;
			p += 9;
			curtask->interval = atoi(p);
			tspec = p + strspn(p, "0123456789");
			switch (*tspec) {
			  case 'm': curtask->interval *= 60; break;	/* Minutes */
			  case 'h': curtask->interval *= 3600; break;	/* Hours */
			  case 'd': curtask->interval *= 86400; break;	/* Days */
			}
		}
		else if (curtask && (strncasecmp(p, "LOGFILE ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			curtask->logfile = strdup(p);
		}
		else if (curtask && (strncasecmp(p, "NEEDS ", 6) == 0)) {
			p += 6;
			p += strspn(p, " \t");
			for (twalk = taskhead; (twalk && strcmp(twalk->key, p)); twalk = twalk->next);
			if (twalk) {
				curtask->depends = twalk;
			}
			else {
				errprintf("Configuration error, unknown dependency %s->%s", curtask->key, p);
			}
		}
		else if (curtask && (strncasecmp(p, "ENVFILE ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			curtask->envfile = strdup(p);
		}
		else if (curtask && (strncasecmp(p, "ENVAREA ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			curtask->envarea = strdup(p);
		}
		else if (curtask && (strcasecmp(p, "DISABLED") == 0)) {
			curtask->disabled = 1;
		}
	}
	if (curtask) update_task(curtask);
	stackfclose(fd);
	freestrbuffer(inbuf);

	/* Running tasks that have been deleted or changed are killed off now. */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		switch (twalk->cfload) {
		  case -1:
			/* Kill the task, if active */
			if (twalk->pid) {
				dbgprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
				kill(twalk->pid, SIGTERM);
			}
			/* And prepare to free this tasklist entry */
			xfree(twalk->key); 
			xfree(twalk->cmd); 
			if (twalk->logfile) xfree(twalk->logfile);
			if (twalk->envfile) xfree(twalk->envfile);
			if (twalk->envarea) xfree(twalk->envarea);
			break;

		  case 0:
			/* Do nothing */
			break;

		  case 1:
			/* Bounce the task, if it is active */
			if (twalk->pid) {
				dbgprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
				kill(twalk->pid, SIGTERM);
			}
			break;
		}
	}

	/* First clean out dead tasks at the start of the list */
	while (taskhead->cfload == -1) {
		tasklist_t *tmp;

		tmp = taskhead;
		taskhead = taskhead->next;
		xfree(tmp);
	}

	/* Then unlink and free those inside the list */
	twalk = taskhead;
	while (twalk && twalk->next) {
		tasklist_t *tmp;

		if (twalk->next->cfload == -1) {
			tmp = twalk->next;
			twalk->next = tmp->next;
			xfree(tmp);
		}
		else twalk = twalk->next;
	}

	if (taskhead == NULL) 
		tasktail = NULL;
	else {
		tasktail = taskhead;
		while (tasktail->next) tasktail = tasktail->next;
	}

	/* Make sure group usage counts are correct (groups can change) */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		if (twalk->group) twalk->group->currentuse = 0;
	}
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		if (twalk->group && twalk->pid) twalk->group->currentuse++;
	}
}

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGCHLD:
		break;

	  case SIGHUP:
		nextcfgload = 0;
		dologswitch = 1;
		break;

	  case SIGTERM:
		running = 0;
		break;
	}
}

int main(int argc, char *argv[])
{
	tasklist_t *twalk, *dwalk;
	grouplist_t *gwalk;
	int argi;
	int daemonize = 1;
	int verbose = 0;
	char *config = "/etc/hobbitlaunch.cfg";
	char *logfn = NULL;
	char *pidfn = NULL;
	pid_t cpid;
	int status;
	struct sigaction sa;
	char *envarea = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else if (strcmp(argv[argi], "--verbose") == 0) {
			verbose = 1;
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			config = strdup(expand_env(p+1));
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');
			logfn = strdup(expand_env(p+1));
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--pidfile=")) {
			char *p = strchr(argv[argi], '=');
			pidfn = strdup(expand_env(p+1));
		}
		else if (strcmp(argv[argi], "--dump") == 0) {
			/* Dump configuration */

			load_config(config);
			for (gwalk = grouphead; (gwalk); gwalk = gwalk->next) {
				if (gwalk->maxuse > 1) printf("GROUP %s %d\n", gwalk->groupname, gwalk->maxuse);
			}
			printf("\n");
			for (twalk = taskhead; (twalk); twalk = twalk->next) {
				printf("[%s]\n", twalk->key);
				printf("\tCMD %s\n", twalk->cmd);
				if (twalk->group)    printf("\tGROUP %s\n", twalk->group->groupname);
				if (twalk->depends)  printf("\tNEEDS %s\n", twalk->depends->key);
				if (twalk->interval) printf("\tINTERVAL %d\n", twalk->interval);
				if (twalk->logfile)  printf("\tLOGFILE %s\n", twalk->logfile);
				if (twalk->envfile)  printf("\tENVFILE %s\n", twalk->envfile);
				if (twalk->envarea)  printf("\tENVAREA %s\n", twalk->envarea);
				printf("\n");
			}
			return 0;
		}
	}

	/* Go daemon */
	if (daemonize) {
		pid_t childpid;

		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork child\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent exits */
			if (pidfn) {
				FILE *pidfd = fopen(pidfn, "w");

				if (pidfd) {
					fprintf(pidfd, "%d\n", (int)childpid);
					fclose(pidfd);
				}
			}

			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	/* If using a logfile, switch stdout and stderr to go there */
	if (logfn) {
		/* Should we close stdin here ? No ... */
		freopen("/dev/null", "r", stdin);
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	save_errbuf = 0;
	setup_signalhandler("hobbitlaunch");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	errprintf("hobbitlaunch starting\n");
	while (running) {
		time_t now = gettimer();

		if (now >= nextcfgload) {
			load_config(config);
			nextcfgload = (now + 30);
		}

		if (logfn && dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			dologswitch = 0;
		}

		/* Pick up children that have terminated */
		while ((cpid = wait3(&status, WNOHANG, NULL)) > 0) {
			for (twalk = taskhead; (twalk && (twalk->pid != cpid)); twalk = twalk->next);
			if (twalk) {
				twalk->pid = 0;
				twalk->beingkilled = 0;
				if (WIFEXITED(status)) {
					twalk->exitcode = WEXITSTATUS(status);
					if (twalk->exitcode) {
						errprintf("Task %s terminated, status %d\n", twalk->key, twalk->exitcode);
						twalk->failcount++;
					}
					else {
						twalk->failcount = 0;
					}
				}
				else if (WIFSIGNALED(status)) {
					twalk->exitcode = -WTERMSIG(status);
					twalk->failcount++;
					errprintf("Task %s terminated by signal %d\n", twalk->key, abs(twalk->exitcode));
					twalk->failcount++;
				}

				if (twalk->group) twalk->group->currentuse--;

				/* Tasks that depend on this task should be killed ... */
				for (dwalk = taskhead; (dwalk); dwalk = dwalk->next) {
					if ((dwalk->depends == twalk) && (dwalk->pid > 0)) {
						kill(dwalk->pid, SIGTERM);
					}
				}
			}
		}

		/* See what new tasks need to get going */
		dbgprintf("\n");
		dbgprintf("Starting tasklist scan\n");
		for (twalk = taskhead; (twalk); twalk = twalk->next) {
			if ((twalk->pid == 0) && !twalk->disabled && (now >= (twalk->laststart + twalk->interval))) {

				if (twalk->depends && ((twalk->depends->pid == 0) || (twalk->depends->laststart > (now - 5)))) {
					dbgprintf("Postponing start of %s due to %s not yet running\n",
						twalk->key, twalk->depends->key);
					continue;
				}

				if (twalk->group && (twalk->group->currentuse >= twalk->group->maxuse)) {
					dbgprintf("Postponing start of %s due to group %s being busy\n",
						twalk->key, twalk->group->groupname);
					continue;
				}

				if ((twalk->failcount > MAX_FAILS) && ((twalk->laststart + 600) < now)) {
					dbgprintf("Releasing %s from failure hold\n", twalk->key);
					twalk->failcount = 0;
				}

				if (twalk->failcount > MAX_FAILS) {
					dbgprintf("Postponing start of %s due to multiple failures\n", twalk->key);
					continue;
				}

				if (twalk->laststart > (now - 5)) {
					dbgprintf("Postponing start of %s, will not try more than once in 5 seconds\n", twalk->key);
					continue;
				}

				dbgprintf("About to start task %s\n", twalk->key);

				twalk->laststart = now;
				twalk->pid = fork();
				if (twalk->pid == 0) {
					/* Exec the task */
					char *cmd;
					char **cmdargs = NULL;
					static char bbsleepenv[20];

					/* Setup environment */
					if (twalk->envfile) {
						dbgprintf("%s -> Loading environment from %s area %s\n", 
							twalk->key, expand_env(twalk->envfile), 
							(twalk->envarea ? twalk->envarea : ""));
						loadenv(expand_env(twalk->envfile), twalk->envarea);
					}

					/* Setup BBSLEEP to match the interval */
					sprintf(bbsleepenv, "BBSLEEP=%d", twalk->interval);
					putenv(bbsleepenv);

					/* Setup command line and arguments */
					cmdargs = setup_commandargs(twalk->cmd, &cmd);

					/* Point stdout/stderr to a logfile, if requested */
					if (twalk->logfile) {
						char *logfn = expand_env(twalk->logfile);

						dbgprintf("%s -> Assigning stdout/stderr to log '%s'\n", twalk->key, logfn);

						freopen(logfn, "a", stdout);
						freopen(logfn, "a", stderr);
					}

					/* Go! */
					dbgprintf("%s -> Running '%s', BBHOME=%s\n", twalk->key, cmd, xgetenv("BBHOME"));
					execvp(cmd, cmdargs);

					/* Should never go here */
					errprintf("Could not start task %s using command '%s': %s\n", 
						   twalk->key, cmd, strerror(errno));
					exit(0);
				}
				else if (twalk->pid == -1) {
					/* Fork failed */
					errprintf("Fork failed!\n");
					twalk->pid = 0;
				}
				else {
					if (twalk->group) twalk->group->currentuse++;
					if (verbose) errprintf("Task %s started with PID %d\n", twalk->key, (int)twalk->pid);
				}
			}
			else if (twalk->pid > 0) {
				dbgprintf("Task %s active with PID %d\n", twalk->key, (int)twalk->pid);
			}
		}

		sleep(5);
	}

	/* Shutdown running tasks */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		if (twalk->pid) kill(twalk->pid, SIGTERM);
	}

	if (pidfn) unlink(pidfn);

	return 0;
}

