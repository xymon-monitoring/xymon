/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This is a xymond worker module, it should be run off xymond_channel.       */
/*                                                                            */
/* This module implements the traditional Big Brother filebased storage of    */
/* incoming status messages to the $XYMONVAR/logs/, $XYMONVAR/data/,          */
/* $XYMONWWWDIR/notes/ and $XYMONVAR/disabled/ directories.                   */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#include "libxymon.h"

/* O_NOFOLLOW is POSIX.1-2008; the temp-file writers below rely on it to refuse
 * a planted symlink. On a target whose <fcntl.h> predates it (the legacy
 * Makefile.* platforms), fall back to 0 so the tree still builds -- the open
 * then follows a symlink there, the same exposure as before this hardening,
 * rather than failing to compile. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#include "xymond_worker.h"

static char *multigraphs = ",disk,inode,qtree,quotas,snapshot,TblSpace,if_load,";
static int locatorbased = 0;

enum role_t { ROLE_STATUS, ROLE_DATA, ROLE_NOTES, ROLE_ENADIS};

/*
 * A path component is refused outright, never reduced with basename(): the
 * reduction maps different wire names onto one file, so "a/victim" replaced
 * the note, log and page of "victim" (measured). Both '/' and '\\' are refused
 * - '\\' separates directories on an SMB/CIFS $XYMONVAR, and xymond's
 * new-testname charset admits it. Refusal also retires basename(3)'s trap: on
 * NetBSD/OpenBSD its static storage once stored host "realhost" test "conn" as
 * conn.conn.
 *
 * "", and any leading-dot name, are refused too. "" lands a note on dir
 * itself; "." and ".." are dir and its parent; a leading dot collides with the
 * temp namespace - a stored ".victim" is the temp name update_file() derives
 * for "victim", whose unlink() then destroys it (measured).
 */
static int name_confined(char *dir, char *component)
{
	if (component[0] == '\0') {
		errprintf("Empty name under %s, refusing it\n", dir);
		return 0;
	}
	if (component[0] == '.') {
		errprintf("Name '%s' under %s begins with a dot, refusing it\n", component, dir);
		return 0;
	}
	if ((strchr(component, '/') == NULL) && (strchr(component, '\\') == NULL)) return 1;
	errprintf("Name '%s' under %s holds a path separator, refusing it\n", component, dir);
	return 0;
}

/*
 * Build "<dir>/<host>.<test>[.<suffix>]", both components confined. Neither
 * arrives validated. With '/' refused, a "." or ".." residue cannot climb
 * out of dir: the component is always followed by "." and more text - not
 * true of xymond_rrd, where it names a directory to delete recursively (#287).
 */
static void build_logfn(char *buf, size_t bufsz, char *dir, char *host, char *test, char *suffix)
{
	int n;

	if (!name_confined(dir, host) || !name_confined(dir, test)) {
		buf[0] = '\0';
		return;
	}

	if (suffix)
		n = snprintf(buf, bufsz, "%s/%s.%s.%s", dir, host, test, suffix);
	else
		n = snprintf(buf, bufsz, "%s/%s.%s", dir, host, test);

	/*
	 * A truncated name is not this file's name - it can just as well be
	 * another host's or another test's - so nothing is written rather than
	 * the wrong file. The empty path then fails at open(), which every caller
	 * already reports.
	 */
	/*
	 * An empty buffer is the refusal, and every caller checks it. Leaving it
	 * to the open() or unlink() that follows works only because "" happens
	 * not to name anything - which is a property of those two calls, not of
	 * this contract, and the htmlfile caller proved it: there the empty name
	 * became ".tmp" in the worker's cwd.
	 */
	if ((n < 0) || ((size_t)n >= bufsz)) {
		errprintf("Filename for %s/%s under %s does not fit, not writing it\n", host, test, dir);
		buf[0] = '\0';
	}
}

