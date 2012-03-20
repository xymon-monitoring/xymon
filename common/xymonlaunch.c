/* -*- mode:C; tab-width:8; intent-tabs-mode:1; c-basic-offset:8  -*- */
/*----------------------------------------------------------------------------*/
/* Xymon application launcher.                                                */
/*                                                                            */
/* This is used to launch various parts of the Xymon system. Some programs    */
/* start up once and keep running, other must run at various intervals.       */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/* "CMD +/-" code and enable/disable enhancement by Martin Sperl 2010-2011    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <regex.h>

#include "libxymon.h"

#include <signal.h>

/*
 * config file format:
 *
 * [xymond]
 * 	CMD xymond --no-daemon
 * 	LOGFILE /var/log/xymond.log
 *
 * [xymongen]
 * 	CMD xymongen
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
	int interval, maxruntime;
	char *logfile;
	char *envfile, *envarea, *onhostptn;
	pid_t pid;
	time_t laststart;
	int exitcode;
	int failcount;
	int cfload;	/* Used while reloading a configuration */
	int beingkilled;
	char *cronstr; /* pointer to cron string */
	void *crondate; /* pointer to cron date-time structure */
	struct tasklist_t *depends;
	struct tasklist_t *next;
	struct tasklist_t *copy;
} tasklist_t;
tasklist_t *taskhead = NULL;
tasklist_t *tasktail = NULL;
grouplist_t *grouphead = NULL;

volatile time_t nextcfgload = 0;
volatile int running = 1;
volatile int dologswitch = 0;
volatile int forcereload=0;

# define xfreenull(k) { if (k) { xfree(k); k=NULL;} }
# define xfreeassign(k,p) { if (k) { xfree(k); } k=p; }
# define xfreedup(k,p) { if (k) { xfree(k); } k=strdup(p); }

