#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

int main(int argc, char *argv[])
{
	PCRE2_SIZE errofs = 0;
	int errnum = 0;
	pcre2_code *result;
	PCRE2_SPTR pattern = (PCRE2_SPTR)"xymon.*";

	result = pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &errnum, &errofs, NULL);
	if (result != NULL) pcre2_code_free(result);

	return 0;
}
