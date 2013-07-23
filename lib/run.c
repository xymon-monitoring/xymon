/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains miscellaneous routines.                                        */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/statvfs.h>

#include "libxymon.h"

int run_command(char *cmd, char *errortext, strbuffer_t *banner, int showcmd, int timeout)
{
	int	result;
	char	l[1024];
	int	pfd[2];
	pid_t	childpid; 

	MEMDEFINE(l);

	result = 0;
	if (banner && showcmd) { 
		sprintf(l, "Command: %s\n\n", cmd); 
		addtobuffer(banner, l);
	}

	/* Adapted from Stevens' popen()/pclose() example */
	if (pipe(pfd) > 0) {
		errprintf("Could not create pipe: %s\n", strerror(errno));
		MEMUNDEFINE(l);
		return -1;
	}

	if ((childpid = fork()) < 0) {
		errprintf("Could not fork child process: %s\n", strerror(errno));
		MEMUNDEFINE(l);
		return -1;
	}

	if (childpid == 0) {
		/* The child runs here */
		close(pfd[0]);
		if (pfd[1] != STDOUT_FILENO) {
			dup2(pfd[1], STDOUT_FILENO);
			dup2(pfd[1], STDERR_FILENO);
			close(pfd[1]);
		}

		if (strchr(cmd, ' ') == NULL) execlp(cmd, cmd, NULL);
		else {
			char *shell = getenv("SHELL");
			if (!shell) shell = "/bin/sh";
			execl(shell, "sh", "-c", cmd, NULL);
		}
		exit(127);
	}
	else {
		/* The parent runs here */
		int done = 0, didterm = 0, n;
		struct timespec tmo, timestamp, cutoff;

		close(pfd[1]);

		/* Make our reads non-blocking */
		if (fcntl(pfd[0], F_SETFL, O_NONBLOCK) == -1) {
			/* Failed .. but lets try and run this anyway */
			errprintf("Could not set non-blocking reads on pipe: %s\n", strerror(errno));
		}

		getntimer(&cutoff);
		cutoff.tv_sec += timeout;

		while (!done) {
			fd_set readfds;

			getntimer(&timestamp);
			tvdiff(&timestamp, &cutoff, &tmo);
			if ((tmo.tv_sec < 0) || (tmo.tv_nsec < 0)) {
				/* Timeout already happened */
				n = 0;
			}
			else {
				struct timeval selecttmo;

				selecttmo.tv_sec = tmo.tv_sec;
				selecttmo.tv_usec = tmo.tv_nsec / 1000;
				FD_ZERO(&readfds);
				FD_SET(pfd[0], &readfds);
				n = select(pfd[0]+1, &readfds, NULL, NULL, &selecttmo);
			}

			if (n == -1) {
				errprintf("select() error: %s\n", strerror(errno));
				result = -1;
				done = 1;
			}
			else if (n == 0) {
				/* Timeout */
				errprintf("Timeout waiting for data from child, killing it\n");
				kill(childpid, (didterm ? SIGKILL : SIGTERM));
				if (!didterm) didterm = 1; else { done = 1; result = -1; }
			}
			else if (FD_ISSET(pfd[0], &readfds)) {
				n = read(pfd[0], l, sizeof(l)-1);
				l[n] = '\0';

				if (n == 0) {
					done = 1;
				}
				else {
					if (banner && *l) addtobuffer(banner, l);
					if (errortext && (strstr(l, errortext) != NULL)) result = 1;
				}
			}
		}

		close(pfd[0]);

		result = 0;
		while ((result == 0) && (waitpid(childpid, &result, 0) < 0)) {
			if (errno != EINTR) {
				errprintf("Error picking up child exit status: %s\n", strerror(errno));
				result = -1;
			}
		}

		if (WIFEXITED(result)) {
			result = WEXITSTATUS(result);
		}
		else if (WIFSIGNALED(result)) {
			errprintf("Child process terminated with signal %d\n", WTERMSIG(result));
			result = -1;
		}
	}

	MEMUNDEFINE(l);
	return result;
}



