#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ares.h>
#include <ares_dns.h>
#include <ares_version.h>


int main(int argc, char *argv[])
{
	static ares_channel mychannel;
	struct ares_options options;
	int status, version, ver_maj, ver_min, ver_patch;
	int ver_maj_required, ver_min_required, ver_patch_required, failed = 0;
	const char *versionstr;
	char *version_required, *tok;

	version_required = strdup(argv[1]);
	tok = strtok(version_required, "."); ver_maj_required = atoi(tok);
	tok = strtok(NULL, "."); ver_min_required = atoi(tok);
	tok = strtok(NULL, "."); ver_patch_required = atoi(tok);

	versionstr = ares_version(&version);
	ver_maj = ((version >> 16) & 0xFF);
	ver_min = ((version >> 8)  & 0xFF);
	ver_patch = (version & 0xFF);
	if (ver_maj > ver_maj_required) 
		failed = 0;
	else if (ver_maj < ver_maj_required) 
		failed = 1;
	else {
		/* Major version matches */
		if (ver_min > ver_min_required)
			failed = 0;
		else if (ver_min < ver_min_required)
			failed = 1;
		else {
			/* Major and minor version matches */
			if (ver_patch > ver_patch_required)
				failed = 0;
			else if (ver_patch < ver_patch_required)
				failed = 1;
			else {
				/* Major, minor and patch matches */
				failed = 0;
			}
		}
	}
	printf("C-ARES version: Found %d.%d.%d - %s, require %d.%d.%d\n", 
		ver_maj, ver_min, ver_patch, 
		(failed ? "too old, will use included version" : "OK"),
		ver_maj_required, ver_min_required, ver_patch_required );

	/* ARES timeout backported from Xymon trunk 20120411 - this should give us a ~23 second timeout */
	options.timeout = 2000;
	options.tries = 4;

	status = ares_init_options(&mychannel, &options, (ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES));
	if (status != ARES_SUCCESS) {
		printf("c-ares init failed\n");
		return 1;
	}

	ares_destroy(mychannel);
	return (failed ? 1 : 0);
}

