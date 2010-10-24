#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	char l[100];
	snprintf(l, sizeof(l), "testing ... %d %d %d\n", 1, 2, 3);
	return 0;
}

