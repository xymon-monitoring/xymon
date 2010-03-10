#include <config.h>
#include <stdio.h>
#include "gettext.h"
#define _(string) gettext (string)

/**
 * Print a Hello World string.
 * \param take in nothing.
 * \return Hello World string
 */

void say_hello (void)
{
  printf("Hello World!\n");
  printf("This is %s.\n", PACKAGE_STRING );
}
