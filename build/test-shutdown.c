#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[])
{

#ifndef SHUT_RD
	printf("#define SHUT_RD 0\n");
#endif

#ifndef SHUT_WR
	printf("#define SHUT_WR 1\n");
#endif

#ifndef SHUT_RDWR
	printf("#define SHUT_RDWR 2\n");
#endif

	return 0;
}

