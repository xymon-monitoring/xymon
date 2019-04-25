#include <stdio.h>
#include <unistd.h>
#include <limits.h>

int main(int argc, char *argv[])
{
	long res;

#ifndef PATH_MAX
	res = pathconf("/", _PC_PATH_MAX);
	if (res == -1) res = 4096;
	printf("#define PATH_MAX %ld\n", res);
#endif

	return 0;
}