void update_file(char *fn, char *mode, char *msg, time_t expire, char *sender, time_t timesincechange, int seq)
{
	FILE *logfd;
	char tmpfn[PATH_MAX];
	char *p;
	int n;
	int writefailed = 0;

	MEMDEFINE(tmpfn);

	dbgprintf("Updating seq %d file %s\n", seq, fn);

	/*
	 * One byte tighter than the destination: tmpfn is fn with a '.' inserted,
	 * so a name that fits fn exactly does not fit here - where an unbounded
	 * sprintf() wrote past this buffer, reached through an unvalidated notes
	 * ID (@SoundGoof, under ASan).
	 */
	p = strrchr(fn, '/');
	if (p) {
		*p = '\0';
		n = snprintf(tmpfn, sizeof(tmpfn), "%s/.%s", fn, p+1);
		*p = '/';
	}
	else {
		n = snprintf(tmpfn, sizeof(tmpfn), ".%s", fn);
	}
	if ((n < 0) || ((size_t)n >= sizeof(tmpfn))) {
		errprintf("Temporary filename for %s does not fit, not writing it\n", fn);
		MEMUNDEFINE(tmpfn);
		return;
	}

	/*
	 * The temp name is predictable, so whatever already sits there was put
	 * there by someone else: a symlink or a hardlink planted to have the
	 * daemon write through it, or a temp file a crashed run left behind.
	 * unlink() it, then create it with O_EXCL, so what we open is always a
	 * file we just made. O_NOFOLLOW alone caught the symlink but not the
	 * hardlink - a hardlinked temp still overwrote the file it pointed at -
	 * and reusing a leftover in append mode published its stale contents
	 * ahead of ours. O_EXCL also loses the race where the name reappears
	 * between the unlink and the open: the create fails and nothing is
	 * written, rather than writing through the planted file.
	 */
	{
		int fd, flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW;

		unlink(tmpfn);
		fd = open(tmpfn, flags, 0666);
		logfd = (fd == -1) ? NULL : fdopen(fd, mode);
		/* Preserve fdopen()'s errno across the cleanup: close()/unlink() may
		   overwrite it, and it is what the diagnostic below reports. */
		if ((fd != -1) && !logfd) { int e = errno; close(fd); unlink(tmpfn); errno = e; }
	}
	if (!logfd) {
		errprintf("Could not open '%s' (for %s), mode %s: %s\n", tmpfn, fn, mode, strerror(errno));
		MEMUNDEFINE(tmpfn);
		return;
	}
	if (strlen(msg) && (fwrite(msg, strlen(msg), 1, logfd) != 1)) {
		errprintf("Write to '%s' (for %s) failed: %s\n", tmpfn, fn, strerror(errno));
		writefailed = 1;
	}
	if (sender && (fprintf(logfd, "\n\nMessage received from %s\n", sender) < 0)) writefailed = 1;
	if (timesincechange >= 0) {
		char timestr[100];
		char *p = timestr;
		if (timesincechange > 86400) p += sprintf(p, "%ld days, ", (timesincechange / 86400));
		p += sprintf(p, "%ld hours, %ld minutes",
				((timesincechange % 86400) / 3600), ((timesincechange % 3600) / 60));
		if (fprintf(logfd, "Status unchanged in %s\n", timestr) < 0) writefailed = 1;
	}
	/*
	 * The stream's own error flag as well, while it still exists: a write can
	 * fail without the call that failed being the one whose return value was
	 * examined, and this catches the rest.
	 */
	if (ferror(logfd)) writefailed = 1;
	/*
	 * Buffered data is flushed at close, so a full filesystem surfaces
	 * here and nowhere else - an unchecked fclose() loses the write in
	 * silence.
	 */
	if (fclose(logfd) != 0) {
		errprintf("Write to '%s' (for %s) failed at close: %s\n", tmpfn, fn, strerror(errno));
		writefailed = 1;
	}
	/*
	 * A failed write used to be logged and then ignored, and the rename below
	 * put the fragment in place of the file that was there before: with a
	 * write limit in the way, an existing note was replaced by a truncated
	 * one. Nothing is written at all rather than something incomplete - the
	 * previous contents stay, and the next update replaces them properly.
	 */
	if (writefailed) {
		errprintf("Not replacing '%s': '%s' was not written completely\n", fn, tmpfn);
		remove(tmpfn);
		MEMUNDEFINE(tmpfn);
		return;
	}

	if (expire) {
		struct utimbuf logtime;

		/* This stamp is the expiry the readers go by, so a failure here
		   publishes a file that lies about when it goes stale. */
		logtime.actime = logtime.modtime = expire;
		if (utime(tmpfn, &logtime) != 0)
			errprintf("Cannot set the expiry time on %s: %s\n", tmpfn, strerror(errno));
	}

	/* Without this the temp file is orphaned and the real one never updated. */
	if (rename(tmpfn, fn) != 0) {
		errprintf("Could not rename '%s' to '%s': %s\n", tmpfn, fn, strerror(errno));
		remove(tmpfn);
	}

	MEMUNDEFINE(tmpfn);
}

