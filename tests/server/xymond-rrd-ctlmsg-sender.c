/* For tests/server/xymond-rrd-ctlmsg.sh: sends each argument after the socket
 * path argv[1] as one datagram to that socket. An argument "-NUMBER" sends a
 * message of NUMBER bytes instead: "/", then 'a's, then "/". Exits 1 when a
 * send fails.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
	struct sockaddr_un addr;
	int s, i;

	if ((argc < 3) || (strlen(argv[1]) >= sizeof(addr.sun_path))) return 2;

	s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (s == -1) { perror("socket"); return 2; }
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, argv[1]);

	for (i = 2; (i < argc); i++) {
		char *msg = argv[i];
		size_t len = strlen(msg);

		if (msg[0] == '-') {
			len = (size_t)atol(msg + 1);
			if (len < 2) return 2;
			msg = malloc(len);
			if (!msg) return 2;
			memset(msg, 'a', len);
			msg[0] = msg[len - 1] = '/';
		}
		if (sendto(s, msg, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			perror("sendto");
			return 1;
		}
	}

	return 0;
}
