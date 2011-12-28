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
	struct addrinfo hints, *addr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	return 0;
}

