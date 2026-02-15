#ifndef __PCRE_COMPAT_H__
#define __PCRE_COMPAT_H__

#ifdef PCRE2

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef pcre2_code pcre_pattern_t;
typedef pcre2_match_data pcre_match_data_t;
typedef void pcre_extra;

/* Keep legacy type name available for existing call sites. */
typedef pcre_pattern_t pcre;

/* Legacy PCRE option compatibility */
#ifndef PCRE_CASELESS
#define PCRE_CASELESS PCRE2_CASELESS
#endif
#ifndef PCRE_MULTILINE
#define PCRE_MULTILINE PCRE2_MULTILINE
#endif
#ifndef PCRE_FIRSTLINE
#define PCRE_FIRSTLINE PCRE2_FIRSTLINE
#endif

/* Legacy API wrappers (implemented in pcre_compat.c) */
pcre_pattern_t *pcre_compile_legacy(const char *pattern, int options,
				    const char **errmsg, int *errofs,
				    const unsigned char *tableptr);
int pcre_exec_legacy(const pcre_pattern_t *pattern, const pcre_extra *extra,
		     const char *subject, int length, int startoffset,
		     int options, int *ovector, int ovecsize);
void pcre_free_legacy(const pcre_pattern_t *pattern);
int pcre_copy_substring_legacy(const char *subject, int *ovector,
			       int stringcount, int stringnumber,
			       char *buffer, int buffersize);

#else  /* PCRE1 */

#include <pcre.h>

typedef pcre pcre_pattern_t;
typedef int pcre_match_data_t[30];

/* PCRE1 fallback aliases */
#define pcre_compile_legacy(pattern, options, errmsg, errofs, tableptr) \
	pcre_compile((pattern), (options), (errmsg), (errofs), (tableptr))

#define pcre_exec_legacy(pattern, extra, subject, length, startoffset, options, ovector, ovecsize) \
	pcre_exec((pattern), (extra), (subject), (length), (startoffset), (options), (ovector), (ovecsize))

#define pcre_free_legacy(pattern) \
	pcre_free((void *)(pattern))

#define pcre_copy_substring_legacy(subject, ovector, stringcount, stringnumber, buffer, buffersize) \
	pcre_copy_substring((subject), (ovector), (stringcount), (stringnumber), (buffer), (buffersize))

#endif  /* PCRE2 */


/* Compatibility API */
pcre_pattern_t *pcre_compile_compat(const char *pattern, int options,
				    char *errmsg, size_t errmsg_size,
				    int *errofs);
int pcre_exec_compat(const pcre_pattern_t *pattern, const char *subject,
		     int length, pcre_match_data_t *match_data);
pcre_match_data_t *pcre_match_data_create_compat(const pcre_pattern_t *pattern);
void pcre_match_data_free_compat(pcre_match_data_t *match_data);
void pcre_free_compat(const pcre_pattern_t *pattern);
int pcre_copy_substring_compat(const char *subject,
			       pcre_match_data_t *match_data,
			       int stringnumber,
			       char *buffer,
			       size_t buffersize);

/* Safe helper wrappers */
pcre_pattern_t *pcre_compile_optional(const char *pattern, int options,
				      const char **errmsg, int *errofs);
int pcre_exec_capture(const pcre_pattern_t *pattern, const char *subject,
		      int *ovector, size_t ovecsize);
int pcre_copy_substring_ovector(const char *subject, int *ovector,
				int stringcount, int stringnumber,
				char *buffer, size_t buffersize);
int pcre_match_pagelist(void *hostinfo, const pcre_pattern_t *pattern);
void pcre_free_pattern(pcre_pattern_t **pattern);

/* Higher-level helpers */
pcre_pattern_t *compile_pattern_with_error(const char *pattern,
					   const char *patternname);
void setup_disk_patterns(pcre_pattern_t **inclpattern,
			 pcre_pattern_t **exclpattern,
			 int *ptnsetup);
int disk_wanted(const char *diskname,
		pcre_pattern_t *inclpattern,
		pcre_pattern_t *exclpattern,
		pcre_match_data_t *match_data);
pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern,
				       const char *regex,
				       const char *patternname);
int match_and_extract(const char *subject,
		      const char *pattern,
		      int stringnumber,
		      char *buffer,
		      size_t buffersize,
		      pcre_pattern_t *compiled_pattern,
		      pcre_match_data_t **match_data);

#endif /* __PCRE_COMPAT_H__ */

