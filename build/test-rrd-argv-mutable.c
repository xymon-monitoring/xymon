/* configure: does rrd.h declare rrd_update() with "char **"? */
#include <rrd.h>
int rrd_update(int, char **);
int main(void) { return 0; }