void load_config(char *conffn)
{
	static void *configfiles = NULL;
	tasklist_t *twalk, *curtask = NULL;
	FILE *fd;
	strbuffer_t *inbuf;
	char *p;
	char myhostname[256];

	/* First check if there were no modifications at all */
	if (configfiles) {
	        if (!stackfmodified(configfiles) && (!forcereload)) {
			dbgprintf("No files modified, skipping reload of %s\n", conffn);
			return;
		}
		else {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
	}

	errprintf("Loading tasklist configuration from %s\n", conffn);
	if (gethostname(myhostname, sizeof(myhostname)) != 0) {
		errprintf("Cannot get the local hostname, using 'localhost' (error: %s)\n", strerror(errno));
		strcpy(myhostname, "localhost");
	}

	/* The cfload flag: -1=delete task, 0=old task unchanged, 1=new/changed task */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		twalk->cfload = -1;
		twalk->group = NULL;
		/* Create a copy, but retain the settings and pointers are the same */
		twalk->copy = xmalloc(sizeof(tasklist_t));
		memcpy(twalk->copy,twalk,sizeof(tasklist_t));
		/* These should get cleared */
		twalk->copy->next = NULL;
		twalk->copy->copy = NULL;
		/* And clean the values of all others, so that we really can detect a difference */
		twalk->disabled = 0;
		twalk->cmd = NULL;
		twalk->interval = 0;
		twalk->maxruntime = 0;
		twalk->group = NULL;
		twalk->logfile = NULL;
		twalk->envfile = NULL;
		twalk->envarea = NULL;
		twalk->onhostptn = NULL;
		twalk->cronstr = NULL;
		twalk->crondate = NULL;
		twalk->depends = NULL;
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
			/* get name */
			p++; endp = strchr(p, ']');
			if (endp == NULL) continue;
			*endp = '\0';

			/* try to find the task */
			for (twalk = taskhead; (twalk && (strcmp(twalk->key, p))); twalk = twalk->next);

			if (twalk) {
				curtask=twalk;
			} else {
				/* New task, just create it */
				curtask = (tasklist_t *)calloc(1, sizeof(tasklist_t));
				curtask->key = strdup(p);
				/* add it to the list */
				if (taskhead == NULL) taskhead = curtask;
				else tasktail->next = curtask;
				tasktail = curtask;
			}
			/* mark task as configured */
			curtask->cfload = 0;
		}
		else if (curtask && (strncasecmp(p, "CMD ", 4) == 0)) {
			p += 3;
			p += strspn(p, " \t");
			/* Handle + - options as well */
			if (*p == '+') {
				/* append to command */
				if (curtask->cmd) {
					int l1 = strlen(curtask->cmd);
					int l2 = strlen(p);
					char *newcmd = xcalloc(1, l1+l2+1);

					strncpy(newcmd,curtask->cmd,l1);
					strncpy(newcmd+l1,p,l2);
					newcmd[l1]=' '; /* this also overwrites the + */

					/* free and assign new */
					xfreeassign(curtask->cmd,newcmd);
				}
			} 
			else if (*p == '-') {
				/* remove from command */
				if (curtask->cmd) {
					int l = strlen(p)-1;
					if (l > 0) {
						char *found;

						while((found = strstr(curtask->cmd,p+1)) != NULL) {
							/* doing a copy - can not use strcpy as we are overlapping */
							char *s = found + l;

							while (*s) {
								*found=*s; 
								found++;
								s++;
							}

							*found=0;
						}
					} 
					else {
						errprintf("Configuration error, empty command removal (CMD -) for task %s\n", curtask->key);
					}
				}
			} else {
				xfreedup(curtask->cmd,p);
			}
		}
		else if (strncasecmp(p, "GROUP ", 6) == 0) {
			/* Note: GROUP can be used by itself to define a group, or inside a task definition */
			char *groupname;
			int maxuse;
			grouplist_t *gwalk;

			p += 6;
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
		else if (curtask && (strncasecmp(p, "CRONDATE ", 9) == 0)) {
			p+= 9;
			xfreedup(curtask->cronstr,p);
			if (curtask->crondate) crondatefree(curtask->crondate);
			curtask->crondate = parse_cron_time(curtask->cronstr);
			if (!curtask->crondate) {
				errprintf("Can't parse cron date: %s->%s\n", curtask->key, curtask->cronstr);
				curtask->disabled = 1;
			}
			curtask->interval = -1; /* disable interval */
		}
		else if (curtask && (strncasecmp(p, "MAXTIME ", 8) == 0)) {
			char *tspec;
			p += 8;
			curtask->maxruntime = atoi(p);
			tspec = p + strspn(p, "0123456789");
			switch (*tspec) {
			  case 'm': curtask->maxruntime *= 60; break;	/* Minutes */
			  case 'h': curtask->maxruntime *= 3600; break;	/* Hours */
			  case 'd': curtask->maxruntime *= 86400; break;	/* Days */
			}
		}
		else if (curtask && (strncasecmp(p, "LOGFILE ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			xfreedup(curtask->logfile,p);
		}
		else if (curtask && (strncasecmp(p, "NEEDS ", 6) == 0)) {
			p += 6;
			p += strspn(p, " \t");
			for (twalk = taskhead; (twalk && strcmp(twalk->key, p)); twalk = twalk->next);
			if (twalk) {
				curtask->depends = twalk;
			}
			else {
				errprintf("Configuration error, unknown dependency %s->%s\n", curtask->key, p);
			}
		}
		else if (curtask && (strncasecmp(p, "ENVFILE ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			xfreedup(curtask->envfile,p);
		}
		else if (curtask && (strncasecmp(p, "ENVAREA ", 8) == 0)) {
			p += 7;
			p += strspn(p, " \t");
			xfreedup(curtask->envarea,p);
		}
		else if (curtask && (strcasecmp(p, "DISABLED") == 0)) {
			curtask->disabled = 1;
		}
		else if (curtask && (strcasecmp(p, "ENABLED") == 0)) {
			curtask->disabled = 0;
		}
		else if (curtask && (strncasecmp(p, "ONHOST ", 7) == 0)) {
			regex_t cpattern;
			int status;

			p += 7;
			p += strspn(p, " \t");

			xfreedup(curtask->onhostptn,p);

			/* Match the hostname against the pattern; if it doesnt match then disable the task */
			status = regcomp(&cpattern, curtask->onhostptn, REG_EXTENDED|REG_ICASE|REG_NOSUB);
			if (status == 0) {
				status = regexec(&cpattern, myhostname, 0, NULL, 0);
				if (status == REG_NOMATCH) curtask->disabled = 1;
			}
			else {
				errprintf("ONHOST pattern '%s' is invalid\n", p);
			}
		}
	}
	stackfclose(fd);
	freestrbuffer(inbuf);

	/* Running tasks that have been deleted or changed are killed off now. */
	for (twalk = taskhead; (twalk); twalk = twalk->next) {
		/* compare the current settings with the copy - if we have one */
		if (twalk->cfload == 0) {
			if (twalk->copy) {
				/* compare the current version with the new version and decide if we have changed */
				int changed=0;
				int reload=0;
				/* first the nummeric ones */
				if (twalk->disabled!=twalk->copy->disabled) { changed++; }
				if (twalk->interval!=twalk->copy->interval) { changed++; }
				if (twalk->maxruntime!=twalk->copy->maxruntime) { changed++; }
				if (twalk->group!=twalk->copy->group) { changed++; reload++;}
				/* then the string versions */
#define twalkstrcmp(k,doreload) {					\
					if (twalk->k!=twalk->copy->k) {	\
						if (twalk->copy->k) {	\
							if (twalk->k) {	\
								if (strcmp(twalk->k,twalk->copy->k)) { \
									changed++;reload+=doreload; \
								}	\
							} else {	\
								changed++;reload+=doreload; \
							}		\
							/* we can always delete the copy*/ \
							xfree(twalk->copy->k); \
							twalk->copy->k=NULL; \
						} else {		\
							changed++;reload+=doreload; \
						}			\
					}				\
				}
				twalkstrcmp(cmd,1);
				twalkstrcmp(logfile,1);
				twalkstrcmp(envfile,1);
				twalkstrcmp(envarea,1);
				twalkstrcmp(onhostptn,0);
				twalkstrcmp(cronstr,0);
				if ((twalk->copy->cronstr == NULL) && twalk->copy->crondate) {
					crondatefree(twalk->copy->crondate);
					twalk->copy->crondate = NULL;
				}
				
				/* we can release the copy now - not using xfree, as this releases it from the list making a mess...*/
				xfreenull(twalk->copy);
				/* now make the decision for reloading 
				   - if we have changed, then we may assign cfload,
				   - otherwise the entry does not exist any longer */
				if (reload) { reload=1;}
				if (changed) { twalk->cfload=reload; }
			} else {
				/* new object, so we need to do this */
				twalk->cfload=1;
			}
		}

		/* and based on this decide what to do */
		switch (twalk->cfload) {
		  case -1:
			/* Kill the task, if active */
			if (twalk->pid) {
				dbgprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
				twalk->beingkilled = 1;
				kill(twalk->pid, SIGTERM);
			}
			/* And prepare to free this tasklist entry */
			xfreenull(twalk->key); 
			xfreenull(twalk->cmd); 
			xfreenull(twalk->logfile);
			xfreenull(twalk->envfile);
			xfreenull(twalk->envarea);
			xfreenull(twalk->onhostptn);
			xfreenull(twalk->cronstr);
			if (twalk->crondate) crondatefree(twalk->crondate);
			break;

		  case 0:
			/* Do nothing */
			break;

		  case 1:
			/* Bounce the task, if it is active */
			if (twalk->pid) {
				dbgprintf("Killing task %s PID %d\n", twalk->key, (int)twalk->pid);
				twalk->beingkilled = 1;
				kill(twalk->pid, SIGTERM);
			}
			break;
		}
	}

	/* First clean out dead tasks at the start of the list */
	while (taskhead && (taskhead->cfload == -1)) {
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
	char *config = "/etc/tasks.cfg";
	pid_t cpid;
	int status;
	struct sigaction sa;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else if (strcmp(argv[argi], "--verbose") == 0) {
			verbose = 1;
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			config = strdup(expand_env(p+1));
		}
		else if (strcmp(argv[argi], "--dump") == 0) {
			/* Dump configuration */
			forcereload=1;
			load_config(config);
			forcereload=0;
			for (gwalk = grouphead; (gwalk); gwalk = gwalk->next) {
				if (gwalk->maxuse > 1) printf("GROUP %s %d\n", gwalk->groupname, gwalk->maxuse);
			}
			printf("\n");
			for (twalk = taskhead; (twalk); twalk = twalk->next) {
				printf("[%s]\n", twalk->key);
				printf("\tCMD %s\n", twalk->cmd);
				if (twalk->disabled)     printf("\tDISABLED\n");
				if (twalk->group)        printf("\tGROUP %s\n", twalk->group->groupname);
				if (twalk->depends)      printf("\tNEEDS %s\n", twalk->depends->key);
				if (twalk->interval > 0) printf("\tINTERVAL %d\n", twalk->interval);
				if (twalk->cronstr)      printf("\tCRONDATE %s\n", twalk->cronstr);
				if (twalk->maxruntime)   printf("\tMAXTIME %d\n", twalk->maxruntime);
				if (twalk->logfile)      printf("\tLOGFILE %s\n", twalk->logfile);
				if (twalk->envfile)      printf("\tENVFILE %s\n", twalk->envfile);
				if (twalk->envarea)      printf("\tENVAREA %s\n", twalk->envarea);
				if (twalk->onhostptn)    printf("\tONHOST %s\n", twalk->onhostptn);
				printf("\n");
			}
			fflush(stdout);
			return 0;
		}
		else if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
		else {
			fprintf(stderr,"%s: Unsupported argument: %s\n",argv[0],argv[argi]);
			fflush(stderr);
			return 1;
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
	setup_signalhandler("xymonlaunch");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	errprintf("xymonlaunch starting\n");
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
				}

				if (twalk->group) twalk->group->currentuse--;

				/* Tasks that depend on this task should be killed ... */
				for (dwalk = taskhead; (dwalk); dwalk = dwalk->next) {
					if ((dwalk->depends == twalk) && (dwalk->pid > 0)) {
						dwalk->beingkilled = 1;
						kill(dwalk->pid, SIGTERM);
					}
				}
			}
		}

		/* See what new tasks need to get going */
		dbgprintf("\n");
		dbgprintf("Starting tasklist scan\n");
		crongettime();
		for (twalk = taskhead; (twalk); twalk = twalk->next) {
			if ( (twalk->pid == 0) && !twalk->disabled && 
			       ( ((twalk->interval >= 0) && (now >= (twalk->laststart + twalk->interval))) || /* xymon interval condition */
			         (twalk->crondate && ((twalk->laststart + 55) < now) && cronmatch(twalk->crondate)) /* cron date */
			       ) 
			   ) {
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
					static char tasksleepenv[20],bbsleepenv[20];

					/* Setup environment */
					if (twalk->envfile) {
						dbgprintf("%s -> Loading environment from %s area %s\n", 
							twalk->key, expand_env(twalk->envfile), 
							(twalk->envarea ? twalk->envarea : ""));
						loadenv(expand_env(twalk->envfile), twalk->envarea);
					}

					/* Setup TASKSLEEP to match the interval */
					sprintf(tasksleepenv, "TASKSLEEP=%d", twalk->interval);
					sprintf(bbsleepenv, "BBSLEEP=%d", twalk->interval);	/* For compatibility */
					putenv(tasksleepenv); putenv(bbsleepenv);

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
					dbgprintf("%s -> Running '%s', XYMONHOME=%s\n", twalk->key, cmd, xgetenv("XYMONHOME"));
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
				if (twalk->maxruntime && ((now - twalk->laststart) > twalk->maxruntime)) {
					errprintf("Killing hung task %s (PID %d) after %d seconds\n",
						  twalk->key, (int)twalk->pid,
						  (now - twalk->laststart));
					kill(twalk->pid, (twalk->beingkilled ? SIGKILL : SIGTERM));
					twalk->beingkilled = 1; /* Next time it's a real kill */
				}
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

