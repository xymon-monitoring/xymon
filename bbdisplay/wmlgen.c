#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "wmlgen.h"

void do_wml_cards(time_t startofrun)
{
	FILE		*fd;
	char		fn[256];
	hostlist_t	*h;
	int		hostcount = 0;
	time_t		timeleft;

	sprintf(fn, "%s/bb-hosts-wml.tmp", getenv("BBTMP"));
	fd = fopen(fn, "w");
	if (fd == NULL) {
		printf("Cannot open %s\n", fn);
		return;
	}

	for (h = hosthead; (h); h = h->next) {
		if (h->hostentry->color == COL_RED) {
			hostcount++;
			fprintf(fd, "%s %s %s\n", 
				h->hostentry->ip, h->hostentry->hostname,
				h->hostentry->rawentry);
		}
	}

	fclose(fd);

	timeleft = atoi(getenv("BBSLEEP")) - (time(NULL) - startofrun); 
	if ((timeleft >= 20) && (hostcount <= 10)) {
		pid_t pid;
		char mkbbwmlcmd[256];
		char newbbhosts[256];

		sprintf(mkbbwmlcmd, "%s/web/mkbbwml.sh", getenv("BBHOME"));
		sprintf(newbbhosts, "BBHOSTS=%s", fn);
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
}

