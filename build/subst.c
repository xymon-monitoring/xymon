/*
 * build/subst.c
 *
 * Substitute @NAME@ placeholders in a template read from stdin, writing the
 * result to stdout. Each argument is NAME=VALUE; every @NAME@ whose NAME was
 * given is replaced with VALUE literally -- no shell, sed or regular-expression
 * metacharacter is special, so a value containing &, !, a backslash, a space or
 * a slash is copied through verbatim. This is what the Makefiles use to generate
 * config files and the systemd unit from their .DIST templates, where the old
 * "sed -e 's!@X@!$(X)!g'" silently mangled a value carrying one of those.
 *
 * A @NAME@ with no matching argument is left as it stands, so a stray @ or an
 * unrelated token is untouched; the generated text is never rescanned, so a
 * value that itself contains @...@ is copied, not re-substituted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char *argv[])
{
	int c;

	while ((c = getchar()) != EOF) {
		char name[256];
		int n = 0, ch;

		if (c != '@') { putchar(c); continue; }

		/* Read a candidate name: @[A-Za-z0-9_]*@ */
		while ((ch = getchar()) != EOF && (isalnum(ch) || ch == '_')) {
			if (n < (int)sizeof(name) - 1) name[n++] = ch;
		}
		name[n] = '\0';

		if (ch == '@') {
			const char *val = NULL;
			int i;

			for (i = 1; i < argc; i++) {
				if ((strncmp(argv[i], name, n) == 0) && (argv[i][n] == '=')) {
					val = argv[i] + n + 1;
					break;
				}
			}
			if (val) fputs(val, stdout);
			else { putchar('@'); fputs(name, stdout); putchar('@'); }
		}
		else {
			/* Not a closed @NAME@: emit what was consumed, verbatim. */
			putchar('@');
			fputs(name, stdout);
			if (ch != EOF) putchar(ch);
		}
	}
	return 0;
}
