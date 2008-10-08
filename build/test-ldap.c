#include <time.h>
#include <stdio.h>

#include <ldap.h>
#include <lber.h>

LDAPURLDesc ludp;

int main(int argc, char *argv[])
{
	LDAP *ld;

	if ((argc >= 1) && (strcmp(argv[1], "vendor") == 0)) {
		printf("%s\n", LDAP_VENDOR_NAME);
	}
	else if ((argc >= 1) && (strcmp(argv[1], "version") == 0)) {
		printf("%d\n", LDAP_VENDOR_VERSION);
	}
	else if ((argc >= 1) && (strcmp(argv[1], "flags") == 0)) {
		if ((strcasecmp(LDAP_VENDOR_NAME, "OpenLDAP") == 0) && (LDAP_VENDOR_VERSION >= 20400)) {
			printf("-DLDAP_DEPRECATED=1\n");
		}
	}
	else
		ld = ldap_init(ludp.lud_host, ludp.lud_port);

	return 0;
}

