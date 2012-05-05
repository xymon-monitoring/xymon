#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#include <ldap.h>
#include <lber.h>

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
	else {
		LDAPURLDesc ludp;
		int protocol, rc;
		struct timeval nettimeout;

		protocol = LDAP_VERSION3;
		nettimeout.tv_sec = 1; nettimeout.tv_usec = 0;

		rc = ldap_url_parse("ldap://ldap.example.com/cn=xymon,ou=development,o=xymon", &ludp);
		ld = ldap_init(ludp.lud_host, ludp.lud_port);
		rc = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &protocol);
		rc = ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &nettimeout);
		rc = ldap_start_tls_s(ld, NULL, NULL);
	}

	return 0;
}

