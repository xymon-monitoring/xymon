/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * build/install-safe.c -- install a file, or set the owner and mode of a name,
 * as root into a directory owned by an unprivileged user, without following a
 * symlink at the destination name and without a check-then-act race.
 *
 * make install runs as root and writes into directories later owned by the Xymon
 * user, who can plant a symlink at a destination name; a plain cp/chown/chmod/
 * touch then follows it and acts on the link's target. The shell install guarded
 * each call site by hand (rm -f before cp, -h on chown, test -L before chmod) in
 * several styles and still left a check-then-act window. This helper does each
 * operation once, safely and atomically, so a call site is one invocation.
 *
 * Usage (one verb per call):
 *   install-safe copy  -m MODE [-o OWNER] [-g GROUP] SRC DEST   install SRC as DEST
 *   install-safe mkdir [-m MODE] [-o OWNER] [-g GROUP] DIR      create DIR and parents; set owner/mode on creation
 *   install-safe touch [-m MODE] [-o OWNER] [-g GROUP] NAME     create NAME if absent, set owner/mode
 *   install-safe set   [-m MODE] [-o OWNER] [-g GROUP] NAME     set owner/mode of an existing NAME
 *
 * MODE is octal (required for copy); OWNER/GROUP a name or numeric id. A setuid/
 * setgid MODE is allowed only with copy and only when the file ends up root-owned
 * (an unprivileged owner could rewrite the content before the mode is set).
 *
 * How it is safe:
 *   copy       -- content goes to a fresh O_CREAT|O_EXCL|O_NOFOLLOW temp (random
 *                 name) in DEST's directory, owner then mode on the fd, fsync,
 *                 rename over DEST (atomic; replaces a symlink, follows nothing),
 *                 the result confirmed by fd to be the file just written.
 *   owner/mode -- lstat first: a symlink is left untouched (relocation preserved),
 *                 a non-plain-file (set) or non-directory (mkdir) name is refused
 *                 unopened; only then open O_NOFOLLOW and operate by fd, the fd
 *                 confirmed to be the object lstat saw.
 *
 * Residuals, inherent to acting on a name (not defects):
 *   - An *intermediate* directory in the path being an attacker symlink is not
 *     closed: the parent chain must be root-owned (the Xymon layout keeps its
 *     roots so). Closing it needs an openat component walk -- tracked as #102.
 *   - set/mkdir act on whatever holds the name when they run, so set never grants
 *     privilege; anything that must be root-owned is installed fresh, not adjusted.
 *   - SRC is read from the trusted build tree, its symlinks followed.
 *   - A setuid copy into a user-owned directory can be captured (renamed aside,
 *     the inode kept); the content is the trusted binary, so it grants no new code.
 */

/* Each Unix gates fchown, fchmod and SIGXFSZ behind a different feature-test
 * macro. A strict _POSIX_C_SOURCE can hide one or another of them on the BSDs;
 * AIX and HP-UX expose them only WITH their own extensions macro; Solaris needs
 * __EXTENSIONS__ and rejects a POSIX.1-2008 level on 10; glibc and macOS expose
 * them under _POSIX_C_SOURCE. */
#if defined(_AIX)
#  define _ALL_SOURCE
#elif defined(__hpux)
#  define _HPUX_SOURCE
#elif defined(__sun)
/* Solaris 10 supports only POSIX.1-2001 and its headers reject 200809L;
 * __EXTENSIONS__ exposes fchown/fchmod and O_NOFOLLOW across Solaris versions. */
#  define __EXTENSIONS__
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
/* The BSDs can hide fchown/fchmod or SIGXFSZ under a strict _POSIX_C_SOURCE;
 * clear one a caller's CFLAGS may have predefined, rather than only refraining
 * from it. */
#  undef _POSIX_C_SOURCE
#  undef _XOPEN_SOURCE
#else
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

