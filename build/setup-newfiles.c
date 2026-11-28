#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <pwd.h>

#include "libxymon.h"

/* setup-newfiles installs a shipped default config or www file only where the
 * administrator has none, or an unmodified old version of ours. It decides which
 * files to place; the placement itself -- install-safe's symlink-safe copy
 * (written through a temporary, owner and mode after the content) or mkdir -- is
 * run as a child rather than reimplemented here. */
static char installsafe[PATH_MAX];

/* Owner passed to install-safe for each file and directory, resolved from
 * $XYMONUSER. Set only for a real root install (not a PKGBUILD staging run, and
 * only when we run as root), matching the makefiles' "ifndef PKGBUILD" guard. */
static int set_owner = 0;
static char uidstr[32];
static char gidstr[32];

/* Run install-safe with the given NULL-terminated argv; 0 on success. */
static int run_installsafe(char *const av[])
{
	pid_t pid = fork();
	int status;

	if (pid < 0) { fprintf(stderr, "setup-newfiles: fork: %s\n", strerror(errno)); return -1; }
	if (pid == 0) {
		/* execvp, not execv: a bare "install-safe" (argv[0] had no slash) is then
		 * found on PATH, while the usual "../../build/install-safe" path is used
		 * as given. */
		execvp(installsafe, av);
		fprintf(stderr, "setup-newfiles: cannot run %s: %s\n", installsafe, strerror(errno));
		_exit(127);
	}
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) { fprintf(stderr, "setup-newfiles: waitpid: %s\n", strerror(errno)); return -1; }
	}
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* Append "-o UID -g GID" to av when installing as root; return the new count. */
static int add_owner(char *av[], int ac)
{
	if (set_owner) {
		av[ac++] = "-o"; av[ac++] = uidstr;
		av[ac++] = "-g"; av[ac++] = gidstr;
	}
	return ac;
}

