#ifndef __PCRE2_API_COMPAT_H__
#define __PCRE2_API_COMPAT_H__

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/*
 * PCRE2 strings are unsigned (PCRE2_SPTR is "const unsigned char *"), C
 * strings are not, and the two stay distinct types however the compiler is
 * flagged. One cast at the boundary, written once: PCRE2STR() for pattern
 * and subject inputs, PCRE2BUF() for output buffers.
 */
#define PCRE2STR(s) ((PCRE2_SPTR)(s))
#define PCRE2BUF(s) ((PCRE2_UCHAR *)(s))

#endif