#ifndef O_NOFOLLOW
#error "O_NOFOLLOW is required: without it this primitive cannot refuse a symlink at a destination name -- the lstat/fstat identity check alone does not, as a rename to the same inode defeats it"
#endif
/* Some AIX levels define O_NOFOLLOW wider than the int that open() takes, so the
 * flag is silently truncated and the open follows the link. Fail the build there
 * (a negative array size) rather than ship a primitive that does not refuse a
 * symlink; the APAR-fixed AIX headers, and every other platform, size it to fit. */
typedef char install_safe_O_NOFOLLOW_fits_in_int[(O_NOFOLLOW) == (int)(O_NOFOLLOW) ? 1 : -1];
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static const char *prog = "install-safe";

/* The temporary file in progress, so a signal can remove it. */
static char g_tmp[PATH_MAX + 48];
static volatile sig_atomic_t g_have_tmp = 0;

/* Fill buf with n hex digits of unpredictable data and a NUL, so the temporary
 * name cannot be guessed or pre-created by the directory's owner. /dev/urandom
 * where it is readable; otherwise the pid, which is weaker but not the bare name. */
static void rand_suffix(char *buf, int n)
{
	static const char hex[] = "0123456789abcdef";
	static unsigned long seq = 0;	/* so the fallback differs on each retry */
	unsigned char r[16];
	int i, got = 0, fd = open("/dev/urandom", O_RDONLY | O_NOCTTY);
	if (fd >= 0) { ssize_t k = read(fd, r, sizeof r); if (k > 0) got = (int)k; close(fd); }
	seq++;
	for (i = 0; i < n; i++) {
		unsigned char b = (i / 2 < got) ? r[i / 2]
		    : (unsigned char)((unsigned long)getpid() + seq * 2654435761UL + (unsigned long)i);
		buf[i] = hex[(i & 1) ? (b & 0xf) : ((b >> 4) & 0xf)];
	}
	buf[n] = '\0';
}

static void cleanup_tmp(void)
{
	sigset_t block, old;
	if (!g_have_tmp) return;
	/* Block handled signals across clear-then-unlink: otherwise a signal in that
	 * gap sees the flag already cleared, skips the unlink, and _exit()s leaving
	 * the temp behind; and two cleanups cannot both unlink a name reused between
	 * them. sigprocmask is async-signal-safe, so this is valid from the handler. */
	sigfillset(&block);
	sigprocmask(SIG_BLOCK, &block, &old);
	if (g_have_tmp) { g_have_tmp = 0; (void)unlink(g_tmp); }
	sigprocmask(SIG_SETMASK, &old, NULL);
}

static void on_signal(int sig)
{
	cleanup_tmp();
	_exit(128 + sig);
}

static void die(const char *what, const char *name)
{
	int e = errno;
	cleanup_tmp();
	fprintf(stderr, "%s: %s %s: %s\n", prog, what, name ? name : "", strerror(e));
	exit(1);
}

/* An O_NOFOLLOW open of a symlink reports ELOOP on most systems, EMLINK on a
 * few. isdir is unused here but kept for a possible O_DIRECTORY ENOTDIR case. */
static int is_symlink_open_errno(int e, int isdir)
{
	(void)isdir;
	return e == ELOOP
#ifdef EMLINK
	    || e == EMLINK
#endif
#ifdef EFTYPE
	    || e == EFTYPE	/* NetBSD returns this for O_NOFOLLOW on a symlink */
#endif
#if defined(__hpux) && defined(ENOTSUP)
	    || e == ENOTSUP	/* HP-UX returns this for O_NOFOLLOW on a symlink */
#endif
	    ;
}

/* A trailing '/', or a final "." or "..", makes open(O_NOFOLLOW) and lstat both
 * resolve through a symlink at the last real component, defeating the no-follow
 * guarantee. Refuse such names; a caller names the object itself. */
