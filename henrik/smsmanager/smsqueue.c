/*----------------------------------------------------------------------------*/
/* SMS message queue processor                                                */
/*                                                                            */
/* Copyright (C) 2009 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: smsqueue.c,v 1.3 2009/06/30 14:36:51 henrik Exp henrik $";

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <utime.h>
#include <dirent.h>
#include <signal.h>

#include "libxymon.h"

int running = 1;

void do_xmit(char *sendcmd, char *adfn, char *sender, char *xmitfn)
{
	time_t now = getcurrenttime(NULL);
	FILE *logfd, *msgfd, *recipfd, *reciplistfd, *cmdfd;
	char logfn[PATH_MAX];
	char recipfn[PATH_MAX];
	char reciplistfn[PATH_MAX];
	char msgfn[PATH_MAX];
	char *msgbuf;
	struct stat st;
	struct utimbuf ut;
	char recip[1024], *recipient, cmd[32768];
	strbuffer_t *recipstrbuf;

	sprintf(xmitfn, "%s/lastxmit", adfn);
	ut.actime = ut.modtime = now;
	if (utime(xmitfn, &ut) != 0) errprintf("Cannot update timestamp %s : %s\n", xmitfn, strerror(errno));

	sprintf(msgfn, "%s/message", adfn);
	if (stat(msgfn, &st) != 0) return;
	msgfd = fopen(msgfn, "r"); if (!msgfd) return;
	msgbuf = (char *)malloc(st.st_size+1);
	fread(msgbuf, st.st_size, 1, msgfd);
	msgbuf[st.st_size] = '\0';
	fclose(msgfd);

	sprintf(recipfn, "%s/recips", adfn);
	recipfd = fopen(recipfn, "r");
	if (!recipfd) { free(msgbuf); return; }
	if (fgets(recip, sizeof(recip), recipfd) == NULL) { fclose(recipfd); free(msgbuf); return; }
	fclose(recipfd);

	sprintf(reciplistfn, "%s/recips/%s", sender, recip);
	reciplistfd = stackfopen(reciplistfn, "r", NULL);
	if (!reciplistfd) { free(msgbuf); return; }

	sprintf(logfn, "%s/log", adfn);
	logfd = fopen(logfn, "a");
	init_timestamp();

	recipstrbuf = newstrbuffer(0);
	sprintf(cmd, "%s %s", sendcmd, sender);
	while (stackfgets(recipstrbuf, "%") != NULL) {
		char *p;

		recipient = STRBUF(recipstrbuf);
		p = recipient + (strcspn(recipient, " \t\n")); *p = '\0';
		if (*recipient == '\0') continue;

		sprintf(cmd + strlen(cmd), " %s", recipient);
	}
	strcat(cmd, " 2>/dev/null 1>/dev/null");
	freestrbuffer(recipstrbuf);
	stackfclose(reciplistfd);

	dbgprintf("Will run command: %s\n", cmd);
	cmdfd = popen(cmd, "w"); 
	if (!cmdfd) {
		if (logfd) fprintf(logfd, "%s : Could not send message to %s : %s\n", timestamp, recip, strerror(errno));
	}
	else {
		int n;

		fwrite(msgbuf, strlen(msgbuf), 1, cmdfd);
		n = pclose(cmdfd);
		if (n == 0) {
			if (logfd) fprintf(logfd, "%s : Sent message to %s: '%s'\n", timestamp, recip, msgbuf);
		}
		else {
			if (logfd) fprintf(logfd, "%s : Could not send message to %s : Errorcode %d\n", timestamp, recip, WEXITSTATUS(n));
		}
	}

	if (logfd) fclose(logfd);
	free(msgbuf);
}


void sig_handler(int signum)
{
	switch (signum) {
	  case SIGCHLD:
		break;

	  case SIGTERM:
	  case SIGINT:
		running = 0;
		break;
	}
}


int main(int argc, char *argv[])
{
	int argi;
	struct sigaction sa;
	char *envarea = NULL;
	char *topdirectory = "/var/spool/smsgui";
	char *sendcmd = "/usr/local/bin/handleguisms";

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--topdir=")) {
			char *p = strchr(argv[argi], '=');
			topdirectory = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--command=")) {
			char *p = strchr(argv[argi], '=');
			sendcmd = strdup(p+1);
		}

	}

	setup_signalhandler("smsqueue");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	if (chdir(topdirectory) != 0) {
		errprintf("Cannot access top-level directory %s : %s", topdirectory, strerror(errno));
		return 0;
	}

	while (running) {
		DIR *userdir, *activedir;
		struct dirent *udent, *adent;

		userdir = opendir(".");
		if (!userdir) {
			errprintf("Cannot scan toplevel directory %s for users: %s\n", topdirectory, strerror(errno));
			return 0;
		}

		while ((udent = readdir(userdir)) != NULL) {
			char actdn[PATH_MAX];
			time_t now = getcurrenttime(NULL);

			sprintf(actdn, "%s/active", udent->d_name);
			activedir = opendir(actdn);
			if (!activedir) continue;

			dbgprintf("Scanning active directory for user %s\n", udent->d_name);

			while ((adent = readdir(activedir)) != NULL) {
				char adfn[PATH_MAX];
				struct stat st;
				char fn[PATH_MAX], *xmitfn;
				FILE *fd;
				char l[1024];
				int lastxmit, repeattime;

				if (strncmp(adent->d_name, "sms", 3) != 0) continue;
				sprintf(adfn, "%s/%s", actdn, adent->d_name);
				if (stat(adfn, &st) != 0) continue;
				if (!S_ISDIR(st.st_mode)) continue;

				sprintf(fn, "%s/endtime", adfn);
				if (stat(fn, &st) != 0) {
					dbgprintf("stat() of %s failed: %s\n", fn, strerror(errno));
				}
				else if (now > st.st_mtime) {
					/* Message must die - it is overdue */
					char oldfn[PATH_MAX];

					dbgprintf("Message %s is too old, stopping it\n", adfn);

					init_timestamp();
					sprintf(fn, "%s/log", adfn);
					fd = fopen(fn, "a");
					if (fd) {
						fprintf(fd, "%s : Message auto-deleted (expired)\n", timestamp);
						fclose(fd);
					}
					sprintf(oldfn, "%s/old/%s",  udent->d_name, adent->d_name);
					rename(adfn, oldfn);
					continue;
				}

				dbgprintf("Checking repeat for %s\n", adfn);

				sprintf(fn, "%s/lastxmit", adfn);
				xmitfn = strdup(fn);
				if (stat(xmitfn, &st) != 0) {
					dbgprintf("stat() of %s failed: %s\n", xmitfn, strerror(errno));
					continue;
				}
				lastxmit = st.st_mtime;

				sprintf(fn, "%s/repeat", adfn);
				if ((fd = fopen(fn, "r")) == NULL) {
					dbgprintf("fopen() of %s failed: %s\n", fn, strerror(errno));
					continue;
				}
				if (fgets(l, sizeof(l), fd) == NULL) {
					dbgprintf("fgets() of %s failed: %s\n", fn, strerror(errno));
					fclose(fd);
					continue;
				} 
				if (fd) fclose(fd);

				repeattime = atoi(l);
				if (repeattime == -2) {
					/* Non-repeat message, move it to the completed list */
					char olddir[PATH_MAX], olddn[PATH_MAX];
					struct stat st;

					dbgprintf("One-shot message, sending and setting it to completed\n");
					do_xmit(sendcmd, adfn, udent->d_name, xmitfn);

					sprintf(olddir, "%s/old", udent->d_name);
					if ((stat(olddir, &st) == -1) && (errno == ENOENT)) mkdir (olddir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

					init_timestamp();
					snprintf(fn, sizeof(fn), "%s/log", adfn);
					fd = fopen(fn, "a");
					if (fd) {
						fprintf(fd, "%s : Message auto-deleted (no repeat)\n", timestamp);
						fclose(fd);
					}

					snprintf(olddn, sizeof(olddn), "%s/old/%s", udent->d_name, adent->d_name);
					rename(adfn, olddn);
					goto cleanup;
				}
				else if (repeattime <= 0) {
					dbgprintf("Entry %s has repeat <= 0, i.e. it is suspended\n", adfn);
					goto cleanup;
				}
				else {
					repeattime = 60*atoi(l);
					dbgprintf("Entry %s has lastxmit=%d, repeat=%d, next=%d, now=%d\n",
						  adfn, lastxmit, repeattime, (lastxmit+repeattime), now);
				}

				if ((lastxmit + repeattime) > now) {
					dbgprintf("Skipping %s, repeating in %d seconds\n", adfn, ((lastxmit + repeattime) - now));
					goto cleanup;
				}

				dbgprintf("Processing %s\n", adfn);
				do_xmit(sendcmd, adfn, udent->d_name, xmitfn);

cleanup:
				free(xmitfn);
			}
			closedir(activedir);
		}
		closedir(userdir);

		sleep(30);
	}

	return 0;
}

