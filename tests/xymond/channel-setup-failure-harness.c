/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/xymond/channel-setup-failure-harness.c
 *
 * Makes one of xymond's channels impossible to set up, and reports what
 * SysV IPC a channel's key still holds, for
 * tests/xymond/channel-setup-failure-releases-ipc.sh.
 *
 * A channel's key is ftok($XYMONHOME, channel id), as in setup_channel()
 * (lib/xymond_ipc.c). Every command takes a channel name and works on that
 * key, so the test never needs the ids xymond itself got.
 *
 *   block <channel>   create a semaphore set of ONE semaphore on the key.
 *                     setup_channel() asks for three on it, and semget()
 *                     refuses a count larger than the existing set's with
 *                     EINVAL, so that channel fails to set up -- after every
 *                     channel before it has succeeded.
 *   held <channel>    print "sem" and/or "shm" for what still exists on the
 *                     key, or nothing; exit 0 either way.
 *   release <channel> remove both, for cleanup. Best-effort.
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
	char *home = getenv("XYMONHOME");
	int cnid, semid, shmid;
	key_t key;

	if (argc != 3) {
		fprintf(stderr, "usage: %s block|held|release <channel>\n", argv[0]);
		return 2;
	}
	if (home == NULL) {
		fprintf(stderr, "XYMONHOME is not set\n");
		return 2;
	}
	cnid = channel_id(argv[2]);
	if (cnid == -1) {
		fprintf(stderr, "unknown channel name '%s'\n", argv[2]);
		return 2;
	}
	key = ftok(home, cnid);
	if (key == -1) {
		fprintf(stderr, "ftok(%s, %d): %s\n", home, cnid, strerror(errno));
		return 1;
	}

	if (strcmp(argv[1], "block") == 0) {
		if (semget(key, 1, IPC_CREAT | IPC_EXCL | 0600) == -1) {
			fprintf(stderr, "semget(0x%X, 1, IPC_CREAT|IPC_EXCL): %s\n", (unsigned int)key, strerror(errno));
			return 1;
		}
		return 0;
	}

	semid = semget(key, 0, 0);
	shmid = shmget(key, 0, 0);

	if (strcmp(argv[1], "held") == 0) {
		if (semid != -1) printf("sem\n");
		if (shmid != -1) printf("shm\n");
		return 0;
	}

	if (strcmp(argv[1], "release") == 0) {
		if (semid != -1) semctl(semid, 0, IPC_RMID);
		if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
		return 0;
	}

	fprintf(stderr, "usage: %s block|held|release <channel>\n", argv[0]);
	return 2;
}
