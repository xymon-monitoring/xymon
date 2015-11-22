/* A Make sure we can compile with the lzo libs 	*/
/* Works for either the full LZO library (below) or 	*/
/* just the minilzo copylib... */

#include <stdio.h>
#include <stdlib.h>

#include <lzo/lzoconf.h>
#include <lzo/lzo1x.h>
/* #include "minilzo.h" */


int main(int argc, char *argv[])
{
	return (lzo_init() == LZO_E_OK) ? 0 : 1;
}
