#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bbgen.h"
#include "debug.h"
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
bbd_channel_t *channel = NULL;

void sig_handler(int signum)
{
	running = 0;
}

int main(int argc, char *argv[])
{
	int argi, n;

	struct sembuf s;
	char buf[MAXMSG];
	msg_t *newmsg;

	int cnid;
	int pfd[2];
	pid_t handlerpid = 0;
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
		else {
			int i = 0;
			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
		}
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
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* Attach to the channel */
	channel = setup_channel(cnid, 0);
	if (channel == NULL) {
		errprintf("Channel not available\n");
		return 1;
	}

	/* Start out by signaling that we're ready */
	s.sem_num = 1; s.sem_op  = +1; s.sem_flg = 0;
	n = semop(channel->semid, &s, 1); 

	do {
		/* Wait for a new message */
		dprintf("Waiting for message\n");
		s.sem_num = 0; s.sem_op  = -1; s.sem_flg = (head ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);
		if (n == 0) {
			dprintf("Got it\n");

			/* Copy the message */
			strcpy(buf, channel->channelbuf);

			/* Let master know we got it */
			s.sem_num = 1; s.sem_op  = +1; s.sem_flg = 0;
			n = semop(channel->semid, &s, 1);
	
			dprintf("Got msg: '%s'\n", buf);

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
		}
		else {
			if (errno != EAGAIN) {
				dprintf("Semaphore wait aborted: %s\n", strerror(errno));
				break;
			}
		}

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
		}

	} while (running);

	/* Detach from channels */
	close_channel(channel, 0);

	return 0;
}

