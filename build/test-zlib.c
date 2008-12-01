#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

int main(int argc, char **argv)
{
	z_stream strm;
	int n;

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	n = inflateInit(&strm);
	if (n == Z_VERSION_ERROR) {
		printf("Version mismatch: Compiled with %s, runtime has %s\n",
			ZLIB_VERSION, zlibVersion());
	}
	else {
		printf("zlib version %s\n", zlibVersion());
		if (ZLIB_VERNUM < 0x1230) {
			char *override = getenv("IGNOREOLDZLIB");

			printf("Your zlib version is too old, requires version 1.2.3 or later\n");
			if (override) printf("Ignoring this and continuing anyway\n");

			return (override ? 0 : 1);
		}
	}

	return 0;
}

