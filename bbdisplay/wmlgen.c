#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "wmlgen.h"

void do_wml_cards(void)
{
	FILE		*fd;
	char		fn[MAX_PATH];
	hostlist_t	*h;
	entry_t		*t;
	int		wapred;
	pid_t pid;
	char mkbbwmlcmd[MAX_PATH];
	char newbbhosts[MAX_PATH];

	sprintf(fn, "%s/bb-hosts-wml.tmp", getenv("BBTMP"));
	fd = fopen(fn, "w");
	if (fd == NULL) {
		printf("Cannot open %s\n", fn);
		return;
	}

	for (h = hosthead; (h); h = h->next) {
		/* See if there are any WAP enabled tests that are RED */
		for (t = h->hostentry->entries, wapred=0; (t && (wapred == 0)); t = t->next) {
			wapred = (t->onwap && (t->color == COL_RED));
		}

		if (wapred) {
			/* Include this host in the hosts to generate WML pages for. */
			fprintf(fd, "%s %s %s\n", 
				h->hostentry->ip, h->hostentry->hostname,
				h->hostentry->rawentry);
		}
	}

	fclose(fd);

	/* Fork off the WML generator */
	sprintf(newbbhosts, "BBHOSTS=%s", fn);
	sprintf(mkbbwmlcmd, "%s/web/mkbbwml.sh", getenv("BBHOME"));
	pid = fork();
	if (pid == -1) {
		printf("Fork error in forking %s\n", mkbbwmlcmd);
		return;
	}
	else if (pid == 0) {
		putenv(newbbhosts);
		execl(mkbbwmlcmd, "mkbbwml.sh",NULL);
	}
	else {
		wait(NULL);
	}
}

