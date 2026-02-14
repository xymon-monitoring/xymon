#include <string.h>
#include <stdlib.h>
#include "pcre_compat.h"
#include "libxymon.h"

#ifdef PCRE2

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
    int i, rc, maxcopy;
    PCRE2_SIZE *pairs;
    pcre2_match_data *mdata;
    (void)extra;

    mdata = pcre2_match_data_create_from_pattern(code, NULL);
    if (!mdata) return -1;

    rc = pcre2_match(code, (PCRE2_SPTR)subject, length, startoffset, options, mdata, NULL);
    if ((rc > 0) && ovector && (ovecsize > 0)) {
        pairs = pcre2_get_ovector_pointer(mdata);
        maxcopy = rc * 2;
        if (maxcopy > ovecsize) maxcopy = ovecsize;
        for (i = 0; i < maxcopy; i++) ovector[i] = (int)pairs[i];
    }

    pcre2_match_data_free(mdata);
    return rc;
}

void pcre_free_legacy(void *ptr) {
    pcre2_code_free((pcre2_code *)ptr);
}

pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs) {
    int err;
    PCRE2_SIZE errofs_pcre2;
    pcre2_code *result = pcre2_compile(pattern, strlen(pattern), options, &err, &errofs_pcre2, NULL);
    if (!result && errmsg) {
        pcre2_get_error_message(err, errmsg, errmsg_size);
        *errofs = (int)errofs_pcre2;
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
    if (!result && errmsg && error) {
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
