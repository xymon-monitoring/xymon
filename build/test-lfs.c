#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	off_t fileofs;
	int minsize = atoi(argv[1]);

	fileofs = 0;

#ifdef _LARGEFILE_SOURCE
	printf("%d:%lld\n", (sizeof(off_t) >= minsize), fileofs);
#else
	printf("%d:%ld\n", (sizeof(off_t) >= minsize), fileofs);
#endif

	return 0;
}

