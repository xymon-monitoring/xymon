#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

int main(int argc, char *argv[])
{
	char srcfn[PATH_MAX];
	char destfn[PATH_MAX];
	char *p;
	struct stat st;

	while (fgets(srcfn, sizeof(srcfn), stdin)) {
		p = strchr(srcfn, '\n'); if (p) *p = '\0';

		strcpy(destfn, argv[1]);
		p = srcfn;
		if (strcmp(srcfn, ".") == 0) p = "";
		else if (strncmp(p, "./", 2) == 0) p += 2;
		strcat(destfn, p);

		if (stat(destfn, &st) == 0) continue;  /* Destination file exists, dont do anything */

		if (lstat(srcfn, &st) != 0) {
			printf("Error - cannot lstat() %s\n", srcfn);
			return 1;
		}

		if (S_ISREG(st.st_mode)) {
			FILE *infd, *outfd;
			char buf[16384];
			int n;

			infd = fopen(srcfn, "r");
			outfd = fopen(destfn, "w");
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

