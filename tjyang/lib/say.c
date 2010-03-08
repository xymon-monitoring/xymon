#include <config.h>
#include <stdio.h>
#include "gettext.h"
#define _(string) gettext (string)

void say_hello (void)
{
  puts ("Hello World!\n");
  printf ("This is %s.\n", PACKAGE_STRING );
}
