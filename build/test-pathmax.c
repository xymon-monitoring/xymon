#include <stdio.h>
#include <unistd.h>
#include <limits.h>

int main(int argc, char *argv[])
{
	long res;

#ifndef PATH_MAX
	res = pathconf("/", _PC_PATH_MAX);
	printf("#define PATH_MAX %ld\n", res);
#endif

	return 0;
}

