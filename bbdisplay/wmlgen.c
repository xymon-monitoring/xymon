#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "util.h"
#include "wmlgen.h"

void do_wml_cards(void)
{
	FILE		*fd;
	char		fn[MAX_PATH];
	hostlist_t	*h;
	entry_t		*t;
	int		wapcolor, hostcolor;
	pid_t pid;
	char mkbbwmlcmd[MAX_PATH];
	char newbbhosts[MAX_PATH];

	sprintf(fn, "%s/bb-hosts-wml.tmp", getenv("BBTMP"));
	sprintf(newbbhosts, "BBHOSTS=%s", fn);
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot open %s\n", fn);
		return;
	}

	wapcolor = COL_GREEN;
	for (h = hosthead; (h); h = h->next) {
		hostcolor = COL_GREEN;
		for (t = h->hostentry->entries; (t); t = t->next) {
			if (t->onwap && (t->color > hostcolor)) hostcolor = t->color;
		}

		/* We only care about RED or YELLOW */
		switch (hostcolor) {
		 case COL_RED:
		 case COL_YELLOW:
			if (hostcolor > wapcolor) wapcolor = hostcolor;

			/* Include this host in the hosts to generate WML pages for. */
			fprintf(fd, "%s %s %s\n", 
				h->hostentry->ip, h->hostentry->hostname,
				h->hostentry->rawentry);
		}
	}

	fclose(fd);

	/* Create the file used to determine background color in mkbbwml.sh */
	sprintf(fn, "%s/.bkg", getenv("BBLOGS"));
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot open %s\n", fn);
		return;
	}
	fprintf(fd, "%s\n", colorname(wapcolor));
	fclose(fd);

	/* Fork off the WML generator */
	sprintf(mkbbwmlcmd, "%s/web/mkbbwml.sh", getenv("BBHOME"));
	pid = fork();
	if (pid == -1) {
		errprintf("Fork error in forking %s\n", mkbbwmlcmd);
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

