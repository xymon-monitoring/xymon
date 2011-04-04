#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	char buf[1024];
	char *newnam, *oldnam, *oldval, *p;

	while (fgets(buf, sizeof(buf), stdin)) {
		p = strchr(buf, '\n'); if (p) *p = '\0';
		newnam = buf;
		oldnam = strchr(buf, '='); if (!oldnam) continue;
		*oldnam = '\0'; oldnam++;
		oldval = getenv(oldnam);
		if (oldval) {
			printf("%s=\"", newnam);
			for (p = oldval; (*p); p++) {
				if (*p == '"')
					printf("\\\"");
				else
					printf("%c", *p);
			}
			printf("\"\n");
		}
	}

	return 0;
}

