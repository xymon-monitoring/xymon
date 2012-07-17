#include <lber.h>

int main(int argc, char **argv) {
	BerElement *dummy;

	dummy = ber_init(NULL);
	return 0;
}
