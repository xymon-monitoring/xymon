/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This module receives messages from one channel of the Hobbit master daemon.*/
/* These messages are then forwarded to the actual worker process via stdin;  */
/* the worker process can process the messages without having to care about   */
/* the tricky details in the hobbitd/hobbitd_channel communications.          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_channel.c,v 1.49 2006-08-09 19:47:18 henrik Exp $";

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "libbbgen.h"

#include "hobbitd_ipc.h"

/* For our in-memory queue of messages received from hobbitd via IPC */
typedef struct hobbit_msg_t {
	char *buf;
	char *bufp;
	int buflen;
	struct hobbit_msg_t *next;
} hobbit_msg_t;

hobbit_msg_t *head = NULL;
hobbit_msg_t *tail = NULL;

static volatile int running = 1;
static volatile int gotalarm = 0;
static int childexit = -1;
hobbitd_channel_t *channel = NULL;

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
	  case SIGPIPE:
		/* We lost the pipe to the worker child. Shutdown. */
		running = 0;
		break;

	  case SIGCHLD:
		/* Our worker child died. Follow it to the grave */
		wait(&childexit);
		running = 0;
		break;

	  case SIGALRM:
		gotalarm = 1;
		break;
	}
}

int main(int argc, char *argv[])
{
	int argi, n;

	struct sembuf s;
	hobbit_msg_t *newmsg;
	int daemonize = 0;
	char *logfn = NULL;
	char *pidfile = NULL;
	char *envarea = NULL;

	int cnid = -1;
	int pfd[2];
	pid_t childpid = 0;
	char *childcmd = NULL;
	char **childargs = NULL;
	int canwrite;
	struct sigaction sa;

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--channel=")) {
			char *cn = strchr(argv[argi], '=') + 1;

			for (cnid = C_STATUS; (channelnames[cnid] && strcmp(channelnames[cnid], cn)); cnid++) ;
			if (channelnames[cnid] == NULL) cnid = -1;
		}
		else if (argnmatch(argv[argi], "--daemon")) {
			daemonize = 1;
		}
		else if (argnmatch(argv[argi], "--no-daemon")) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--pidfile=")) {
			char *p = strchr(argv[argi], '=');
			pidfile = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');
			logfn = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else {
			int i = 0;
			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
		}
	}

	if (childcmd == NULL) {
		errprintf("No command to pass data to\n");
		return 1;
	}

	if (cnid == -1) {
		errprintf("No channel/unknown channel specified\n");
		return 1;
	}

	/* Go daemon */
	if (daemonize) {
		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork child\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent exits */
			FILE *fd = NULL;
			if (pidfile) fd = fopen(pidfile, "w");
			if (fd) {
				fprintf(fd, "%d\n", (int)childpid);
				fclose(fd);
			}
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	/* Catch signals */
	setup_signalhandler("hobbitd_channel");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	/* Switch stdout/stderr to the logfile, if one was specified */
	freopen("/dev/null", "r", stdin);	/* hobbitd_channel's stdin is not used */
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	/* Start the channel handler */
	n = pipe(pfd);
	if (n == -1) {
		errprintf("Could not get a pipe: %s\n", strerror(errno));
		return 1;
	}
	childpid = fork();
	if (childpid == -1) {
		errprintf("Could not fork channel handler: %s\n", strerror(errno));
		return 1;
	}
	else if (childpid == 0) {
		/* The channel handler child */

		if (logfn) {
			char *logfnenv = (char *)malloc(strlen(logfn) + 30);
			sprintf(logfnenv, "HOBBITCHANNEL_LOGFILENAME=%s", logfn);
			putenv(logfnenv);
		}

		n = dup2(pfd[0], STDIN_FILENO);
		close(pfd[0]); close(pfd[1]);
		n = execvp(childcmd, childargs);
	}
	/* Parent process continues */
	close(pfd[0]);

	/* We dont want to block when writing to the worker */
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);

	/* Attach to the channel */
	channel = setup_channel(cnid, CHAN_CLIENT);
	if (channel == NULL) {
		errprintf("Channel not available\n");
		return 1;
	}

	while (running) {
		/* 
		 * Wait for GOCLIENT to go up.
		 *
		 * Note that we use IPC_NOWAIT if there are messages in the
		 * queue, because then we just want to pick up a message if
		 * there is one, and if not we want to continue pushing the
		 * queued data to the worker.
		 */
		s.sem_num = GOCLIENT; s.sem_op  = -1; s.sem_flg = (head ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);

		if (n == 0) {
			/*
			 * GOCLIENT went high, and so we got alerted about a new
			 * message arriving. Copy the message to our own buffer queue.
			 */
			char *inbuf = strdup(channel->channelbuf);

			/* 
			 * Now we have safely stored the new message in our buffer.
			 * Wait until any other clients on the same channel have picked up 
			 * this message (GOCLIENT reaches 0).
			 *
			 * We wrap this into an alarm handler, because it can occasionally
			 * fail, causing the whole system to lock up. We dont want that....
			 * We'll set the alarm to trigger after 1 second. Experience shows
			 * that we'll either succeed in a few milliseconds, or fail completely
			 * and wait the full alarm-timer duration.
			 */
			gotalarm = 0; signal(SIGALRM, sig_handler); alarm(2); 
			do {
				s.sem_num = GOCLIENT; s.sem_op  = 0; s.sem_flg = 0;
				n = semop(channel->semid, &s, 1);
			} while ((n == -1) && (errno == EAGAIN) && running && (!gotalarm));
			signal(SIGALRM, SIG_IGN);

			if (gotalarm) {
				errprintf("Gave up waiting for GOCLIENT to go low.\n");
			}

			/* 
			 * Let master know we got it by downing BOARDBUSY.
			 * This should not block, since BOARDBUSY is upped
			 * by the master just before he ups GOCLIENT.
			 */
			do {
				s.sem_num = BOARDBUSY; s.sem_op  = -1; s.sem_flg = IPC_NOWAIT;
				n = semop(channel->semid, &s, 1);
			} while ((n == -1) && (errno == EINTR));
			if (n == -1) {
				errprintf("Tried to down BOARDBUSY: %s\n", strerror(errno));
			}

			/*
			 * Put the new message on our outbound queue.
			 */
			newmsg = (hobbit_msg_t *) malloc(sizeof(hobbit_msg_t));
			newmsg->buf = newmsg->bufp = inbuf;
			newmsg->buflen = strlen(inbuf);
			newmsg->next = NULL;
			if (head == NULL) {
				head = tail = newmsg;
			}
			else {
				tail->next = newmsg;
				tail = newmsg;
			}

			/*
			 * See if they want us to rotate logs. We pass this on to
			 * the worker module as well, but must handle our own logfile.
			 */
			if (strncmp(inbuf, "@@logrotate", 11) == 0) {
				freopen(logfn, "a", stdout);
				freopen(logfn, "a", stderr);
			}
		}
		else {
			if (errno != EAGAIN) {
				dbgprintf("Semaphore wait aborted: %s\n", strerror(errno));
				continue;
			}
		}

		/* 
		 * We've picked up messages from the master. Now we 
		 * must push them to the worker process. Since there 
		 * is no way to hang off both a semaphore and select(),
		 * we'll just push as much data as possible into the 
		 * pipe. If we get to a point where we would block,
		 * then wait a teeny bit of time and restart the 
		 * whole loop with checking for new messages from the
		 * master etc.
		 *
		 * In theory, this could become an almost busy-wait loop.
		 * In practice, however, the queue will be empty most
		 * of the time because we'll just shove the data to the
		 * worker child.
		 */
		canwrite = 1;
		while (head && canwrite) {
			n = write(pfd[1], head->bufp, head->buflen);
			if (n >= 0) {
				head->bufp += n;
				head->buflen -= n;
				if (head->buflen == 0) {
					hobbit_msg_t *tmp = head;
					head = head->next; if (!head) tail = NULL;
					xfree(tmp->buf);
					xfree(tmp);
				}
			}
			else if (errno == EAGAIN) {
				/*
				 * Write would block ... stop for now. 
				 * Wait just a little while before continuing, so we
				 * dont do busy-waiting when the worker child is not
				 * accepting more data.
				 */
				canwrite = 0;
				usleep(2500);
			}
			else {
				hobbit_msg_t *tmp;

				/* Write failed */
				errprintf("Our child has failed and will not talk to us: Channel %s, PID %d, cause %s\n",
					  channelnames[cnid], getpid(), strerror(errno));
				tmp = head;
				head = head->next; if (!head) tail = NULL;
				xfree(tmp->buf);
				xfree(tmp);
				canwrite = 0;
			}
		}
	}

	if (childexit != -1) {
		errprintf("Worker process died with exit code %d, terminating\n", childexit);
	}
	else {
		if (childpid > 0) kill(childpid, SIGTERM);
	}

	/* Detach from channels */
	close_channel(channel, CHAN_CLIENT);

	if (pidfile) unlink(pidfile);

	return (childexit != -1) ? 1 : 0;
}

