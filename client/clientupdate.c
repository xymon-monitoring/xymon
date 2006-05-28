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

static char rcsid[] = "$Id: clientupdate.c,v 1.3 2006-05-28 13:36:01 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include "libbbgen.h"

#define CLIENTVERSIONFILE "etc/clientversion.cfg"
#define INPROGRESSFILE "tmp/.inprogress.update"


#ifdef HPUX

void drop_root(uid_t myuid)
{
	setresuid(-1, myuid, -1);
}

void get_root(void)
{
	setresuid(-1, 0, -1);
}

#else

void drop_root(uid_t myuid)
{
	seteuid(myuid);
}

void get_root(void)
{
	seteuid(0);
}

#endif


int main(int argc, char *argv[])
{
	int argi;
	char *versionfn, *inprogressfn;
	FILE *versionfd, *tarpipefd;
	char version[10];
	char *newversion = NULL;
	char *newverreq;
	char *updateparam = NULL;
	int  removeself = 0;
	uid_t myuid;

	/* Immediately drop all root privs, we'll regain them later when needed */
	myuid = getuid();
	drop_root(myuid);

	versionfn = (char *)malloc(strlen(xgetenv("BBHOME")) + strlen(CLIENTVERSIONFILE) + 2);
	sprintf(versionfn, "%s/%s", xgetenv("BBHOME"), CLIENTVERSIONFILE);
	inprogressfn = (char *)malloc(strlen(xgetenv("BBHOME")) + strlen(INPROGRESSFILE) + 2);
	sprintf(inprogressfn, "%s/%s", xgetenv("BBHOME"), INPROGRESSFILE);

	versionfd = fopen(versionfn, "r");
	if (versionfd) {
		char *p;
		fgets(version, sizeof(version), versionfd);
		p = strchr(version, '\n'); if (p) *p = '\0';
		fclose(versionfd);
	}
	else *version = '\0';

	if (chdir(xgetenv("BBHOME")) != 0) {
		printf("Cannot chdir to BBHOME\n");
		return 1;
	}

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--level") == 0) {
			/* For checking what version we're at */
			printf("%s\n", version);
			return 0;
		}
		else if (strcmp(argv[argi], "--reexec") == 0) {
			/*
			 * First step of the update procedure.
			 *
			 * To avoid problems with unpacking a new clientupdate
			 * on top of the running one (some tar's will abort
			 * if they try this), copy ourself to a temp. file and
			 * re-exec it to carry out the update.
			 */
			char tmpfn[PATH_MAX];
			char *srcfn;
			FILE *tmpfd, *srcfd;
			unsigned char buf[8192];
			long n;
			char *newcmd;
			struct stat st;
			int cperr;

			if (!updateparam) {
				printf("clientupdate --reexec called with no update version\n");
				return 1;
			}

			if ( (stat(inprogressfn, &st) == 0) && ((time(NULL) - st.st_mtime) < 3600) ) {
				printf("Found update in progress or failed update (started %ld minutes ago)\n",
					(long) (time(NULL)-st.st_mtime)/60);
				return 1;
			}
			unlink(inprogressfn);
			tmpfd = fopen(inprogressfn, "w"); if (tmpfd) fclose(tmpfd);

			/* Copy the executable */
			srcfn = argv[0];
			srcfd = fopen(srcfn, "r"); cperr = errno;

			sprintf(tmpfn, "%s/.update.%s.%ld.tmp", 
				xgetenv("BBTMP"), xgetenv("MACHINEDOTS"), (long)time(NULL));
			unlink(tmpfn);	/* To avoid symlink attacks */
			if (srcfd) { tmpfd = fopen(tmpfn, "w"); cperr = errno; }

			if (!srcfd || !tmpfd) {
				printf("Cannot copy executable: %s\n", strerror(cperr));
				return 1;
			}

			while ((n = fread(buf, 1, sizeof(buf), srcfd)) > 0) fwrite(buf, 1, n, tmpfd);
			fclose(srcfd); fclose(tmpfd);

			/* Make sure the temp. binary has execute permissions set */
			chmod(tmpfn, S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);

			/*
			 * Set the temp. executable suid-root, and exec() it.
			 * If get_root() fails (because clientupdate was installed
			 * without suid-root privs), just carry on and do what we
			 * can without root privs. (It basically just means that
			 * logfetch() and clientupdate() will continue to run without
			 * root privs).
			 */
			get_root();
			chown(tmpfn, 0, getgid());
			chmod(tmpfn, S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
			drop_root(myuid);

			/* Run the temp. executable */
			execl(tmpfn, tmpfn, updateparam, "--remove-self", (char *)NULL);

			/* We should never go here */
			printf("exec() failed to launch update: %s\n", strerror(errno));
			return 1;
		}

		else if (strncmp(argv[argi], "--update=", 9) == 0) {
			newversion = strdup(argv[argi]+9);
			updateparam = argv[argi];
		}
		else if (strcmp(argv[argi], "--remove-self") == 0) {
			removeself = 1;
		}

		else if (strcmp(argv[argi], "--suid-setup") == 0) {
			/*
			 * Final step of the update procedure.
			 *
			 * Become root to setup suid-root privs on utils that need it.
			 * Note: If get_root() fails, we're left with normal user privileges. That is
			 * OK, because that is how the client was installed originally, then.
			 */
			get_root();
			chown("bin/logfetch", 0, getgid());
			chmod("bin/logfetch", S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
			drop_root(myuid);

			return 0;
		}
	}

	if (!newversion) return 1;

	/* Update to version "newversion" */
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

	/* Create the new version file */
	versionfd = fopen(versionfn, "w");
	if (versionfd) {
		fprintf(versionfd, "%s", newversion);
		fclose(versionfd);
	}

	/* Make sure these have execute permissions */
	chmod("bin/hobbitclient.sh", S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
	chmod("bin/clientupdate", S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);

	/*
	 * Become root to setup suid-root privs on the new clientupdate util.
	 * Note: If get_root() fails, we're left with normal user privileges. That is
	 * OK, because that is how the client was installed originally, then.
	 */
	get_root();
	chown("bin/clientupdate", 0, getgid());
	chmod("bin/clientupdate", S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
	drop_root(myuid);

	/* Remove temporary- and lock-files */
	unlink(inprogressfn);
	if (removeself) unlink(argv[0]);

	/*
	 * Exec the new client-update utility to fix suid-root permissions on
	 * the new files.
	 */
	execl("bin/clientupdate", "bin/clientupdate", "--suid-setup", (char *)NULL);

	/* We should never go here */
	printf("exec() of clientupdate --suid-setup failed: %s\n", strerror(errno));

	return 0;
}