void update_htmlfile(char *fn, char *msg, 
		     char *hostname, char *service, int color, int flapping,
		     char *sender, char *flags,
		     time_t logtime, time_t timesincechange,
		     time_t acktime, char *ackmsg,
                     time_t disabletime, char *dismsg)
{
	FILE *output;
	char *tmpfn;
	char *firstline, *restofmsg;
	char *displayname = hostname;
	char *ip = "";
	char timestr[100];
	int writefailed = 0;

	MEMDEFINE(timestr);

	tmpfn = (char *) malloc(strlen(fn)+5);
	if (!tmpfn) {
		errprintf("Out of memory for the HTML temp name of %s, not writing it\n", fn);
		MEMUNDEFINE(timestr);
		return;
	}
	sprintf(tmpfn, "%s.tmp", fn);

	/* Same reasoning as update_file(): "<destination>.tmp" is predictable, so
	   anyone able to create a file in this directory can leave a symlink or a
	   hardlink there and have the daemon write through it. unlink() then
	   O_EXCL, so the page is always written to a file we just made. */
	{
		int fd;

		unlink(tmpfn);
		fd = open(tmpfn, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
		output = (fd == -1) ? NULL : fdopen(fd, "w");
		/* Keep fdopen()'s errno; close()/unlink() below can clobber it. */
		if ((fd != -1) && !output) { int e = errno; close(fd); unlink(tmpfn); errno = e; }
	}
	if (!output) errprintf("Cannot create HTML file %s: %s\n", tmpfn, strerror(errno));

	if (output) {
		firstline = msg;
		restofmsg = strchr(msg, '\n');
		if (restofmsg) {
			*restofmsg = '\0';
			restofmsg++;
		}
		else {
			restofmsg = "";
		}

		if (timesincechange >= 0) {
			char *p = timestr;
			if (timesincechange > 86400) p += sprintf(p, "%ld days, ", (timesincechange / 86400));
			p += sprintf(p, "%ld hours, %ld minutes",
					((timesincechange % 86400) / 3600), ((timesincechange % 3600) / 60));
		}
		else {
			/* generate_html_log() prints timestr when non-empty; a negative
			   delta (changetime past logtime, e.g. a clock step) skips the
			   block above, so terminate it or the page carries stack bytes. */
			timestr[0] = '\0';
		}

		generate_html_log(hostname, displayname, service, ip,
			color, flapping, sender, flags,
			logtime, timestr,
			firstline, restofmsg, NULL,
			acktime, ackmsg, NULL,
			disabletime, dismsg,
			0, 1, 0, locatorbased, multigraphs, NULL, 
			NULL, NULL, NULL,
			0,
			output);

		/* A page that was not written whole must not replace the one that is
		   there: the failure shows up at fclose() when the filesystem is
		   full, and renaming regardless publishes the fragment. */
		if (ferror(output)) writefailed = 1;
		if (fclose(output) != 0) writefailed = 1;

		if (writefailed) {
			/* No strerror(): the failure may have been seen through
			   ferror(), which does not set errno, so a captured errno here
			   would name an unrelated call. */
			errprintf("Cannot write HTML file %s: the page was not written whole\n", tmpfn);
			unlink(tmpfn);
		}
		else if (rename(tmpfn, fn) != 0) {
			errprintf("Cannot rename %s to %s: %s\n", tmpfn, fn, strerror(errno));
			unlink(tmpfn);
		}
	}

	xfree(tmpfn);
	MEMUNDEFINE(timestr);
}

void update_enable(char *fn, time_t expiretime)
{
	time_t now = getcurrenttime(NULL);

	dbgprintf("Enable/disable file %s, time %d\n", fn, (int)expiretime);

	if (expiretime <= now) {
		if (unlink(fn) != 0) {
			errprintf("Could not remove disable-file '%s':%s\n", fn, strerror(errno));
		}
	}
	else {
		int fd, n;
		struct utimbuf logtime;
		char tmpfn[PATH_MAX];
		char *p;

		/* The third writer with the same shape, but written to a temp and
		   renamed, as update_file() is. A symlink or a hardlink planted at
		   the name would otherwise be written through; the temp is unlinked
		   and created with O_EXCL, so it is always one we made. Renaming it
		   over the real name is atomic - unlinking the real name in place, as
		   an earlier round did, left a window where the file was absent and
		   the host read as not-disabled, which O_TRUNC never did. The file is
		   empty and carries the expiry as its mtime, so the stamp goes on the
		   temp before the rename; a failure anywhere drops the temp and leaves
		   the existing disable-file untouched. */
		p = strrchr(fn, '/');
		if (p) {
			*p = '\0';
			n = snprintf(tmpfn, sizeof(tmpfn), "%s/.%s", fn, p+1);
			*p = '/';
		}
		else {
			n = snprintf(tmpfn, sizeof(tmpfn), ".%s", fn);
		}
		if ((n < 0) || ((size_t)n >= sizeof(tmpfn))) {
			errprintf("Temporary disable-file name for %s does not fit, not writing it\n", fn);
			return;
		}

		unlink(tmpfn);
		fd = open(tmpfn, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
		if (fd == -1) {
			errprintf("Could not create disable-file '%s': %s\n", tmpfn, strerror(errno));
			return;
		}
		if (close(fd) != 0) {
			errprintf("Could not write disable-file '%s': %s\n", tmpfn, strerror(errno));
			unlink(tmpfn);
			return;
		}

		logtime.actime = logtime.modtime = expiretime;
		if (utime(tmpfn, &logtime) != 0) {
			errprintf("Could not set the expiry time on '%s': %s\n", tmpfn, strerror(errno));
			unlink(tmpfn);
			return;
		}

		if (rename(tmpfn, fn) != 0) {
			errprintf("Could not rename '%s' to '%s': %s\n", tmpfn, fn, strerror(errno));
			unlink(tmpfn);
		}
	}
}

static int wantedtest(char *wanted, char *key)
{
	char *p, *ckey;

	if (wanted == NULL) return 1;

	ckey = (char *)malloc(strlen(key) + 3);
	sprintf(ckey, ",%s,", key);
	p = strstr(wanted, ckey);
	xfree(ckey);

	dbgprintf("wantedtest: Found '%s' at '%s'\n", key, (p ? p : "<not found>"));

	return (p != NULL);
}

int main(int argc, char *argv[])
{
	char *filedir = NULL;
	char *htmldir = NULL;
	char *htmlextension = "html";
	char *onlytests = NULL;
	char *msg;
	enum role_t role = ROLE_STATUS;
	enum msgchannels_t chnid = C_STATUS;
	int argi;
	int seq;
	int running = 1;

	/* Don't save the error buffer */
	save_errbuf = 0;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--status") == 0) {
			role = ROLE_STATUS;
			chnid = C_STATUS;
			if (!filedir) filedir = xgetenv("XYMONRAWSTATUSDIR");
		}
		else if (strcmp(argv[argi], "--html") == 0) {
			role = ROLE_STATUS;
			chnid = C_STATUS;
			if (!htmldir) htmldir = xgetenv("XYMONHTMLSTATUSDIR");
		}
		else if (strcmp(argv[argi], "--data") == 0) {
			role = ROLE_DATA;
			chnid = C_DATA;
			if (!filedir) filedir = xgetenv("XYMONDATADIR");
		}
		else if (strcmp(argv[argi], "--notes") == 0) {
			role = ROLE_NOTES;
			chnid = C_NOTES;
			if (!filedir) filedir = xgetenv("XYMONNOTESDIR");
		}
		else if (strcmp(argv[argi], "--enadis") == 0) {
			role = ROLE_ENADIS;
			chnid = C_ENADIS;
			if (!filedir) filedir = xgetenv("XYMONDISABLEDDIR");
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--dir=")) {
			filedir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--htmldir=")) {
			htmldir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--htmlext=")) {
			htmlextension = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--only=")) {
			char *p = strchr(argv[argi], '=') + 1;
			onlytests = (char *)malloc(3 + strlen(p));
			sprintf(onlytests, ",%s,", p);
		}
		else if (argnmatch(argv[argi], "--multigraphs=")) {
			char *p = strchr(argv[argi], '=');
			multigraphs = (char *)malloc(strlen(p+1) + 3);
			sprintf(multigraphs, ",%s,", p+1);
		}
		else if (argnmatch(argv[argi], "--locator=")) {
			char *p = strchr(argv[argi], '=');
			locator_init(p+1);
			locatorbased = 1;
		}
	}

	if (filedir == NULL) {
		errprintf("No directory given, aborting\n");
		return 1;
	}

	/* For picking up lost children */
	setup_signalhandler("xymond_filestore");
	signal(SIGPIPE, SIG_DFL);

	dbgprintf("Storing tests: %s\n", (onlytests ? onlytests : "<all>"));

	while (running) {
		char *metadata[20] = { NULL, };
		char *statusdata = "";
		char *p;
		int metacount;
		char *hostname, *testname;
		time_t expiretime = 0;
		char logfn[PATH_MAX];

		MEMDEFINE(logfn);

		msg = get_xymond_message(chnid, "filestore", &seq, NULL);
		if (msg == NULL) {
			running = 0;
			MEMUNDEFINE(logfn);
			continue;
		}

		p = strchr(msg, '\n'); 
		if (p) {
			*p = '\0'; 
			statusdata = p+1;
		}

		metacount = 0;
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < 20)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}

		if ((role == ROLE_STATUS) && (metacount >= 14) && (strncmp(metadata[0], "@@status", 8) == 0)) {
			/* @@status|timestamp|sender|origin|hostname|testname|expiretime|color|testflags|prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage|clientmsgtstamp|flapping */
			int ltime, flapping = 0;
			time_t logtime = 0, timesincechange = 0, acktime = 0, disabletime = 0;

			hostname = metadata[4];
			testname = metadata[5];
			if (!wantedtest(onlytests, testname)) {
				dbgprintf("Status dropped - not wanted\n");
				MEMUNDEFINE(logfn);
				continue;
			}

			build_logfn(logfn, sizeof(logfn), filedir, commafy(hostname), testname, NULL);
			expiretime = atoi(metadata[6]);
			statusdata = msg_data(statusdata, 0);
			sscanf(metadata[1], "%d.%*d", &ltime); logtime = ltime;
			timesincechange = logtime - atoi(metadata[10]);
			if (logfn[0]) update_file(logfn, "w", statusdata, expiretime, metadata[2], timesincechange, seq);
			if (htmldir) {
				char *ackmsg = NULL;
				char *dismsg = NULL;
				char htmllogfn[PATH_MAX];

				MEMDEFINE(htmllogfn);

				if (metadata[11]) acktime = atoi(metadata[11]);
				if (metadata[12] && strlen(metadata[12])) ackmsg = metadata[12];
				if (ackmsg) nldecode(ackmsg);

				if (metadata[13]) disabletime = atoi(metadata[13]);
				if (metadata[14] && strlen(metadata[14]) && (disabletime > 0)) dismsg = metadata[14];
				if (dismsg) nldecode(dismsg);

				flapping = (metadata[16] ? (*metadata[16] == '1') : 0);

				/*
				 * Raw, a '/' here writes outside htmldir; build_logfn()
				 * refuses it.
				 */
				/* An empty name is build_logfn()'s refusal. Handing it on
				   builds ".tmp" and writes it in whatever directory the
				   worker happens to be running from. */
				build_logfn(htmllogfn, sizeof(htmllogfn), htmldir, hostname, testname, htmlextension);
				if (htmllogfn[0]) {
					update_htmlfile(htmllogfn, statusdata, hostname, testname, parse_color(metadata[7]), flapping,
							     metadata[2], metadata[8], logtime, timesincechange,
							     acktime, ackmsg,
							     disabletime, dismsg);
				}

				MEMUNDEFINE(htmllogfn);
			}
		}
		else if ((role == ROLE_DATA) && (metacount > 5) && (strncmp(metadata[0], "@@data", 6) == 0)) {
			/* @@data|timestamp|sender|hostname|testname */
			p = hostname = metadata[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[5];
			if (!wantedtest(onlytests, testname)) {
				dbgprintf("data dropped - not wanted\n");
				MEMUNDEFINE(logfn);
				continue;
			}

			statusdata = msg_data(statusdata, 0); if (*statusdata == '\n') statusdata++;
			build_logfn(logfn, sizeof(logfn), filedir, hostname, testname, NULL);
			expiretime = 0;
			if (logfn[0]) update_file(logfn, "a", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_NOTES) && (metacount > 3) && (strncmp(metadata[0], "@@notes", 7) == 0)) {
			/* @@notes|timestamp|sender|hostname */
			hostname = metadata[3];
			statusdata = msg_data(statusdata, 0); if (*statusdata == '\n') statusdata++;
			/*
			 * xymond does not validate the notes ID, so never use it as a raw
			 * path component: a separator is refused, not reduced; filedir is
			 * the configured absolute destination. name_confined() bounds the
			 * name, not its bytes - filedir is under the web root, so serve it
			 * with SSI off (Apache IncludesNOEXEC): the ID chooses the extension.
			 */
			{
				int n;

				if (!name_confined(filedir, hostname)) {
					logfn[0] = '\0';
				}
				else {
					n = snprintf(logfn, sizeof(logfn), "%s/%s", filedir, hostname);
					if ((n < 0) || ((size_t)n >= sizeof(logfn))) {
						errprintf("Filename for note %s under %s does not fit, not writing it\n",
							  hostname, filedir);
						logfn[0] = '\0';
					}
				}
			}
			expiretime = 0;
			if (logfn[0]) update_file(logfn, "w", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_ENADIS) && (metacount > 5) && (strncmp(metadata[0], "@@enadis", 8) == 0)) {
			/* @@enadis|timestamp|sender|hostname|testname|expiretime */
			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			expiretime = atoi(metadata[5]);
			build_logfn(logfn, sizeof(logfn), filedir, hostname, testname, NULL);
			if (logfn[0]) update_enable(logfn, expiretime);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = malloc(strlen(hostname) + 2);
			strcpy(hostlead, hostname); strcat(hostlead, ".");

			/* The lead is a prefix match, not a path through build_logfn(), so
			   it carries its own guard. An empty host - reachable when the
			   field is not the last, "@@drophost|..||extra" - makes the lead
			   "." and matches the ".host.test" temp files and dot entries. The
			   dot-to-comma pass above already turns a "." or ".." host into
			   commas, and '/' cannot appear in a readdir() name, so the empty
			   case is the one left to stop. */
			dirfd = name_confined(filedir, hostname) ? opendir(filedir) : NULL;
			if (dirfd) {
				while ( (de = readdir(dirfd)) != NULL) {
					if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
						/* Cast to size_t so a negative snprintf() (an encoding
						   error) reads as huge and fails here too, not just
						   truncation - as build_logfn() checks it. */
						if ((size_t)snprintf(logfn, sizeof(logfn), "%s/%s", filedir, de->d_name) >= sizeof(logfn)) {
							errprintf("Filename for %s under %s does not fit, not removing it\n",
								  de->d_name, filedir);
							continue;
						}
						unlink(logfn);
					}
				}
				closedir(dirfd);
			}

			xfree(hostlead);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			/* @@droptest|timestamp|sender|hostname|testname */
			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			build_logfn(logfn, sizeof(logfn), filedir, hostname, testname, NULL);
			if (logfn[0]) unlink(logfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;
			char *newhostname;
			char newlogfn[PATH_MAX];

			MEMDEFINE(newlogfn);

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = malloc(strlen(hostname) + 2);
			strcpy(hostlead, hostname); strcat(hostlead, ".");
			p = newhostname = metadata[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';

			/* Both names come off the channel unchecked. The dot-to-comma
			   pass above stops "..", not "/": "link/moved" renamed the log
			   through a symlinked directory and out of filedir, and reducing
			   it instead renamed onto another host's log (both measured). The
			   source guards its match-lead as drophost does - an empty host
			   would rename the dot entries it matches. */
			dirfd = (name_confined(filedir, hostname) && name_confined(filedir, newhostname))
				? opendir(filedir) : NULL;
			if (dirfd) {
				while ( (de = readdir(dirfd)) != NULL) {
					if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
						char *testname = strchr(de->d_name, '.');

						if (((size_t)snprintf(logfn, sizeof(logfn), "%s/%s", filedir, de->d_name) >= sizeof(logfn)) ||
						    ((size_t)snprintf(newlogfn, sizeof(newlogfn), "%s/%s%s", filedir, newhostname, testname) >= sizeof(newlogfn))) {
							errprintf("Cannot rename %s under %s: the name does not fit\n",
								  de->d_name, filedir);
							continue;
						}
						if (rename(logfn, newlogfn) != 0) {
							errprintf("Cannot rename %s to %s: %s\n", logfn, newlogfn, strerror(errno));
						}
					}
				}
				closedir(dirfd);
			}
			xfree(hostlead);

			MEMUNDEFINE(newlogfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			char *newtestname;
			char newfn[PATH_MAX];

			MEMDEFINE(newfn);

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			newtestname = metadata[5];
			build_logfn(logfn, sizeof(logfn), filedir, hostname, testname, NULL);

			/*
			 * Both halves of the destination, not one: build_logfn() refuses
			 * a '/' in hostname (an empty logfn), and newtestname - the one
			 * field with no pass over it at all - is refused here. Measured,
			 * with logs/link a symlink: hostname "link/oldhost" wrote outside
			 * filedir even after newtestname was confined.
			 */
			if (!logfn[0] || !name_confined(filedir, newtestname) ||
			    ((size_t)snprintf(newfn, sizeof(newfn), "%s/%s.%s", filedir, hostname, newtestname) >= sizeof(newfn))) {
				errprintf("Cannot rename %s/%s to %s: the name was refused\n",
					  hostname, testname, newtestname);
			}
			else if (rename(logfn, newfn) != 0) {
				errprintf("Cannot rename %s to %s: %s\n", logfn, newfn, strerror(errno));
			}

			MEMUNDEFINE(newfn);
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Ignored */
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			/* Do nothing */
		}
		else {
			errprintf("Dropping message type %s, metacount=%d\n", metadata[0], metacount);
		}

		MEMUNDEFINE(logfn);
	}

	return 0;
}

