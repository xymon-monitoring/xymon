#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	off_t fileofs;
	int minsize = atoi(argv[1]);

	memset(&fileofs, 0, sizeof(fileofs));

#ifdef _LARGEFILE_SOURCE
	printf("%d:%d:%lld\n", sizeof(off_t), (sizeof(off_t) >= minsize), fileofs);
#else
	printf("%d:%d:%ld\n", sizeof(off_t), (sizeof(off_t) >= minsize), fileofs);
#endif

	return 0;
}

