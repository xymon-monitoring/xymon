/*----------------------------------------------------------------------------*/
/* Hobbit client update tool.                                                 */
/*                                                                            */
/* This tool is used to fetch the current client version from the config-file */
/* saved in etc/clientversion.cfg. The client script compares this with the   */
/* current version on the server, and if they do not match then this utility  */
/* is run to fetch the new version from the server and unpack it via "tar".   */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: clientupdate.c,v 1.1 2006-05-01 20:34:33 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "libbbgen.h"

#define CLIENTVERSIONFILE "etc/clientversion.cfg"

int main(int argc, char *argv[])
{
	int argi;
	char *versionfn;
	FILE *versionfd, *tarpipefd;
	char version[10];
	char *newversion = NULL;
	char *newverreq;

	versionfn = (char *)malloc(strlen(xgetenv("BBHOME")) + strlen(CLIENTVERSIONFILE) + 2);
	sprintf(versionfn, "%s/%s", xgetenv("BBHOME"), CLIENTVERSIONFILE);
	versionfd = fopen(versionfn, "r");
	if (versionfd) {
		char *p;
		fgets(version, sizeof(version), versionfd);
		p = strchr(version, '\n'); if (p) *p = '\0';
		fclose(versionfd);
	}
	else *version = '\0';

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--level") == 0) {
			printf("%s\n", version);
			return 0;
		}
		else if (strncmp(argv[argi], "--update=", 9) == 0) {
			newversion = strdup(argv[argi]+9);
		}
	}

	if (!newversion) return 1;

	/* Update to version "newversion" */
	if (chdir(xgetenv("BBHOME")) != 0) {
		printf("Cannot chdir to BBHOME\n");
		return 1;
	}

	tarpipefd = popen("tar xf -", "w");
	if (tarpipefd == NULL) {
		printf("Cannot launch 'tar xf -'\n");
		return 1;
	}

	newverreq = (char *)malloc(100+strlen(newversion));
	sprintf(newverreq, "download %s.tar", newversion);
	if (sendmessage(newverreq, NULL, tarpipefd, NULL, 1, BBTALK_TIMEOUT) != BB_OK) {
		printf("Cannot fetch new client tarfile\n");
		return 1;
	}

	if (pclose(tarpipefd) != 0) {
		printf("Upgrade failed, tar reported error status\n");
		return 1;
	}

	versionfd = fopen(versionfn, "w");
	if (versionfd) {
		fprintf(versionfd, "%s", newversion);
		fclose(versionfd);
	}

	return 0;
}

