/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/xymond/channel-fatal-semop-harness.c
 *
 * Stands in for xymond as the owner of a channel's SysV IPC, so a test can
 * run the real xymond_channel against a real channel and then tear the
 * semaphore set out from under it.
 *
 * SysV IPC is kernel-persistent: the ids stay valid after the creating
 * process exits, so "create" can create and return rather than having to
 * stay alive alongside the test.
 *
 *   create <channelname>    create the channel as CHAN_MASTER; print its
 *                           shmid and semid on stdout as SHMID=/SEMID=
 *   clients <semid>         print the CLIENTCOUNT semaphore.  A test must not
 *                           break the channel before xymond_channel has
 *                           attached to it, or the process exits for the
 *                           wrong reason and the test passes vacuously;
 *                           CLIENTCOUNT going to 1 is that readiness gate.
 *   rmsem <semid>           IPC_RMID the semaphore set.  Every semop already
 *                           blocked on it returns -1 with EIDRM, and every
 *                           later one fails the same way: the permanent,
 *                           fatal error the test is about.
 *   rmall <shmid> <semid>   remove both, for cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "libxymon.h"

/* Same lookup xymond_channel does for --channel= (xymond_channel.c). */
static int channel_id(char *name)
{
	int cnid;

	for (cnid = C_STATUS; (channelnames[cnid] && strcmp(channelnames[cnid], name)); cnid++) ;
	return (channelnames[cnid] ? cnid : -1);
}

int main(int argc, char *argv[])
{
	if ((argc == 3) && (strcmp(argv[1], "create") == 0)) {
		int cnid = channel_id(argv[2]);
		xymond_channel_t *channel;

		if (cnid == -1) {
			fprintf(stderr, "unknown channel name '%s'\n", argv[2]);
			return 1;
		}

		channel = setup_channel(cnid, CHAN_MASTER);
		if (channel == NULL) {
			fprintf(stderr, "setup_channel(%s, CHAN_MASTER) failed\n", argv[2]);
			return 1;
		}

		printf("SHMID=%d\n", channel->shmid);
		printf("SEMID=%d\n", channel->semid);
		return 0;
	}

	if ((argc == 3) && (strcmp(argv[1], "clients") == 0)) {
		int n = semctl(atoi(argv[2]), CLIENTCOUNT, GETVAL);

		if (n == -1) {
			fprintf(stderr, "semctl(%s, CLIENTCOUNT, GETVAL): %s\n", argv[2], strerror(errno));
			return 1;
		}
		printf("%d\n", n);
		return 0;
	}

	if ((argc == 3) && (strcmp(argv[1], "rmsem") == 0)) {
		int semid = atoi(argv[2]);

		if (semctl(semid, 0, IPC_RMID) == -1) {
			fprintf(stderr, "semctl(%d, IPC_RMID): %s\n", semid, strerror(errno));
			return 1;
		}
		return 0;
	}

	if ((argc == 4) && (strcmp(argv[1], "rmall") == 0)) {
		/* Best-effort cleanup: either may already be gone. */
		shmctl(atoi(argv[2]), IPC_RMID, NULL);
		semctl(atoi(argv[3]), 0, IPC_RMID);
		return 0;
	}

	fprintf(stderr, "usage: %s create <channel> | clients <semid> | rmsem <semid> | rmall <shmid> <semid>\n", argv[0]);
	return 2;
}
