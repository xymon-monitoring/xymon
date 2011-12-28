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
	int sock;

	sock = socket(AF_INET6, SOCK_STREAM, 0);
	return 0;
}

