#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	char srcfn[PATH_MAX];
	char destfn[PATH_MAX];
	char *p;
	struct stat st;
	unsigned char *sumbuf = NULL;

	if (argc > 2) {
		if (stat(argv[2], &st) == 0) {
			FILE *sumfd;
			printf("Loading md5-data ... ");
			sumfd = fopen(argv[2], "r");
			if (sumfd) {
				sumbuf = (char *)malloc(st.st_size + 1);
				if (fread(sumbuf, 1, st.st_size, sumfd) == st.st_size) {
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

		strcpy(destfn, argv[1]);
		p = srcfn;
		if (strcmp(srcfn, ".") == 0) p = "";
		else if (strncmp(p, "./", 2) == 0) p += 2;
		strcat(destfn, p);

		*srcmd5 = '\0';
		if (((fd = fopen(srcfn, "r")) != NULL) && ((ctx = digest_init("md5")) != NULL)) {
			while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) digest_data(ctx, buf, buflen);
			strcpy(srcmd5, digest_done(ctx));
			fclose(fd);
		}

		if (stat(destfn, &st) == 0) {
			/* Destination file exists, see if it's a previous version */

			if (sumbuf == NULL) continue; /* No md5-data, dont overwrite an existing file */
			if (!S_ISREG(st.st_mode)) continue;

			fd = fopen(destfn, "r"); if (fd == NULL) continue;
			if ((ctx = digest_init("md5")) == NULL) continue;
			while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) digest_data(ctx, buf, buflen);
			md5sum = digest_done(ctx);
			fclose(fd);

			if (strstr(sumbuf, md5sum) == NULL) continue;  /* Not one of our known versions */
			if (strcmp(srcmd5, md5sum) == 0) continue; /* Already installed */

			/* We now know the destination that exists is just one of our old files */
			printf("Updating old standard file %s\n", destfn);
			unlink(destfn);
		}
		else {
			printf("Installing new file %s\n", destfn);
		}

		if (lstat(srcfn, &st) != 0) {
			printf("Error - cannot lstat() %s\n", srcfn);
			return 1;
		}

		if (S_ISREG(st.st_mode)) {
			FILE *infd, *outfd;
			char buf[16384];
			int n;

			infd = fopen(srcfn, "r");
			if (infd == NULL) {
				/* Dont know how this can happen, but .. */
				fprintf(stderr, "Cannot open input file %s: %s\n", srcfn, strerror(errno));
				return 1;
			}
			outfd = fopen(destfn, "w");
			if (outfd == NULL) {
				/* Dont know how this can happen, but .. */
				fprintf(stderr, "Cannot create output file %s: %s\n", destfn, strerror(errno));
				return 1;
			}
			while ( (n = fread(buf, 1, sizeof(buf), infd)) > 0) fwrite(buf, 1, n, outfd);
			fclose(infd); fclose(outfd);
			chmod(destfn, st.st_mode);
		}
		else if (S_ISDIR(st.st_mode)) {
			struct stat tmpst;

			/* Create upper-lying directories */
			if (*destfn == '/') p = strchr(destfn+1, '/'); else p = strchr(destfn, '/');
			while (p) {
				*p = '\0';
				if ((stat(destfn, &tmpst) == 0) || (mkdir(destfn, st.st_mode) == 0)) { 
					*p = '/'; 
					p = strchr(p+1, '/');
				}
				else p = NULL;
			}

			/* Create the directory itself */
			if (stat(destfn, &tmpst) == -1) mkdir(destfn, st.st_mode);
			chmod(destfn, st.st_mode);
		}
		else if (S_ISLNK(st.st_mode)) {
			char ldest[PATH_MAX + 1];

			memset(ldest, 0, sizeof(ldest));
			readlink(srcfn, ldest, sizeof(ldest)-1);
			symlink(ldest, destfn);
		}
	}

	return 0;
}

