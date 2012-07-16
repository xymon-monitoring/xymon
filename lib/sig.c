/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for handling of signals and crashes.                  */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "libxymon.h"

/* Data used while crashing - cannot depend on the stack being usable */
static char signal_xymoncmd[PATH_MAX];
static char signal_xymondserver[1024];
static char signal_msg[1024];
static char signal_tmpdir[PATH_MAX];


static void sigsegv_handler(int signum)
{
	/*
	 * This is a signal handler. Only a very limited number of 
	 * library routines can be safely used here, according to
	 * Posix: http://www.opengroup.org/onlinepubs/007904975/functions/xsh_chap02_04.html#tag_02_04_03
	 * Do not use string, stdio etc. - just basic system calls.
	 * That is why we need to setup all of the strings in advance.
	 */

	signal(signum, SIG_DFL);

	/* 
	 * Try to fork a child to send in an alarm message.
	 * If the fork fails, then just attempt to exec() the XYMON command
	 */
	if (fork() <= 0) {
		execl(signal_xymoncmd, "xymon-signal", signal_xymondserver, signal_msg, NULL);
	}

	/* Dump core and abort */
	if (chdir(signal_tmpdir) == 0) {};	/* Cannot chdir? Well, abort anyway */
	abort();
}

static void sigusr2_handler(int signum)
{
	/* SIGUSR2 toggles debugging */

	if (debug) {
		dbgprintf("Debug OFF\n");
		debug = 0;
	}
	else {
		debug = 1;
		dbgprintf("Debug ON\n");
	}
}

void setup_signalhandler(char *programname)
{
	struct rlimit lim;
	struct sigaction sa;

	MEMDEFINE(signal_xymoncmd);
	MEMDEFINE(signal_xymondserver);
	MEMDEFINE(signal_tmpdir);
	MEMDEFINE(signal_msg);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigsegv_handler;

	/*
	 * Try to allow ourselves to generate core files
	 */
	getrlimit(RLIMIT_CORE, &lim);
	lim.rlim_cur = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &lim);

	if (xgetenv("XYMON") == NULL) return;
	if (xgetenv("XYMSRV") == NULL) return;

	/*
	 * Used inside signal-handler. Must be setup in
	 * advance.
	 */
	strcpy(signal_xymoncmd, xgetenv("XYMON"));
	strcpy(signal_xymondserver, xgetenv("XYMSRV"));
	strcpy(signal_tmpdir, xgetenv("XYMONTMP"));
	sprintf(signal_msg, "status %s.%s red - Program crashed\n\nFatal signal caught!\n", 
		(xgetenv("MACHINE") ? xgetenv("MACHINE") : "XYMSERVERS"), programname);

	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
#ifdef SIGBUS
	sigaction(SIGBUS, &sa, NULL);
#endif

	/*
	 * After lengthy debugging and perusing of mail archives:
	 * Need to ignore SIGPIPE since FreeBSD (and others?) can throw this
	 * on a write() instead of simply returning -EPIPE like any sane
	 * OS would.
	 */
	signal(SIGPIPE, SIG_IGN);

	/* Ignore SIGUSR1 unless explicitly set by main program */
	signal (SIGUSR1, SIG_IGN);

	/* SIGUSR2 toggles debugging */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigusr2_handler;
	sigaction(SIGUSR2, &sa, NULL);
}

