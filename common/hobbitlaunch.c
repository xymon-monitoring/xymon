/*----------------------------------------------------------------------------*/
/* Big Brother application launcher.                                          */
/*                                                                            */
/* This is used to launch various parts of the BB system. Some programs start */
/* up once and keep running, other must run at various intervals.             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitlaunch.c,v 1.6 2004-11-17 16:24:28 henrik Exp $";

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

#include "libbbgen.h"

/*
 * config file format:
 *
 * [bbgend]
 * 	CMD bbgend --no-daemon
 * 	LOGFILE /var/log/bbgend.log
 *
 * [bbdisplay]
 * 	CMD bb-display.sh
 * 	INTERVAL 5m
 */

#define MAX_FAILS 5

typedef struct tasklist_t {
	char *key;
	char *cmd;
	int interval;
	char *logfile;
	char *envfile;
	pid_t pid;
	time_t laststart;
	int exitcode;
	int failcount;
	int cfload;	/* Used while reloading a configuration */
	struct tasklist_t *depends;
	struct tasklist_t *next;
} tasklist_t;
tasklist_t *taskhead = NULL;
tasklist_t *tasktail = NULL;

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
		free(twalk->cmd); 
		if (twalk->logfile) free(twalk->logfile);
		if (twalk->envfile) free(twalk->envfile);
		twalk->cmd = strdup(newtask->cmd);
		if (newtask->logfile) twalk->logfile = strdup(newtask->logfile); else twalk->logfile = NULL;
		if (newtask->envfile) twalk->envfile = strdup(newtask->envfile); else twalk->envfile = NULL;

		/* Must bounce the task */
		twalk->cfload = 1;
	}
	else if (twalk->interval != newtask->interval) {
		twalk->interval = newtask->interval;
		twalk->cfload = 0;
	}
	else {
		/* Task was unchanged */
		twalk->cfload = 0;
	}

	if (freeit) {
		free(newtask->key);
		if (newtask->cmd) free(newtask->cmd);
		if (newtask->logfile) free(newtask->logfile);
		if (newtask->envfile) free(newtask->envfile);
		free(newtask);
	}
}

