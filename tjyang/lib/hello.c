/**
 * \file hello.c to demostrate C and Doxgen documentation.
 * \author T.J. Yang
 * \date 3-08-2010
 */
/** http://www.upl.cs.wisc.edu/tutorials/doxygen/handout/doxygentut.html
 */
#include <config.h>
#include <locale.h>
#include "gettext.h"
/** to say_hello is defined in say.h */
#include "say.h"
int
main(void)
{
  setlocale (LC_ALL,"");
  bindtextdomain(PACKAGE,LOCALEDIR);
  textdomain(PACKAGE);
  say_hello(); /** print  hello */
  return 0;
}