static void reject_bad_name(const char *name)
{
	size_t l = strlen(name);
	const char *b = strrchr(name, '/');
	b = b ? b + 1 : name;
	if (l == 0 || name[l - 1] == '/' || strcmp(b, ".") == 0 || strcmp(b, "..") == 0) {
		fprintf(stderr, "%s: refusing a name ending in '/', '.' or '..': %s\n", prog, name);
		exit(2);
	}
}

/* Create any missing parent directories of PATH. An existing component is
 * followed when it resolves to a directory -- an administrator may relocate an
 * install directory with a symlink in the path (#411), exactly as `mkdir -p`
 * does -- and a missing one is created 0755. Only the final component, created
 * by the caller, is kept from following a symlink; a parent component swapped
 * under the install is the intermediate-component residual the header documents. */
static void make_parents(const char *path)
{
	char buf[PATH_MAX];
	size_t n = strlen(path);
	char *slash;
	struct stat st;
	mode_t oldmask;

	if (n >= sizeof buf) { errno = ENAMETOOLONG; die("path too long", path); }
	memcpy(buf, path, n + 1);

	/* Create the parents at a full 0755 regardless of the caller's umask, so a
	 * tight umask cannot leave an intermediate (e.g. MANROOT above its man<N>
	 * children) at 0700 that the xymon user cannot enter. */
	oldmask = umask(0);
	slash = buf;
	while ((slash = strchr(slash + 1, '/')) != NULL) {
		*slash = '\0';
		if (buf[0] != '\0') {
			if (stat(buf, &st) == 0) {
				if (!S_ISDIR(st.st_mode)) { umask(oldmask); errno = ENOTDIR; die("a parent path component is not a directory", buf); }
			} else if (errno == ENOENT) {
				if (mkdir(buf, 0755) != 0 && errno != EEXIST) { umask(oldmask); die("cannot create a parent directory", buf); }
			} else {
				umask(oldmask); die("cannot stat a parent directory", buf);
			}
		}
		*slash = '/';
	}
	umask(oldmask);
}

static long parse_octal(const char *s)
{
	char *end;
	long v;
	if (!isdigit((unsigned char)*s)) { fprintf(stderr, "%s: bad mode: %s\n", prog, s); exit(2); }
	errno = 0;
	v = strtol(s, &end, 8);
	if (errno != 0 || end == s || *end != '\0' || v < 0 || v > 07777) {
		fprintf(stderr, "%s: bad mode: %s\n", prog, s);
		exit(2);
	}
	return v;
}

static unsigned long parse_id(const char *s)
{
	char *end;
	unsigned long v;
	/* The caller admits only a leading digit, so no sign reaches strtoul. The
	 * representability and sentinel checks are applied against the target type. */
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "%s: bad numeric id: %s\n", prog, s);
		exit(2);
	}
	return v;
}

static int all_digits(const char *s)
{
	if (!*s) return 0;
	for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
	return 1;
}

static uid_t lookup_user(const char *s)
{
	struct passwd *pw;
	if (!s) return (uid_t)-1;
	/* An all-digits argument is a literal id: parse it without a name lookup. A
	 * name service that is slow or down then cannot turn "-o 1234" into a
	 * failure, and a platform whose getpwnam reports not-found through errno
	 * (AIX returns ESRCH) cannot be mistaken for a backend error. A name, by
	 * contrast, must resolve. */
	if (all_digits(s)) {
		unsigned long v = parse_id(s);
		uid_t u = (uid_t)v;
		/* Reject an id that does not survive the cast to uid_t (a narrower type
		 * on some platforms), or that is the (uid_t)-1 "do not change" sentinel. */
		if ((unsigned long)u != v || u == (uid_t)-1) { fprintf(stderr, "%s: uid out of range: %s\n", prog, s); exit(2); }
		return u;
	}
	if ((pw = getpwnam(s)) != NULL) {
		if (pw->pw_uid == (uid_t)-1) { fprintf(stderr, "%s: user %s has the reserved id\n", prog, s); exit(2); }
		return pw->pw_uid;
	}
	fprintf(stderr, "%s: no such user: %s\n", prog, s);
	exit(2);
}

