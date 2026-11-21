/*
 * tests/libxymon/quote-table-size-harness.c -- for quote-table-size.sh
 *
 * Includes lib/strfunc.c, so it can call its static quote() with a table of
 * argv[1] entries, each turning one letter into "<letter>". Prints the
 * escaped alphabet, or, with "null" as argv[2], what quote() returns for a
 * NULL input.
 */
#include "../../lib/strfunc.c"

int main(int argc, char **argv)
{
	static qsub_t subs[32];
	static char reps[32][4];
	static strbuffer_t *result = NULL;
	int n, i;

	if (argc < 2) return 2;
	n = atoi(argv[1]);
	if ((n < 0) || (n > 26)) return 2;
	for (i = 0; i < n; i++) {
		subs[i].c = 'a' + i;
		snprintf(reps[i], sizeof(reps[i]), "<%c>", 'a' + i);
		subs[i].rep = reps[i];
	}
	subs[n].c = 0;
	if ((argc > 2) && (strcmp(argv[2], "null") == 0)) {
		char *r = quote(&result, NULL, subs, 1);
		printf("%s\n", r ? r : "(NULL)");
	}
	else printf("%s\n", quote(&result, "abcdefghijklmnopqrstuvwxyz", subs, 1));
	return 0;
}
