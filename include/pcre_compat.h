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

/* Legacy API wrappers */
pcre_pattern_t *pcre_compile_legacy(const char *pattern, int options, const char **errmsg, int *errofs, const unsigned char *tableptr);
int pcre_exec_legacy(const pcre_pattern_t *pattern, const pcre_extra *extra, const char *subject, int length, int startoffset, int options, int *ovector, int ovecsize);
void pcre_free_legacy(void *pattern);
int pcre_copy_substring_legacy(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, int buffersize);

/*
 * Legacy shim mapping
 * Disable with: -DXYMON_ENABLE_PCRE_LEGACY_SHIMS=0
 */
#ifndef XYMON_ENABLE_PCRE_LEGACY_SHIMS
#define XYMON_ENABLE_PCRE_LEGACY_SHIMS 1
#endif

#if XYMON_ENABLE_PCRE_LEGACY_SHIMS
#define pcre_compile pcre_compile_legacy
#define pcre_exec pcre_exec_legacy
#define pcre_free pcre_free_legacy
#define pcre_copy_substring pcre_copy_substring_legacy
#endif

#else

#include <pcre.h>

typedef pcre pcre_pattern_t;
typedef int pcre_match_data_t[30];

#endif

/* Compatibility API */
pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs);
int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int length, pcre_match_data_t *match_data);
pcre_match_data_t *pcre_match_data_create_compat(pcre_pattern_t *pattern);
void pcre_match_data_free_compat(pcre_match_data_t *match_data);
void pcre_free_compat(pcre_pattern_t *pattern);
int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *match_data, int stringnumber, char *buffer, size_t buffersize);

/* Safe helper wrappers */
pcre_pattern_t *pcre_compile_optional(const char *pattern, int options, const char **errmsg, int *errofs);
int pcre_exec_capture(const pcre_pattern_t *pattern, const char *subject, int *ovector, size_t ovecsize);
int pcre_copy_substring_ovector(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, size_t buffersize);
int pcre_match_pagelist(void *hostinfo, const pcre_pattern_t *pattern);
void pcre_free_pattern(pcre_pattern_t **pattern);

/* Higher-level helpers */
pcre_pattern_t *compile_pattern_with_error(const char *pattern, const char *patternname);
void setup_disk_patterns(pcre_pattern_t **inclpattern, pcre_pattern_t **exclpattern, int *ptnsetup);
int disk_wanted(const char *diskname, pcre_pattern_t *inclpattern, pcre_pattern_t *exclpattern, pcre_match_data_t *match_data);
pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern, const char *regex, const char *patternname);
int match_and_extract(const char *subject, const char *pattern, int stringnumber, char *buffer, size_t buffersize, pcre_pattern_t *compiled_pattern, pcre_match_data_t **match_data);

#endif /* __PCRE_COMPAT_H__ */
