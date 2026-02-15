#include <string.h>
#include <stdlib.h>
#include "pcre_compat.h"
#include "libxymon.h"

#ifdef PCRE2

static void copy_ovector_to_ints(PCRE2_SIZE *offset_pairs, int pair_count, int *match_offsets, int offset_count) {
    int i, max_copy;

    if (!offset_pairs || !match_offsets || (offset_count <= 0)) return;

    max_copy = pair_count * 2;
    if (max_copy > offset_count) max_copy = offset_count;
    for (i = 0; i < max_copy; i++) match_offsets[i] = (int)offset_pairs[i];
}

pcre_pattern_t *pcre_compile_legacy(const char *pattern, int options, const char **errmsg, int *errofs, const unsigned char *tableptr) {
    static char errbuf[256];
    int err;
    PCRE2_SIZE errofs_pcre2 = 0;
    (void)tableptr;

    pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, strlen(pattern), options, &err, &errofs_pcre2, NULL);
    if (!code) {
        if (errmsg) {
            errbuf[0] = '\0';
            pcre2_get_error_message(err, (PCRE2_UCHAR *)errbuf, sizeof(errbuf));
            *errmsg = errbuf;
        }
        if (errofs) *errofs = (int)errofs_pcre2;
    }
    return code;
}

int pcre_exec_legacy(const pcre_pattern_t *code, const pcre_extra *extra, const char *subject, int length, int startoffset, int options, int *ovector, int ovecsize) {
    int rc;
    pcre2_match_data *mdata;
    (void)extra;

    mdata = pcre2_match_data_create_from_pattern(code, NULL);
    if (!mdata) return -1;

    rc = pcre2_match(code, (PCRE2_SPTR)subject, length, startoffset, options, mdata, NULL);
    if (rc > 0) copy_ovector_to_ints(pcre2_get_ovector_pointer(mdata), rc, ovector, ovecsize);

    pcre2_match_data_free(mdata);
    return rc;
}

void pcre_free_legacy(void *ptr) {
    pcre2_code_free((pcre2_code *)ptr);
}

int pcre_copy_substring_legacy(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, int buffersize) {
    int start, end, len;
    (void)stringcount;

    if (!subject || !ovector || !buffer || (buffersize <= 0) || (stringnumber < 0)) return -1;
    if ((stringcount <= 0) || (stringnumber >= stringcount)) return -1;

    start = ovector[2 * stringnumber];
    end = ovector[2 * stringnumber + 1];
    if ((start < 0) || (end < start)) return -1;

    len = end - start;
    if (len >= buffersize) len = buffersize - 1;
    memcpy(buffer, subject + start, (size_t)len);
    buffer[len] = '\0';
    return len;
}

pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs) {
    int err;
    PCRE2_SIZE errofs_pcre2;
    pcre2_code *result = pcre2_compile(pattern, strlen(pattern), options, &err, &errofs_pcre2, NULL);
    if (!result) {
        if (errmsg && (errmsg_size > 0)) pcre2_get_error_message(err, (PCRE2_UCHAR *)errmsg, errmsg_size);
        if (errofs) *errofs = (int)errofs_pcre2;
    }
    return result;
}

int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int subject_len, pcre_match_data_t *match_data) {
    return pcre2_match(pattern, subject, subject_len, 0, 0, match_data, NULL);
}

pcre_match_data_t *pcre_match_data_create_compat(void) {
    return pcre2_match_data_create(30, NULL);
}

void pcre_match_data_free_compat(pcre_match_data_t *match_data) {
    pcre2_match_data_free(match_data);
}

void pcre_free_compat(pcre_pattern_t *pattern) {
    pcre2_code_free(pattern);
}

int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *match_data, int n, char *buffer, size_t buffer_size) {
    PCRE2_SIZE size = buffer_size;
    return (pcre2_substring_copy_bynumber(match_data, n, buffer, &size) == 0) ? (int)size : -1;
}

#else

pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs) {
    const char *error;
    pcre *result = pcre_compile(pattern, options, &error, errofs, NULL);
    if (!result && errmsg && error && (errmsg_size > 0)) {
        strncpy(errmsg, error, errmsg_size - 1);
        errmsg[errmsg_size - 1] = '\0';
    }
    return result;
}

int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int subject_len, pcre_match_data_t *match_data) {
    if (!match_data) return -1;
    return pcre_exec(pattern, NULL, subject, subject_len, 0, 0, *match_data, 30);
}

pcre_match_data_t *pcre_match_data_create_compat(void) {
    return calloc(1, sizeof(pcre_match_data_t));
}

void pcre_match_data_free_compat(pcre_match_data_t *match_data) {
    free(match_data);
}

void pcre_free_compat(pcre_pattern_t *pattern) {
    pcre_free(pattern);
}

