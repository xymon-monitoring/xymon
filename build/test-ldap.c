#include <time.h>

#include <lber.h>
#include <ldap.h>

LDAPURLDesc ludp;

int main(int argc, char *argv[])
{
	LDAP *ld;

	ld = ldap_init(ludp.lud_host, ludp.lud_port);
	return 0;
}

