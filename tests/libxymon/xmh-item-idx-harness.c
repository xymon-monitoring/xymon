/* Harness for tests/libxymon/xmh-item-idx.sh.
 *
 * xmh_item_idx() answers "is this hosts.cfg tag a reserved one?" for the info
 * page (web/svcstatus-info.c) and for xymonnet (xymonnet/xymonnet.c), which
 * treats any tag it does not recognize as a network test.
 *
 * Each input line is "+ TAG" (must be recognized) or "- TAG" (must not be).
 * The real lib/loadhosts.c answers; nothing here mirrors its logic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	char line[1024];
	FILE *fd;
	int failures = 0, checked = 0;

	if (argc != 2) { fprintf(stderr, "usage: %s CASES\n", argv[0]); return 2; }
	fd = fopen(argv[1], "r");
	if (!fd) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }

	while (fgets(line, sizeof(line), fd)) {
		char *tag;
		int want, got;

		line[strcspn(line, "\n")] = '\0';
		if ((strlen(line) < 3) || (line[1] != ' ')) continue;
		want = (line[0] == '+');
		tag = line + 2;

		got = (xmh_item_idx(tag) != -1);
		checked++;
		if (got != want) {
			fprintf(stderr, "%s: xmh_item_idx() calls it %s\n", tag,
				got ? "a reserved tag, but it is a test spec" : "unknown, but it is a reserved tag");
			failures++;
		}
	}
	fclose(fd);

	if (checked == 0) { fprintf(stderr, "no cases read\n"); return 2; }
	return failures ? 1 : 0;
}
