#include <lber.h>

int main(int argc, char **argv) {
	BerElement *dummy;
	char *foo = "bar";

	dummy = ber_init(ber_bvstrdup(foo));
	return 0;
}
