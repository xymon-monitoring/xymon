#ifndef __PCRE_COMPAT_H__
#define __PCRE_COMPAT_H__

#ifdef PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef pcre2_code pcre_pattern_t;
typedef pcre2_match_data pcre_match_data_t;

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

/* Helper function declarations */
pcre_pattern_t *compile_pattern_with_error(const char *pattern, const char *pattern_name);
void setup_disk_patterns(pcre_pattern_t **inclpattern, pcre_pattern_t **exclpattern, int *ptnsetup);
int disk_wanted(const char *diskname, pcre_pattern_t *inclpattern, pcre_pattern_t *exclpattern, pcre_match_data_t *match_data);
pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern, const char *regex, const char *pattern_name);
int match_and_extract(const char *subject, const char *pattern, int group, char *buffer, size_t buffer_size, pcre_match_data_t **match_data);

#endif /* __PCRE_COMPAT_H__ */
