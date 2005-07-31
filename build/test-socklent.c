#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
	int connres;
	socklen_t connressize = sizeof(connres);
	int res, sockfd = 0;

	res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &connres, &connressize);

	return 0;
}

