#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "bbd_net.h"
#include "bbdutil.h"

char *channelnames[] = {
	"status", 
	"stachg",
	"page",
	"data",
	"notes",
	"enadis",
	NULL
};

bbd_channel_t *setup_channel(enum msgchannels_t chnid, int role)
{
	key_t key;
	struct stat st;
	struct sembuf s;
	bbd_channel_t *newch;
	int flags = ((role == CHAN_MASTER) ? (IPC_CREAT | 0600) : 0);

	if ( (getenv("BBHOME") == NULL) || (stat(getenv("BBHOME"), &st) == -1) ) {
		errprintf("BBHOME not defined, or points to invalid directory - cannot continue.\n");
		return NULL;
	}

	key = ftok(getenv("BBHOME"), chnid);
	newch = (bbd_channel_t *)malloc(sizeof(bbd_channel_t));

	newch->seq = 0;
	newch->channelid = chnid;
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
			errprintf("FATAL: bbd_net sees clientcount %d, should be 0\nCheck for hanging bbd_channel processes or stale semaphores\n", n);
			return NULL;
		}
	}

	return newch;
}

void close_channel(bbd_channel_t *chn, int role)
{
	if (chn == NULL) return;

	/* No need to de-register, this happens automatically because we registered with SEM_UNDO */

	if (role == CHAN_MASTER) semctl(chn->semid, 0, IPC_RMID);

	shmdt(chn->channelbuf);
	if (role == CHAN_MASTER) shmctl(chn->shmid, IPC_RMID, NULL);
}

