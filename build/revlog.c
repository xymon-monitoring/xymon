#include <stdio.h>

int main(int argc, char *argv[])
{
	char *date = argv[1];
	char *fn = argv[2];
	FILE *logfd;
	char cmd[4096];
	char buf[4096];
	int gotrevmarker = 0;
	int gotlocksline = 0;
	int fileislocked = 0;

	sprintf(cmd, "rlog \"-d>%s\" %s 2>/dev/null", date, fn);
	logfd = popen(cmd, "r");
	while (fgets(buf, sizeof(buf), logfd)) {
		if (gotlocksline == 0) {
			if (strncmp(buf, "locks:", 6) == 0) gotlocksline = 1;
		}
		else if (gotlocksline == 1) {
			if (isspace(*buf)) fileislocked = 1;
			gotlocksline = 2;
		}

		if (!gotrevmarker) {
			gotrevmarker = (strcmp(buf, "----------------------------\n") == 0);
			if (gotrevmarker) {
				fprintf(stdout, "%s", fn);
				if (fileislocked) fprintf(stdout, " (is being edited)");
				fprintf(stdout, "\n");
				fileislocked = 0;
			}
		}

		if (gotrevmarker) fprintf(stdout, "%s", buf);
	}
	pclose(logfd);

	if (fileislocked) {
		/* Locked file, but we haven't shown anything yet */
		fprintf(stdout, "%s", fn);
		if (fileislocked) fprintf(stdout, " (is being edited)");
		fprintf(stdout, "\n");
		fprintf(stdout, "%s\n", "=============================================================================");
	}

	if (gotrevmarker || fileislocked) fprintf(stdout, "\n");

	return 0;
}

