#include <stdlib.h>

int main(int argc, char *argv[])
{
	setenv("FOO", "BAR", 1);
	return 0;
}

