#include <time.h>
#include <stdio.h>

#include <lber.h>
#define LDAP_DEPRECATED 1
#include <ldap.h>

LDAPURLDesc ludp;

int main(int argc, char *argv[])
{
	LDAP *ld;

	if (argc >= 1)
		printf("%s\n", LDAP_VENDOR_NAME);
	else
		ld = ldap_init(ludp.lud_host, ludp.lud_port);

	return 0;
}