static gid_t lookup_group(const char *s)
{
	struct group *gr;
	if (!s) return (gid_t)-1;
	if (all_digits(s)) {
		unsigned long v = parse_id(s);
		gid_t g = (gid_t)v;
		if ((unsigned long)g != v || g == (gid_t)-1) { fprintf(stderr, "%s: gid out of range: %s\n", prog, s); exit(2); }
		return g;
	}
	if ((gr = getgrnam(s)) != NULL) {
		if (gr->gr_gid == (gid_t)-1) { fprintf(stderr, "%s: group %s has the reserved id\n", prog, s); exit(2); }
		return gr->gr_gid;
	}
	fprintf(stderr, "%s: no such group: %s\n", prog, s);
	exit(2);
}

/* Set owner (if one was given) and mode on an existing name, without following a symlink
 * at it. A symlink is left untouched; a non-plain / non-directory name, or one
 * that changes under us, is refused. */
/* Set owner then mode on an open fd -- owner first, so a later chown cannot
 * clear a setuid/setgid bit. With no mode (-m absent) on a non-directory, strip
 * any setuid/setgid bit instead, decided on the bits as they stand after the
 * chown rather than an earlier stat. On any failure, close the fd and die. */
static void apply_owner_mode(int fd, uid_t uid, gid_t gid, long mode, int isdir, const char *name)
{
	if ((uid != (uid_t)-1 || gid != (gid_t)-1) && fchown(fd, uid, gid) != 0)
		{ close(fd); die("cannot set owner of", name); }
	if (mode >= 0) {
		if (fchmod(fd, (mode_t)mode) != 0) { close(fd); die("cannot set mode of", name); }
	} else if (!isdir) {
		struct stat p;
		if (fstat(fd, &p) != 0) { close(fd); die("cannot stat", name); }
		if ((p.st_mode & 06000) && fchmod(fd, p.st_mode & 01777) != 0) { close(fd); die("cannot clear setuid of", name); }
	}
}

static void set_owner_and_mode(const char *name, uid_t uid, gid_t gid, long mode, int isdir)
{
	int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY;
	struct stat lst, fst;
	int fd;

	if (lstat(name, &lst) != 0) die("cannot stat", name);

	/* A symlink at the name: its relocation is the admin's, do not touch it --
	 * but for mkdir confirm the directory actually exists through it. */
	if (S_ISLNK(lst.st_mode)) {
		if (isdir) {
			struct stat ts;
			if (stat(name, &ts) != 0 || !S_ISDIR(ts.st_mode)) { errno = ENOTDIR; die("directory name is a symlink not pointing to a directory", name); }
		}
		return;
	}

	/* Classify by lstat before opening, so a fifo/device already at the name is
	 * not opened (which could block or have a side effect); one raced in between
	 * is caught by the post-open fstat, and O_NONBLOCK keeps a fifo from hanging. */
	if (isdir) {
		if (!S_ISDIR(lst.st_mode)) { errno = ENOTDIR; die("not a directory", name); }
	} else if (!S_ISREG(lst.st_mode) || lst.st_nlink != 1) {
		errno = EPERM;
		die("not a plain, singly-linked file", name);
	}

#ifdef O_DIRECTORY
	if (isdir) flags |= O_DIRECTORY;
#endif
	fd = open(name, flags);
	if (fd < 0) die("cannot open", name);	/* a swap to a symlink since lstat -> ELOOP here */
	if (fstat(fd, &fst) != 0) { close(fd); die("cannot stat", name); }

	/* The open landed on an object with the device and inode lstat saw, which
	 * catches a build without O_NOFOLLOW and an ordinary swap between the lstat
	 * and the open. A swap to a reused inode of the same type is not distinguished
	 * (the type recheck below bounds it to the same kind); closing that fully
	 * would need the parent directory held non-writable throughout, which the
	 * install does not assume. */
	if (fst.st_dev != lst.st_dev || fst.st_ino != lst.st_ino) {
		close(fd);
		errno = EAGAIN;
		die("name changed under the install", name);
	}
	if (!isdir && fst.st_nlink != 1) { close(fd); errno = EPERM; die("not a singly-linked file", name); }
	/* Re-check the type on the fd: dev/ino equality can be met by a reused inode
	 * number (an ABA swap), so confirm the open object is still the kind lstat saw. */
	if (isdir ? !S_ISDIR(fst.st_mode) : !S_ISREG(fst.st_mode)) { close(fd); errno = EPERM; die("type changed under the install", name); }

	apply_owner_mode(fd, uid, gid, mode, isdir, name);
	if (close(fd) != 0) die("close failed on", name);
}

