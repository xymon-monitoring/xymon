/* Prints the size of sockaddr_un.sun_path on this platform: 108 on Linux,
 * 104 on the BSDs and macOS, and other values elsewhere. Tests that pin a
 * socket-path boundary build their paths from it rather than assume one.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

int main(void)
{
	printf("%d\n", (int)sizeof(((struct sockaddr_un *)0)->sun_path));
	return 0;
}
