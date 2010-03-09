#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	long long l;

	l = strtoll("1234567890123456789x", NULL, 10);
	printf("%lld\n", l);

	return 0;
}