/* Ensure a plain file exists at NAME (create it empty if absent, keep its
 * content if present), then set owner/mode, without following a symlink. A
 * symlink at the name is left untouched (an admin relocation). */
static void ensure_file(const char *name, uid_t uid, gid_t gid, long mode)
{
	struct stat lst, fst;
	int fd, tries;

	/* Classify by lstat first, exactly as set_owner_and_mode does, so nothing is
	 * created or opened through a symlink even on a build without O_NOFOLLOW. A
	 * name that races in between the lstat and the create/open is re-classified,
	 * bounded: a file that appears after the lstat is still given its owner and
	 * mode, rather than reported done untouched, and a name flipped repeatedly
	 * fails loudly instead of succeeding silently or spinning. */
	for (tries = 0; tries <= 8; tries++) {
		if (lstat(name, &lst) != 0) {
			if (errno != ENOENT) die("cannot stat", name);	/* e.g. a parent symlink loop: refuse */

			/* Absent: create it fresh. O_EXCL refuses a symlink or any name
			 * that raced into existence (so a symlink that appeared cannot
			 * be followed), with or without O_NOFOLLOW. */
			fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
			if (fd >= 0) {
				/* A new file can inherit the directory's group (setgid dir, or
				 * BSD): set the group to root's even without -g, as
				 * install_file does. */
				if (gid == (gid_t)-1) gid = getegid();
				apply_owner_mode(fd, uid, gid, mode, 0, name);
				if (close(fd) != 0) die("close failed on", name);
				return;
			}
			if (is_symlink_open_errno(errno, 0)) return;	/* a symlink raced in: leave it */
			if (errno != EEXIST) die("cannot create", name);
			continue;	/* a non-symlink raced in (EEXIST): re-classify and set it below */
		}

		/* A symlink at the name: leave its relocation, as set does. */
		if (S_ISLNK(lst.st_mode)) return;
		if (!S_ISREG(lst.st_mode) || lst.st_nlink != 1) { errno = EPERM; die("not a plain, singly-linked file", name); }

		/* Exists as a plain file: open it (no create, no truncate), confirm the
		 * fd is that object, and set owner/mode by fd. */
		fd = open(name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);	/* only owner/mode are set, never content, so read-only suffices and works on a 0444 file its owner may still chmod */
		if (fd < 0) {
			if (errno == ENOENT) continue;	/* vanished after the lstat: start over */
			die("cannot open", name);
		}
		if (fstat(fd, &fst) != 0) { close(fd); die("cannot stat", name); }
		if (fst.st_dev != lst.st_dev || fst.st_ino != lst.st_ino) { close(fd); errno = EAGAIN; die("name changed under the install", name); }
		if (!S_ISREG(fst.st_mode) || fst.st_nlink != 1) { close(fd); errno = EPERM; die("not a plain, singly-linked file", name); }
		apply_owner_mode(fd, uid, gid, mode, 0, name);
		if (close(fd) != 0) die("close failed on", name);
		return;
	}
	errno = EAGAIN;
	die("name keeps changing under the install", name);
}

