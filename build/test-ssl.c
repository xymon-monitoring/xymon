#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#if !defined(OPENSSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER < 0x00905000L)
#error SSL-protocol testing requires OpenSSL version 0.9.5 or later
#endif

int main(int argc, char *argv[])
{
	SSL_library_init();
	return 0;
}

