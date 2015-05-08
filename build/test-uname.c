#include <sys/types.h>
#include <sys/utsname.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	struct utsname u_name;

	if (uname(&u_name) == -1) return -1;
	printf("system name is: %s, node name is %s\n", u_name.sysname, u_name.nodename);
	return 0;
}