static void install_file(const char *src, const char *dest, uid_t uid, gid_t gid, long mode)
{
	int sfd, tfd, tries;
	char buf[65536];
	ssize_t n;
	struct stat fst, lst, sst;
	sigset_t block, oldmask;

	/* O_NONBLOCK so a fifo SRC cannot hang; require a regular source. */
	if ((sfd = open(src, O_RDONLY | O_NONBLOCK | O_NOCTTY)) < 0) die("cannot read", src);
	if (fstat(sfd, &sst) != 0) { close(sfd); die("cannot stat", src); }
	if (!S_ISREG(sst.st_mode)) { close(sfd); errno = EINVAL; die("source is not a regular file", src); }

	/* Create the temp under an unpredictable name in DEST's directory. O_EXCL
	 * refuses any existing name (so it is never a planted symlink, which
	 * O_NOFOLLOW also refuses) and, with the random name, never a stale temp; a
	 * collision is retried rather than unlinked, so a name we did not create is
	 * never removed. The create and the g_have_tmp flag it publishes are done
	 * under blocked signals, so cleanup never sees the flag and the file out of
	 * step. Mode 0: unopenable by anyone but root until the final fchmod, so the owner
	 * set by fchown cannot grab a writable fd in the fchown->fchmod window (copy
	 * always sets a mode, so the file never stays 0). */
	for (tries = 0; ; tries++) {
		char suf[17];
		rand_suffix(suf, 16);
		if ((size_t)snprintf(g_tmp, sizeof g_tmp, "%s.inst.%s", dest, suf) >= sizeof g_tmp)
			{ close(sfd); fprintf(stderr, "%s: destination path too long: %s\n", prog, dest); exit(1); }
		/* Block signals across the create and the flag that publishes it, so a
		 * signal never runs cleanup with the flag and the file out of step. */
		sigfillset(&block);
		sigprocmask(SIG_BLOCK, &block, &oldmask);
		tfd = open(g_tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0);
		if (tfd >= 0) g_have_tmp = 1;
		sigprocmask(SIG_SETMASK, &oldmask, NULL);
		if (tfd >= 0) break;
		if (errno == EEXIST && tries < 8) continue;
		close(sfd);
		die("cannot create temporary at", g_tmp);
	}

	while ((n = read(sfd, buf, sizeof buf)) > 0) {
		char *p = buf;
		while (n > 0) {
			ssize_t w = write(tfd, p, (size_t)n);
			if (w < 0) { if (errno == EINTR) continue; close(tfd); close(sfd); die("write failed to", g_tmp); }
			if (w == 0) { errno = EIO; close(tfd); close(sfd); die("write stalled on", g_tmp); }
			p += w; n -= w;
		}
	}
	if (n < 0) { close(tfd); close(sfd); die("read failed from", src); }
	(void)close(sfd);

	/* Force the group even without -g: a new temp can inherit the directory's
	 * group (a setgid directory, or BSD/macOS semantics), and the installed file
	 * must not keep the unprivileged user's group. uid may stay -1 (root, since
	 * we created the temp). */
	if (gid == (gid_t)-1) gid = getegid();
	apply_owner_mode(tfd, uid, gid, mode, 0, g_tmp);
	if (fsync(tfd) != 0 && errno != EINVAL && errno != ENOTSUP) { close(tfd); die("fsync failed on", g_tmp); }

	/* A hardlink of the temp to DEST would make rename a successful no-op and
	 * leave the finished temp (with its final, maybe setuid, mode) behind for the
	 * that user to keep. Refuse a temp that is linked anywhere else, and capture
	 * its identity for the post-rename confirmation. */
	if (fstat(tfd, &fst) != 0) { close(tfd); die("cannot stat", g_tmp); }
	if (fst.st_nlink != 1) { close(tfd); errno = EPERM; die("temporary was linked elsewhere", g_tmp); }

	/* rename replaces whatever name is at DEST -- a file or a symlink -- in one
	 * step, acting on the name, never a symlink's target. Keep tfd open so the
	 * installed name can be confirmed to be the file just written (the directory's owner
	 * owning the directory could swap the temp name between the fchmod and the
	 * rename). */
	/* Block signals across the rename and the flag clear: once g_tmp is renamed
	 * to DEST it no longer exists, so a signal in that gap must not run cleanup
	 * and unlink whatever now has g_tmp's (random, but now free) name. */
	sigfillset(&block);
	sigprocmask(SIG_BLOCK, &block, &oldmask);
	if (rename(g_tmp, dest) != 0) { sigprocmask(SIG_SETMASK, &oldmask, NULL); close(tfd); die("cannot install", dest); }
	g_have_tmp = 0;		/* g_tmp is now DEST: nothing left to clean up */
	sigprocmask(SIG_SETMASK, &oldmask, NULL);

	if (lstat(dest, &lst) != 0 || fst.st_dev != lst.st_dev || fst.st_ino != lst.st_ino ||
	    !S_ISREG(lst.st_mode) || lst.st_nlink != 1) {
		int gone = (unlink(dest) == 0);
		close(tfd);
		fprintf(stderr, "%s: %s changed under the install; %s\n", prog, dest,
			gone ? "removed" : "could not be removed");
		exit(1);
	}
	/* The final write error on a delayed-I/O filesystem can surface only on
	 * close, especially where fsync above returned EINVAL/ENOTSUP. */
	if (close(tfd) != 0) die("close failed on", dest);
}

