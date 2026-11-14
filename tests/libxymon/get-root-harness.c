/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/get-root-harness.c
 *
 * Calls lib/suid.c's drop_root() then get_root() and checks the process is
 * still there with the privileges it should have: for root, get_root()
 * gives root back; for a user who cannot become root, it returns with the
 * effective uid unchanged instead of aborting. Run as root -- as the CI
 * lanes are -- it checks root, and a child that has given root up for good
 * (uid 65534) checks the user too, so the case that matters runs either
 * way. Run by tests/libxymon/get-root.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libxymon.h"

static int check(void)
{
	uid_t uid = getuid();

	drop_root();
	get_root();
	if (geteuid() != uid) {
		printf("FAIL get_root() as uid %d left euid %d\n", (int)uid, (int)geteuid());
		return 1;
	}
	drop_root();
	if (geteuid() != uid) {
		printf("FAIL drop_root() as uid %d left euid %d\n", (int)uid, (int)geteuid());
		return 1;
	}
	return 0;
}

int main(void)
{
	pid_t pid;
	int status = 0;

	if (getuid() != 0) return check();

	/*
	 * The user case runs in a child forked before root's check: drop_root()
	 * remembers the uid of its first call, so a process that has already
	 * checked as root would go on dropping to uid 0 after setuid(65534).
	 * Nothing is printed before the fork, so exit() flushes only the
	 * child's own lines.
	 */
	pid = fork();
	if (pid == -1) {
		printf("FAIL fork() for the user case\n");
		return 1;
	}
	if (pid == 0) {
		if ((setgid(65534) != 0) || (setuid(65534) != 0)) {
			printf("FAIL root could not become uid 65534 to check the user case\n");
			exit(1);
		}
		exit(check());
	}
	if (check() != 0) return 1;
	if ((waitpid(pid, &status, 0) != pid) || !WIFEXITED(status)) {
		printf("FAIL the user case did not exit (status %d)\n", status);
		return 1;
	}
	return WEXITSTATUS(status);
}