int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *match_data, int n, char *buffer, size_t buffer_size) {
    if (!match_data) return -1;
    return pcre_copy_substring(subject, *match_data, 30, n, buffer, (int)buffer_size);
}

#endif

pcre_pattern_t *pcre_compile_optional(const char *pattern, int options, const char **errmsg, int *errofs) {
    if (!pattern || !*pattern) return NULL;
#ifdef PCRE2
    return pcre_compile_legacy(pattern, options, errmsg, errofs, NULL);
#else
    return pcre_compile(pattern, options, errmsg, errofs, NULL);
#endif
}

int pcre_exec_capture(const pcre_pattern_t *pattern, const char *subject, int *ovector, size_t ovector_size) {
    if (!pattern || !subject || !ovector || (ovector_size == 0)) return -1;
#ifdef PCRE2
    return pcre_exec_legacy(pattern, NULL, subject, strlen(subject), 0, 0, ovector, (int)ovector_size);
#else
    return pcre_exec(pattern, NULL, subject, strlen(subject), 0, 0, ovector, (int)ovector_size);
#endif
}

int pcre_copy_substring_ovector(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, size_t buffer_size) {
    if (!subject || !ovector || !buffer || (buffer_size == 0)) return -1;
#ifdef PCRE2
    return pcre_copy_substring_legacy(subject, ovector, stringcount, stringnumber, buffer, (int)buffer_size);
#else
    return pcre_copy_substring(subject, ovector, stringcount, stringnumber, buffer, (int)buffer_size);
#endif
}

int pcre_match_pagelist(void *host_info, const pcre_pattern_t *pattern) {
    int match_offsets[30];
    char *page_name;

    if (!pattern) return 0;

    page_name = xmh_item_multi(host_info, XMH_PAGEPATH);
    while (page_name) {
        if (pcre_exec_capture(pattern, page_name, match_offsets, (sizeof(match_offsets) / sizeof(match_offsets[0]))) >= 0) return 1;
        page_name = xmh_item_multi(NULL, XMH_PAGEPATH);
    }

    return 0;
}

void pcre_free_pattern(pcre_pattern_t **pattern) {
    if (pattern && *pattern) {
#ifdef PCRE2
        pcre_free_legacy(*pattern);
#else
        pcre_free(*pattern);
#endif
        *pattern = NULL;
    }
}

/* Helper function implementations */
pcre_pattern_t *compile_pattern_with_error(const char *pattern, const char *pattern_name) {
    char errmsg[120];
    int errofs;
    pcre_pattern_t *compiled = pcre_compile_compat(pattern, PCRE_CASELESS, errmsg, sizeof(errmsg), &errofs);
    if (!compiled) {
        errprintf("PCRE compile of %s='%s' failed, error %s, offset %d\n", pattern_name, pattern, errmsg, errofs);
    }
    return compiled;
}

void setup_disk_patterns(pcre_pattern_t **inclpattern, pcre_pattern_t **exclpattern, int *ptnsetup) {
    if (!*ptnsetup) {
        char *ptn;

        *ptnsetup = 1;
        ptn = getenv("RRDDISKS");
        if (ptn && strlen(ptn)) {
            *inclpattern = compile_pattern_with_error(ptn, "RRDDISKS");
        }
        ptn = getenv("NORRDDISKS");
        if (ptn && strlen(ptn)) {
            *exclpattern = compile_pattern_with_error(ptn, "NORRDDISKS");
        }
    }
}

int disk_wanted(const char *diskname, pcre_pattern_t *inclpattern, pcre_pattern_t *exclpattern, pcre_match_data_t *match_data) {
    int wanted = 1;

    if (exclpattern) {
        wanted = (pcre_exec_compat(exclpattern, diskname, strlen(diskname), match_data) < 0);
    }
    if (wanted && inclpattern) {
        wanted = (pcre_exec_compat(inclpattern, diskname, strlen(diskname), match_data) >= 0);
    }

    return wanted;
}

pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern, const char *regex, const char *pattern_name) {
    if (*pattern == NULL) {
        *pattern = compile_pattern_with_error(regex, pattern_name);
    }
    return *pattern;
}

int match_and_extract(const char *subject, const char *pattern, int group, char *buffer, size_t buffer_size, pcre_match_data_t **match_data) {
    pcre_pattern_t *compiled = compile_pattern_with_error(pattern, "pattern");
    if (!compiled) return -1;

    if (!*match_data) *match_data = pcre_match_data_create_compat();

    int result = pcre_exec_compat(compiled, subject, strlen(subject), *match_data);
    if (result >= 0 && group > 0 && buffer) {
        pcre_copy_substring_compat(subject, *match_data, group, buffer, buffer_size);
    }

    pcre_free_compat(compiled);
    return result;
}