static void usage(void)
{
	fprintf(stderr,
	    "usage: %s copy  -m MODE [-o OWNER] [-g GROUP] SRC DEST   install SRC as DEST\n"
	    "       %s mkdir [-m MODE] [-o OWNER] [-g GROUP] DIR      create DIR and parents; set owner/mode on creation\n"
	    "       %s touch [-m MODE] [-o OWNER] [-g GROUP] NAME     create NAME if absent, set owner/mode\n"
	    "       %s set   [-m MODE] [-o OWNER] [-g GROUP] NAME     set owner/mode of an existing NAME\n",
	    prog, prog, prog, prog);
	exit(2);
}

static void install_signal_handlers(void)
{
	static const int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGALRM,
#ifdef SIGXFSZ
	    SIGXFSZ,	/* FreeBSD/NetBSD hide it under _POSIX_C_SOURCE; handle it where exposed */
#endif
	};
	struct sigaction sa, old;
	size_t i;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigfillset(&sa.sa_mask);	/* block every other handled signal while the handler runs, so cleanup_tmp cannot be interrupted and re-entered */
	for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
		if (sigaction(sigs[i], NULL, &old) == 0 && old.sa_handler != SIG_IGN)
			sigaction(sigs[i], &sa, NULL);	/* leave an inherited SIG_IGN (nohup) alone */
	}
}

