#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "bbd_alert.h"
#include "bbgen.h"
#include "util.h"

void send_alert(activealerts_t *awalk)
{
	char cmd[4096];
	FILE *mailpipe;

	sprintf(cmd, "%s \"BB alert %s:%s %s\" %s",
		getenv("MAIL"),
		awalk->hostname->name, awalk->testname->name,
		colorname(awalk->color),
		"henrik@hswn.dk");
	mailpipe = popen(cmd, "w");
	if (mailpipe) {
		fprintf(mailpipe, "%s", awalk->pagemessage);
		pclose(mailpipe);
	}
}


time_t next_alert(activealerts_t *awalk)
{
	time_t result = time(NULL);

	return result+300;
}

