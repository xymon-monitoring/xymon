#include <stdio.h>

#include <ares.h>
#include <ares_dns.h>
#include <ares_version.h>


int main(int argc, char *argv[])
{
	static ares_channel mychannel;
	struct ares_options options;
	int status;

	/* ARES timeout backported from Xymon trunk 20120411 - this should give us a ~23 second timeout */
	options.timeout = 2000;
	options.tries = 4;

	status = ares_init_options(&mychannel, &options, (ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES));
	if (status != ARES_SUCCESS) {
		printf("c-ares init ok\n");
		return 1;
	}

	ares_destroy(mychannel);
	return 0;
}

