#include <pcre.h>

int main(int argc, char *argv[])
{
	pcre *result;
	const char *errmsg;
	int errofs;
	result = pcre_compile("xymon.*", PCRE_CASELESS, &errmsg, &errofs, NULL);

	return 0;
}

