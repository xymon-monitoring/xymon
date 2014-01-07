#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

int main ()
{

  struct timespec ts;
  (void)clock_gettime(CLOCK_MONOTONIC, &ts);

  return 0;
}
