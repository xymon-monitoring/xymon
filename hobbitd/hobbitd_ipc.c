/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This module implements the setup/teardown of the bbgend communications     */
/* channel, using standard System V IPC mechanisms: Shared memory and         */
/* semaphores.                                                                */
/*                                                                            */
/* The concept is to use a shared memory segment for each "channel" that      */
/* bbgend supports. This memory segment is then used to pass a single bbgend  */
/* message between the bbgend master daemon, and the bbgend_channel workers.  */
/* Two semaphores are used to synchronize between the master daemon and the   */
/* workers, i.e. the workers wait for a semaphore to go up indicating that a  */
/* new message has arrived, and the master daemon then waits for the other    */
/* semaphore to go 0 indicating that the workers have read the message. A     */
/* third semaphore is used as a simple counter to tell how many workers have  */
/* attached to a channel.                                                     */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_ipc.c,v 1.11 2004-11-23 21:46:14 henrik Exp $";

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "libbbgen.h"

#include "bbgend_ipc.h"

char *channelnames[] = {
	"status", 
	"stachg",
	"page",
	"data",
	"notes",
	"enadis",
	NULL
};

bbgend_channel_t *setup_channel(enum msgchannels_t chnid, int role)
{
	key_t key;
	struct stat st;
	struct sembuf s;
	bbgend_channel_t *newch;
	int flags = ((role == CHAN_MASTER) ? (IPC_CREAT | 0600) : 0);

	if ( (getenv("BBHOME") == NULL) || (stat(getenv("BBHOME"), &st) == -1) ) {
		errprintf("BBHOME not defined, or points to invalid directory - cannot continue.\n");
		return NULL;
	}

	key = ftok(getenv("BBHOME"), chnid);
	newch = (bbgend_channel_t *)malloc(sizeof(bbgend_channel_t));

	newch->seq = 0;
	newch->channelid = chnid;
	newch->msgcount = 0;
	newch->shmid = shmget(key, SHAREDBUFSZ, flags);
	if (newch->shmid == -1) {
		errprintf("Could not get shm %s\n", strerror(errno));
		free(newch);
		return NULL;
	}

	newch->channelbuf = (char *) shmat(newch->shmid, NULL, 0);
	if (newch->channelbuf == (char *)-1) {
		errprintf("Could not attach shm %s\n", strerror(errno));
		free(newch);
		return NULL;
	}

	newch->semid = semget(key, 3, flags);
	if (newch->semid == -1) {
		errprintf("Could not get sem %s\n", strerror(errno));
		free(newch);
		return NULL;
	}

	if (role == CHAN_CLIENT) {
		/*
		 * Clients must register their presence.
		 * We use SEM_UNDO; so if the client crashes, it wont leave a stale count.
		 */
		s.sem_num = CLIENTCOUNT; s.sem_op = +1; s.sem_flg = SEM_UNDO;
		if (semop(newch->semid, &s, 1) == -1) {
			errprintf("Could not register presence\n", strerror(errno));
			return NULL;
		}
	}
	else if (role == CHAN_MASTER) {
		int n;

		n = semctl(newch->semid, CLIENTCOUNT, GETVAL);
		if (n > 0) {
			errprintf("FATAL: bbgend sees clientcount %d, should be 0\nCheck for hanging bbgend_channel processes or stale semaphores\n", n);
			return NULL;
		}
	}

	return newch;
}

void close_channel(bbgend_channel_t *chn, int role)
{
	if (chn == NULL) return;

	/* No need to de-register, this happens automatically because we registered with SEM_UNDO */

	if (role == CHAN_MASTER) semctl(chn->semid, 0, IPC_RMID);

	shmdt(chn->channelbuf);
	if (role == CHAN_MASTER) shmctl(chn->shmid, IPC_RMID, NULL);
}

