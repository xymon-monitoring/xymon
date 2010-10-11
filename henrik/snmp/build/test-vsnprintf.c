#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

void errprintf(const char *fmt, ...)
{
        char msg[4096];
        va_list args;

        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
}

int main(int argc, char *argv[])
{
	errprintf("testing ... %d %d %d\n", 1, 2, 3);
	return 0;
}

