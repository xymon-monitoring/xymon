#include <time.h>
#include <stdio.h>

#include <ldap.h>
#include <lber.h>

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

