#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	int sock = 0, opt = 1;

	setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
	return 0;
}

