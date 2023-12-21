#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

int main(int argc, char *argv[])
{
	pcre2_code *result;
	int err;
	PCRE2_SIZE errofs;
	result = pcre2_compile("xymon.*", PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &err, &errofs, NULL);

	return 0;
}

