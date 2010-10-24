/*----------------------------------------------------------------------------*/
/* Xymon client update tool.                                                  */
/*                                                                            */
/* This tool is used to fetch the current client version from the config-file */
/* saved in etc/clientversion.cfg. The client script compares this with the   */
/* current version on the server, and if they do not match then this utility  */
/* is run to fetch the new version from the server and unpack it via "tar".   */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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

void cleanup(char *inprogressfn, char *selffn)
{
	/* Remove temporary- and lock-files */
	unlink(inprogressfn);
	if (selffn) unlink(selffn);
}

int main(int argc, char *argv[])
{
	int argi;
	char *versionfn, *inprogressfn;
	FILE *versionfd, *tarpipefd;
	char version[1024];
	char *newversion = NULL;
	char *newverreq;
	char *updateparam = NULL;
	int  removeself = 0;
	int bbstat = 0;
	sendreturn_t *sres;

#ifdef BIG_SECURITY_HOLE
	/* Immediately drop all root privs, we'll regain them later when needed */
	drop_root();
#else
	/* We WILL not run as suid-root. */
	drop_root_and_removesuid(argv[0]);
#endif

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
	else {
		*version = '\0';
	}

	if (chdir(xgetenv("BBHOME")) != 0) {
		errprintf("Cannot chdir to BBHOME\n");
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
			struct stat st;
			int cperr;

			if (!updateparam) {
				errprintf("clientupdate --reexec called with no update version\n");
				return 1;
			}

			if ( (stat(inprogressfn, &st) == 0) && ((getcurrenttime(NULL) - st.st_mtime) < 3600) ) {
				errprintf("Found update in progress or failed update (started %ld minutes ago)\n",
					(long) (getcurrenttime(NULL)-st.st_mtime)/60);
				return 1;
			}
			unlink(inprogressfn);
			tmpfd = fopen(inprogressfn, "w"); if (tmpfd) fclose(tmpfd);

			/* Copy the executable */
			srcfn = argv[0];
			srcfd = fopen(srcfn, "r"); cperr = errno;

			sprintf(tmpfn, "%s/.update.%s.%ld.tmp", 
				xgetenv("BBTMP"), xgetenv("MACHINEDOTS"), (long)getcurrenttime(NULL));

			dbgprintf("Starting update by copying %s to %s\n", srcfn, tmpfn);

			unlink(tmpfn);	/* To avoid symlink attacks */
			if (srcfd) { tmpfd = fopen(tmpfn, "w"); cperr = errno; }

			if (!srcfd || !tmpfd) {
				errprintf("Cannot copy executable: %s\n", strerror(cperr));
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
#ifdef BIG_SECURITY_HOLE
			get_root();
			chown(tmpfn, 0, getgid());
			chmod(tmpfn, S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
			drop_root();
#endif

			/* Run the temp. executable */
			dbgprintf("Running command '%s %s ..remove-self'\n", tmpfn, updateparam);
			execl(tmpfn, tmpfn, updateparam, "--remove-self", (char *)NULL);

			/* We should never go here */
			errprintf("exec() failed to launch update: %s\n", strerror(errno));
			return 1;
		}

		else if (strncmp(argv[argi], "--update=", 9) == 0) {
			newversion = strdup(argv[argi]+9);
			updateparam = argv[argi];
		}
		else if (strcmp(argv[argi], "--remove-self") == 0) {
			removeself = 1;
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}

		else if (strcmp(argv[argi], "--suid-setup") == 0) {
			/*
			 * Final step of the update procedure.
			 *
			 * Become root to setup suid-root privs on utils that need it.
			 * Note: If get_root() fails, we're left with normal user privileges. That is
			 * OK, because that is how the client was installed originally, then.
			 */
#ifdef BIG_SECURITY_HOLE
			get_root();
			chown("bin/logfetch", 0, getgid());
			chmod("bin/logfetch", S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
			drop_root();
#endif

			return 0;
		}
	}

	if (!newversion) {
		errprintf("No new version string!\n");
		cleanup(inprogressfn, (removeself ? argv[0] : NULL));
		return 1;
	}

	/* Update to version "newversion" */
	dbgprintf("Opening pipe to 'tar'\n");
	tarpipefd = popen("tar xf -", "w");
	if (tarpipefd == NULL) {
		errprintf("Cannot launch 'tar xf -': %s\n", strerror(errno));
		cleanup(inprogressfn, (removeself ? argv[0] : NULL));
		return 1;
	}

	sres = newsendreturnbuf(1, tarpipefd);
	newverreq = (char *)malloc(100+strlen(newversion));
	sprintf(newverreq, "download %s.tar", newversion);
	dbgprintf("Sending command to Xymon: %s\n", newverreq);
	if ((bbstat = sendmessage(newverreq, NULL, BBTALK_TIMEOUT, sres)) != BB_OK) {
		errprintf("Cannot fetch new client tarfile: Status %d\n", bbstat);
		cleanup(inprogressfn, (removeself ? argv[0] : NULL));
		freesendreturnbuf(sres);
		return 1;
	}
	else {
		dbgprintf("Download command completed OK\n");
		freesendreturnbuf(sres);
	}

	dbgprintf("Closing tar pipe\n");
	if ((bbstat = pclose(tarpipefd)) != 0) {
		errprintf("Upgrade failed, tar exited with status %d\n", bbstat);
		cleanup(inprogressfn, (removeself ? argv[0] : NULL));
		return 1;
	}
	else {
		dbgprintf("tar pipe exited with status 0 (OK)\n");
	}

	/* Create the new version file */
	dbgprintf("Creating new version file %s with version %s\n", versionfn, newversion);
	unlink(versionfn);
	versionfd = fopen(versionfn, "w");
	if (versionfd) {
		fprintf(versionfd, "%s", newversion);
		fclose(versionfd);
	}
	else {
		errprintf("Cannot create version file: %s\n", strerror(errno));
	}

	/* Make sure these have execute permissions */
	dbgprintf("Setting execute permissions on hobbitclient.sh and clientupdate tools\n");
	chmod("bin/hobbitclient.sh", S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
	chmod("bin/clientupdate", S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);

	/*
	 * Become root to setup suid-root privs on the new clientupdate util.
	 * Note: If get_root() fails, we're left with normal user privileges. That is
	 * OK, because that is how the client was installed originally, then.
	 */
#ifdef BIG_SECURITY_HOLE
	get_root();
	chown("bin/clientupdate", 0, getgid());
	chmod("bin/clientupdate", S_ISUID|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP);
	drop_root();
#endif

	dbgprintf("Cleaning up after update\n");
	cleanup(inprogressfn, (removeself ? argv[0] : NULL));

	/*
	 * Exec the new client-update utility to fix suid-root permissions on
	 * the new files.
	 */
	execl("bin/clientupdate", "bin/clientupdate", "--suid-setup", (char *)NULL);

	/* We should never go here */
	errprintf("exec() of clientupdate --suid-setup failed: %s\n", strerror(errno));

	return 0;
}

