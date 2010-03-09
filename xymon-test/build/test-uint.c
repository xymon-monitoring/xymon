#include <sys/types.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	u_int32_t l;

	l = 1;
	printf("%u:%d\n", l, sizeof(l));

	return 0;
}