int main(int argc, char **argv)
{
	enum { COPY, MKDIR, TOUCH, SET } action;
	const char *verb;
	int opt, nfd;
	long mode = -1;
	const char *owner = NULL, *group = NULL;
	uid_t uid;
	gid_t gid;

	/* Reserve fds 0-2 so a later open() cannot become stderr and have an error
	 * message write into an opened file. */
	/* Reserve fds 0-2 so a later open() cannot land on 0/1/2 and be mistaken for
	 * stdin/out/err. Only a real gap matters: when all three are already open
	 * (the normal case) a missing /dev/null is harmless, so do not require it. */
	if (fcntl(0, F_GETFD) == -1 || fcntl(1, F_GETFD) == -1 || fcntl(2, F_GETFD) == -1) {
		do { nfd = open("/dev/null", O_RDWR); } while (nfd >= 0 && nfd < 3);
		if (nfd < 0) _exit(2);	/* a gap in 0-2 we cannot fill: refuse */
		if (nfd >= 3) close(nfd);
	}

	if (argc < 2) usage();
	verb = argv[1];
	if      (strcmp(verb, "copy")  == 0) action = COPY;
	else if (strcmp(verb, "mkdir") == 0) action = MKDIR;
	else if (strcmp(verb, "touch") == 0) action = TOUCH;
	else if (strcmp(verb, "set")   == 0) action = SET;
	else usage();
	argc--; argv++;			/* drop the verb; getopt the rest */

	while ((opt = getopt(argc, argv, "m:o:g:")) != -1) {
		switch (opt) {
		case 'm': mode = parse_octal(optarg); break;
		case 'o': owner = optarg; break;
		case 'g': group = optarg; break;
		default: usage();
		}
	}
	argc -= optind; argv += optind;

	if (action == COPY) {
		if (argc != 2) usage();
		if (mode < 0) { fprintf(stderr, "%s: copy needs -m MODE\n", prog); usage(); }
	} else {
		if (argc != 1) usage();
		if (mode >= 0 && (mode & 06000))
			{ fprintf(stderr, "%s: a setuid/setgid mode is only allowed with copy\n", prog); exit(2); }
	}

	install_signal_handlers();

	uid = lookup_user(owner);
	gid = lookup_group(group);

	/* A setuid/setgid file must end up root-owned. If it is given to the
	 * unprivileged user, that user owns the finished inode and can rewrite its
	 * content in the window before the final mode is set. -o unset keeps root,
	 * the temp's creator. */
	if (mode >= 0 && (mode & 06000)) {
		if (uid != 0 && !(uid == (uid_t)-1 && geteuid() == 0)) {
			fprintf(stderr, "%s: a setuid/setgid mode requires root ownership\n", prog);
			exit(2);
		}
		if (mode & 022) {	/* group- or world-writable: anyone there could rewrite the privileged file */
			fprintf(stderr, "%s: a setuid/setgid mode must not be group- or world-writable\n", prog);
			exit(2);
		}
	}

	if (action == COPY) {
		reject_bad_name(argv[1]);
		install_file(argv[0], argv[1], uid, gid, mode);
	} else if (action == TOUCH) {
		reject_bad_name(argv[0]);
		ensure_file(argv[0], uid, gid, mode);
	} else if (action == MKDIR) {
		/* Strip a trailing slash ("foo/" -> "foo") so it is not rejected and the
		 * slash does not force resolution through the name. */
		size_t n = strlen(argv[0]);
		while (n > 1 && argv[0][n - 1] == '/') argv[0][--n] = '\0';
		reject_bad_name(argv[0]);
		make_parents(argv[0]);
		if (mkdir(argv[0], 0700) == 0) {
			/* Freshly created: own and mode it. The mode is applied on creation
			 * regardless of umask, so a tight umask cannot leave a directory the
			 * xymon user cannot enter. */
			set_owner_and_mode(argv[0], uid, gid, mode, 1);
		} else if (errno == EEXIST) {
			/* Already there: confirm it is, or resolves to, a directory, but
			 * leave its owner and mode alone. This is a deliberate change from
			 * the old recipe, whose "chown; chmod 755" restamped every directory
			 * on every run: a relocated or system-shared directory keeps the
			 * owner and mode it was given, and a reinstall does not override an
			 * administrator's choice. */
			struct stat st;
			if (stat(argv[0], &st) != 0 || !S_ISDIR(st.st_mode)) { errno = ENOTDIR; die("directory name is not a directory", argv[0]); }
		} else {
			die("cannot create directory", argv[0]);
		}
	} else {	/* SET */
		reject_bad_name(argv[0]);
		set_owner_and_mode(argv[0], uid, gid, mode, 0);
	}
	return 0;
}
