#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "bbd_net.h"

char *channelnames[] = {
	"status", 
	"stachg",
	"page",
	"data",
	"notes",
	"enadis",
	NULL
};

bbd_channel_t *setup_channel(enum msgchannels_t chnid, int flags)
{
	key_t key = ftok("bbd_net", chnid);
	bbd_channel_t *newch = (bbd_channel_t *)malloc(sizeof(bbd_channel_t));

	newch->channelid = chnid;
	newch->shmid = shmget(key, MAXMSG+1024, flags);
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

	newch->semid = semget(key, 2, flags);
	if (newch->semid == -1) {
		errprintf("Could not get sem %s\n", strerror(errno));
		free(newch);
		return NULL;
	}

	return newch;
}

void close_channel(bbd_channel_t *chn, int removeit)
{
	if (chn == NULL) return;

	if (removeit) semctl(chn->semid, 0, IPC_RMID);

	shmdt(chn->channelbuf);
	if (removeit) shmctl(chn->shmid, IPC_RMID, NULL);
}

