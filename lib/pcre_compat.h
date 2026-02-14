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
/* Legacy PCRE option name compatibility for existing call sites. */
#ifndef PCRE_CASELESS
#define PCRE_CASELESS PCRE2_CASELESS
#endif
#ifndef PCRE_MULTILINE
#define PCRE_MULTILINE PCRE2_MULTILINE
#endif
#ifndef PCRE_FIRSTLINE
#define PCRE_FIRSTLINE PCRE2_FIRSTLINE
#endif

/* Legacy API wrappers used by existing call sites when building with PCRE2. */
pcre_pattern_t *pcre_compile_legacy(const char *pattern, int options, const char **errmsg, int *errofs, const unsigned char *tableptr);
int pcre_exec_legacy(const pcre_pattern_t *code, const pcre_extra *extra, const char *subject, int length, int startoffset, int options, int *ovector, int ovecsize);
void pcre_free_legacy(void *ptr);
int pcre_copy_substring_legacy(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, int buffersize);

/*
 * Legacy shim mapping:
 * Keep default-on for untouched code paths, but make it trivially disableable
 * in the future when all call sites use compat APIs.
 */
#ifndef XYMON_DISABLE_PCRE_LEGACY_SHIMS
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

/* Function declarations */
pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs);
int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int subject_len, pcre_match_data_t *match_data);
pcre_match_data_t *pcre_match_data_create_compat(void);
void pcre_match_data_free_compat(pcre_match_data_t *match_data);
void pcre_free_compat(pcre_pattern_t *pattern);
int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *match_data, int n, char *buffer, size_t buffer_size);

/* Legacy-call-site helpers for compile/match/free patterns safely. */
pcre_pattern_t *pcre_compile_optional(const char *pattern, int options, const char **errmsg, int *errofs);
int pcre_exec_match(const pcre_pattern_t *pattern, const char *subject, int *ovector, size_t ovector_size);
int pcre_match_pagelist(void *hostinfo, const pcre_pattern_t *pattern);
void pcre_free_pattern(pcre_pattern_t **pattern);

/* Helper function declarations */
pcre_pattern_t *compile_pattern_with_error(const char *pattern, const char *pattern_name);
void setup_disk_patterns(pcre_pattern_t **inclpattern, pcre_pattern_t **exclpattern, int *ptnsetup);
int disk_wanted(const char *diskname, pcre_pattern_t *inclpattern, pcre_pattern_t *exclpattern, pcre_match_data_t *match_data);
pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern, const char *regex, const char *pattern_name);
int match_and_extract(const char *subject, const char *pattern, int group, char *buffer, size_t buffer_size, pcre_match_data_t **match_data);

#endif /* __PCRE_COMPAT_H__ */
