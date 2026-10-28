/* Stands in for xymond_rrd's cache-control socket, for
 * tests/web/showgraph-cacheflush.sh: binds a datagram socket at argv[1],
 * prints the first request it receives, and exits 0. Exits 1 when nothing
 * arrives within ten seconds.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static void timeout(int sig)
{
	(void)sig;
	_exit(1);
}

int main(int argc, char *argv[])
{
	struct sockaddr_un addr;
	char buf[1024];
	ssize_t n;
	int s;

	if (argc != 2 || strlen(argv[1]) >= sizeof(addr.sun_path)) return 2;

	s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (s == -1) { perror("socket"); return 2; }
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, argv[1]);
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) { perror("bind"); return 2; }

	signal(SIGALRM, timeout);
	alarm(10);
	n = recv(s, buf, sizeof(buf) - 1, 0);
	if (n < 0) { perror("recv"); return 2; }
	buf[n] = '\0';
	printf("%s\n", buf);
	unlink(argv[1]);
	return 0;
}
