#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "bbd_net.h"
#include "bbdutil.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

typedef struct msg_t {
	char *buf;
	char *bufp;
	int buflen;
	struct msg_t *next;
} msg_t;

msg_t *head = NULL;
msg_t *tail = NULL;

static volatile int running = 1;
static int childexit = -1;
bbd_channel_t *channel = NULL;

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGCHLD:
		/* Our worker child died. Follow it to the grave */
		wait(&childexit);
		break;

	  case SIGPIPE:
		/* We lost the child */
		break;
	}

	running = 0;
}

int main(int argc, char *argv[])
{
	int argi, n;

	struct sembuf s;
	char buf[SHAREDBUFSZ];
	msg_t *newmsg;
	int daemonize = 1;

	int cnid;
	int pfd[2];
	char *childcmd;
	char **childargs;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--channel=", 10) == 0) {
			char *cn = strchr(argv[argi], '=') + 1;

			for (cnid = 0; (channelnames[cnid] && strcmp(channelnames[cnid], cn)); cnid++) ;
		}
		else if (strcmp(argv[argi], "--daemon") == 0) {
			daemonize = 1;
		}
		else if (strcmp(argv[argi], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else {
			int i = 0;
			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
		}
	}

	/* Go daemon */
	if (daemonize) {
		pid_t childpid;

		/* We wont close stdin/stdout/stderr here, since the worker process might need them. */

		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent exits */
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	/* Start the channel handler */
	n = pipe(pfd);
	if (n == -1) {
		errprintf("Could not get a pipe: %s\n", strerror(errno));
		return 1;
	}
	n = fork();
	if (n == -1) {
		errprintf("Could not fork channel handler: %s\n", strerror(errno));
		return 1;
	}
	else if (n == 0) {
		/* The channel handler child */
		n = dup2(pfd[0], STDIN_FILENO);
		close(pfd[0]); close(pfd[1]);
		n = execvp(childcmd, childargs);
	}
	/* Parent process continues */
	close(pfd[0]);

	/* We dont want to block when writing to the worker */
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);

	setup_signalhandler("bbd_channel");
	signal(SIGPIPE, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGCHLD, sig_handler);

	/* Attach to the channel */
	channel = setup_channel(cnid, 0);
	if (channel == NULL) {
		errprintf("Channel not available\n");
		return 1;
	}

	do {
		/* 
		 * Wait for GOCLIENT to go up.
		 *
		 * Note that we use IPC_NOWAIT if there are messages in the
		 * queue, because then we just want to pick up a message if
		 * there is one, and if not we want to continue pushing the
		 * queued data to the worker.
		 */
		dprintf("Waiting for goclient\n");
		s.sem_num = GOCLIENT; s.sem_op  = -1; s.sem_flg = (head ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);
		if (n == 0) {
			/* Copy the message */
			strcpy(buf, channel->channelbuf);

			/* 
			 * Let master know we got it by downing BOARDBUSY.
			 * This should not block, since BOARDBUSY is upped
			 * by the master just before he ups GOCLIENT.
			 */
			s.sem_num = BOARDBUSY; s.sem_op  = -1; s.sem_flg = 0;
			n = semop(channel->semid, &s, 1);

			dprintf("Got msg\n", buf);

			newmsg = (msg_t *) malloc(sizeof(msg_t));
			newmsg->buf = strdup(buf);
			newmsg->bufp = newmsg->buf;
			newmsg->buflen = strlen(buf);
			newmsg->next = NULL;
			if (head == NULL) {
				head = tail = newmsg;
			}
			else {
				tail->next = newmsg;
				tail = newmsg;
			}

			/* 
			 * Wait until other clients on the same channel have picked up 
			 * this message (GOCLIENT reaches 0).
			 * This can block, but should not block for very long.
			 */
			dprintf("Waiting for goclient to drop\n");
			s.sem_num = GOCLIENT; s.sem_op = 0; s.sem_flg = 0;
			n = semop(channel->semid, &s, 1);
		}
		else {
			if (errno != EAGAIN) {
				dprintf("Semaphore wait aborted: %s\n", strerror(errno));
				break;
			}
		}

		/* 
		 * We've picked up messages from the master. Now we 
		 * must push them to the worker process. Since there 
		 * is no way to hang off both a semaphore and select(),
		 * this boils down to doing a kind of busy waiting.
		 * In practice, the queue will be empty most of the
		 * time and then we will just wait on the GOCHILD 
		 * semaphore, so it isn't really much of a concern.
		 *
		 * Maybe it could be optimized via SIGIO ... 
		 */
		if (head) {
			n = write(pfd[1], head->bufp, head->buflen);
			if (n >= 0) {
				head->bufp += n;
				head->buflen -= n;
				if (head->buflen == 0) {
					msg_t *tmp = head;
					free(head->buf);
					head = head->next;
					free(tmp);
				}
			}
			else if (errno == EAGAIN) {
				/*
				 * Wait just a little while so we dont spin out of 
				 * control when worker child is busy.
				 */
				usleep(5000);
			}
			else {
				/* Write failed */
				errprintf("Our child has failed and will not talk to us\n");
				msg_t *tmp = head;
				free(head->buf);
				head = head->next;
				free(tmp);
			}
		}
	} while (running);

	if (childexit != -1) {
		errprintf("Worker process died with exit code %d, terminating\n", childexit);
	}

	/* Detach from channels */
	close_channel(channel, 0);

	return 0;
}