void load_config(char *conffn)
{
	static time_t cfgtstamp = 0;

	struct stat st;
	tasklist_t *twalk, *curtask = NULL;
	char l[32768];
	FILE *fd;
	char *p;

	/* Check the timestamp of the configuration file */
	if (stat(conffn, &st) == -1) {
		errprintf("Cannot access configuration file %s\n", conffn);
		return;
	}
	if (st.st_mtime == cfgtstamp) return; /* No change */
	cfgtstamp = st.st_mtime;

	/* The cfload flag: -1=delete task, 0=old task unchanged, 1=new/changed task */
	for (twalk = taskhead; (twalk); twalk = twalk->next) twalk->cfload = -1;

	fd = fopen(conffn, "r");
	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); if (p) *p = '\0';

		p = l + strspn(l, " \t");
		if ((*p == '\0') || (*p == '#')) {
			/* Comment or blank line - ignore */
		}
		else if (*p == '[') {
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
		else if (curtask && (strncasecmp(p, "ENVFILE ", 4) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			curtask->envfile = strdup(p);
		}
	}
	if (curtask) update_task(curtask);
	fclose(fd);

	/* Running tasks that have been deleted or changed are killed off now. */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		switch (twalk->cfload) {
		  case -1:
			/* Kill the task, if active */
			if (twalk->pid) {
				dprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
				kill(twalk->pid, SIGTERM);
			}
			/* And prepare to free this tasklist entry */
			free(twalk->key); 
			free(twalk->cmd); 
			if (twalk->logfile) free(twalk->logfile);
			if (twalk->envfile) free(twalk->envfile);
			break;

		  case 0:
			/* Do nothing */
			break;

		  case 1:
			/* Bounce the task, if it is active */
			if (twalk->pid) {
				dprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
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
		free(tmp);
	}

	/* Then unlink and free those inside the list */
	twalk = taskhead;
	while (twalk && twalk->next) {
		tasklist_t *tmp;

		if (twalk->next->cfload == -1) {
			tmp = twalk->next;
			twalk->next = tmp->next;
			free(tmp);
		}
		else twalk = twalk->next;
	}

	if (taskhead == NULL) 
		tasktail = NULL;
	else {
		tasktail = taskhead;
		while (tasktail->next) tasktail = tasktail->next;
	}

	/* Dump configuration */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		dprintf("[%s]\n", twalk->key);
		dprintf("\tCMD %s\n", twalk->cmd);
		if (twalk->depends) dprintf("\tNEEDS %s\n", twalk->depends->key);
		if (twalk->interval) dprintf("\tINTERVAL %d\n", twalk->interval);
		if (twalk->logfile) dprintf("\tLOGFILE %s\n", twalk->logfile);
		if (twalk->envfile) dprintf("\tENVFILE %s\n", twalk->envfile);
		dprintf("\n");
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
	tasklist_t *twalk;
	int argi;
	int daemonize = 1;
	char *config = "/etc/bbtasks.cfg";
	char *logfn = NULL;
	pid_t cpid;
	int status;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			config = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');
			logfn = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--dump") == 0) {
			/* Dump configuration */

			load_config(config);
			for (twalk = taskhead; (twalk); twalk = twalk->next) {
				printf("[%s]\n", twalk->key);
				printf("\tCMD %s\n", twalk->cmd);
				if (twalk->depends)  printf("\tNEEDS %s\n", twalk->depends->key);
				if (twalk->interval) printf("\tINTERVAL %d\n", twalk->interval);
				if (twalk->logfile)  printf("\tLOGFILE %s\n", twalk->logfile);
				if (twalk->envfile)  printf("\tENVFILE %s\n", twalk->envfile);
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
	setup_signalhandler("bblaunch");
	signal(SIGCHLD, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGTERM, sig_handler);

	while (running) {
		time_t now = time(NULL);

		if (now >= nextcfgload) {
			dprintf("Loading configuration file\n");
			load_config(config);
			nextcfgload = (now + 30);
		}

		if (logfn && dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			dologswitch = 0;
		}

		/* Pick up children that have terminated */
		while ((cpid = wait4(0, &status, WNOHANG, NULL)) > 0) {
			for (twalk = taskhead; (twalk && (twalk->pid != cpid)); twalk = twalk->next);
			if (twalk) {
				twalk->pid = 0;
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
				}

				/* Tasks that depend on this task should be killed ... */
			}
		}

		/* See what new tasks need to get going */
		dprintf("\n");
		dprintf("Starting tasklist scan\n");
		for (twalk = taskhead; (twalk); twalk = twalk->next) {
			if ((twalk->pid == 0) && (now >= (twalk->laststart + twalk->interval))) {

				if (twalk->depends && (twalk->depends->pid == 0)) {
					dprintf("Postponing start of %s due to %s not yet running\n",
						twalk->key, twalk->depends->key);
					continue;
				}

				if ((twalk->failcount > MAX_FAILS) && ((twalk->laststart + 600) < now)) {
					dprintf("Releasing %s from failure hold\n", twalk->key);
					twalk->failcount = 0;
				}

				if (twalk->failcount > MAX_FAILS) {
					dprintf("Postponing start of %s due to multiple failures\n", twalk->key);
					continue;
				}

				dprintf("About to start task %s\n", twalk->key);

				twalk->laststart = now;
				twalk->pid = fork();
				if (twalk->pid == 0) {
					/* Exec the task */
					char *cmd;
					char **cmdargs = NULL;
					int argcount = 0;
					char *cmdcp, *p;

					/* Setup environment */
					if (twalk->envfile) loadenv(twalk->envfile);

					/* Count # of arguments in command */
					cmdcp = strdup(twalk->cmd);
					p = strtok(cmdcp, " ");
					while (p) { argcount++; p = strtok(NULL, " "); }
					cmdargs = (char **) calloc(argcount+2, sizeof(char *));

					/* Setup cmd and cmdargs */
					strcpy(cmdcp, twalk->cmd);
					cmd = strtok(cmdcp, " ");
					cmdargs[0] = cmd; argcount = 0;
					while ((p = strtok(NULL, " ")) != NULL) cmdargs[++argcount] = p;
					
					/* Point stdout/stderr to a logfile, if requested */
					if (twalk->logfile) {
						int fd = open(twalk->logfile, O_WRONLY|O_CREAT|O_APPEND,
								S_IRUSR|S_IWUSR|S_IRGRP);
						if (fd) { 
							dup2(fd, STDOUT_FILENO); 
							dup2(fd, STDERR_FILENO); 
							close(fd);
						}
					}

					/* Go! */
					execvp(cmd, cmdargs);

					/* Should never go here */
					errprintf("execvp() failed: %s\n", strerror(errno));
					exit(0);
				}
				else if (twalk->pid == -1) {
					/* Fork failed */
					errprintf("Fork failed!\n");
					twalk->pid = 0;
				}
			}
			else if (twalk->pid > 0) {
				dprintf("Task %s active with PID %d\n", twalk->key, (int)twalk->pid);
			}
		}

		sleep(5);
	}

	/* Shutdown running tasks */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		if (twalk->pid) kill(twalk->pid, SIGTERM);
	}

	return 0;
}