int main(int argc, char *argv[])
{
	char srcfn[PATH_MAX];
	char destfn[PATH_MAX];
	char *p;
	struct stat st;
	unsigned char *sumbuf = NULL;

	if (argc < 2) { fprintf(stderr, "usage: %s DESTDIR [MD5FILE]  (file list on stdin)\n", argv[0]); return 1; }

	/* install-safe is our sibling in build/: take its path from our own argv[0]. */
	{
		const char *slash = strrchr(argv[0], '/');
		if (slash && (size_t)(slash - argv[0]) + sizeof("/install-safe") <= sizeof(installsafe)) {
			size_t n = (size_t)(slash - argv[0]) + 1;
			memcpy(installsafe, argv[0], n);
			strcpy(installsafe + n, "install-safe");
		}
		else {
			strcpy(installsafe, "install-safe");
		}
	}

	{
		char *u = getenv("XYMONUSER");
		char *pkg = getenv("PKGBUILD");
		if (u && *u && (!pkg || !*pkg) && geteuid() == 0) {
			struct passwd *pw = getpwnam(u);
			if (!pw) {
				/* Ownership was asked for: a name that will not resolve must stop
				 * the install, not silently leave every file root-owned. */
				fprintf(stderr, "setup-newfiles: cannot resolve XYMONUSER '%s' (unknown user or lookup failure)\n", u);
				return 1;
			}
			set_owner = 1;
			snprintf(uidstr, sizeof(uidstr), "%lu", (unsigned long)pw->pw_uid);
			snprintf(gidstr, sizeof(gidstr), "%lu", (unsigned long)pw->pw_gid);
		}
	}

	if (argc > 2) {
		/* A regular md5-data file whose size fits in size_t with a byte to spare
		 * for the terminator; otherwise it is skipped (sumbuf stays NULL, so no
		 * existing file is overwritten). On a 32-bit build this rejects a table
		 * larger than size_t rather than truncating the length. */
		if (stat(argv[2], &st) == 0 && S_ISREG(st.st_mode) &&
		    st.st_size >= 0 && (unsigned long long)st.st_size < (unsigned long long)((size_t)-1)) {
			size_t sz = (size_t)st.st_size;
			FILE *sumfd;
			printf("Loading md5-data ... ");
			sumfd = fopen(argv[2], "r");
			if (sumfd) {
				sumbuf = (unsigned char *)malloc(sz + 1);
				if (sumbuf && fread(sumbuf, 1, sz, sumfd) == sz) {
					sumbuf[sz] = '\0';	/* terminate for the strstr below */
					printf("OK\n");
				}
				else {
					printf("failed\n");
					free(sumbuf);
					sumbuf = NULL;
				}
				fclose(sumfd);
			}
			else {
				printf("failed\n");
			}
		}
	}

	while (fgets(srcfn, sizeof(srcfn), stdin)) {
		FILE *fd;
		unsigned char buf[8192];
		size_t buflen;
		digestctx_t *ctx;
		char srcmd5[40];
		char *md5sum;

		p = strchr(srcfn, '\n'); if (p) *p = '\0';

		p = srcfn;
		if (strcmp(srcfn, ".") == 0) p = "";
		else if (strncmp(p, "./", 2) == 0) p += 2;
		if ((size_t)snprintf(destfn, sizeof(destfn), "%s%s", argv[1], p) >= sizeof(destfn)) {
			fprintf(stderr, "setup-newfiles: destination path too long: %s%s\n", argv[1], p);
			return 1;
		}

		*srcmd5 = '\0';
		if ((fd = fopen(srcfn, "r")) != NULL) {
			struct stat sfst;
			/* Only a regular file is hashed: the source list also names
			 * directories (fread would fail EISDIR on them) and symlinks, which
			 * are not content-compared. */
			if (fstat(fileno(fd), &sfst) == 0 && S_ISREG(sfst.st_mode) && (ctx = digest_init("md5")) != NULL) {
				while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) digest_data(ctx, buf, buflen);
				if (ferror(fd)) { fprintf(stderr, "setup-newfiles: read failed on %s: %s\n", srcfn, strerror(errno)); fclose(fd); return 1; }
				strcpy(srcmd5, digest_done(ctx));
			}
			fclose(fd);
		}

		if (stat(destfn, &st) == 0) {
			/* Destination exists: replace it only if it is an unmodified old
			 * version of ours, never an administrator's own edit. */
			if (sumbuf == NULL) continue;	/* no md5-data: never overwrite */
			if (!S_ISREG(st.st_mode)) continue;

			fd = fopen(destfn, "r"); if (fd == NULL) continue;
			if ((ctx = digest_init("md5")) == NULL) { fclose(fd); continue; }
			while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) digest_data(ctx, buf, buflen);
			if (ferror(fd)) { fclose(fd); continue; }	/* cannot verify it: leave the file alone */
			md5sum = digest_done(ctx);
			fclose(fd);

			if (strstr((char *)sumbuf, md5sum) == NULL) continue;	/* not a known version */
			if (strcmp(srcmd5, md5sum) == 0) continue;		/* already current */

			printf("Updating old standard file %s\n", destfn);
			/* install-safe copy replaces the destination atomically; no unlink. */
		}
		else {
			printf("Installing new file %s\n", destfn);
		}

		if (lstat(srcfn, &st) != 0) {
			fprintf(stderr, "setup-newfiles: cannot lstat %s: %s\n", srcfn, strerror(errno));
			return 1;
		}

		if (S_ISREG(st.st_mode)) {
			char *av[12]; int ac = 0;
			av[ac++] = installsafe;
			av[ac++] = "copy";
			av[ac++] = "-m"; av[ac++] = "644";
			ac = add_owner(av, ac);
			av[ac++] = srcfn;
			av[ac++] = destfn;
			av[ac] = NULL;
			if (run_installsafe(av) != 0) return 1;
		}
		else if (S_ISDIR(st.st_mode)) {
			char *av[10]; int ac = 0;
			av[ac++] = installsafe;
			av[ac++] = "mkdir";
			av[ac++] = "-m"; av[ac++] = "755";
			ac = add_owner(av, ac);
			av[ac++] = destfn;
			av[ac] = NULL;
			if (run_installsafe(av) != 0) return 1;
		}
		else if (S_ISLNK(st.st_mode)) {
			char ldest[PATH_MAX + 1];
			ssize_t ln;

			memset(ldest, 0, sizeof(ldest));
			ln = readlink(srcfn, ldest, sizeof(ldest)-1);
			if (ln < 0) { fprintf(stderr, "setup-newfiles: cannot readlink %s: %s\n", srcfn, strerror(errno)); return 1; }
			if ((size_t)ln >= sizeof(ldest)-1) { fprintf(stderr, "setup-newfiles: symlink target too long: %s\n", srcfn); return 1; }
			ldest[ln] = '\0';
			/* Creating a symlink names an entry; it does not write through one.
			 * An existing name is left as it is. */
			if (symlink(ldest, destfn) != 0 && errno != EEXIST) {
				fprintf(stderr, "setup-newfiles: cannot symlink %s: %s\n", destfn, strerror(errno));
				return 1;
			}
		}
	}
	if (ferror(stdin)) { fprintf(stderr, "setup-newfiles: read failed on the file list\n"); return 1; }

	return 0;
}
