#include <string.h>
#include <stdlib.h>
#include "pcre_compat.h"
#include "libxymon.h"

#ifdef PCRE2

static void copy_ovector_to_ints(const PCRE2_SIZE *pairs, int matchcount, int *ovector, int ovecsize) {
    int i, maxcopy;
    if (!pairs || !ovector || ovecsize <= 0) {
        return;
    }
    maxcopy = matchcount * 2;
    if (maxcopy > ovecsize) {
        maxcopy = ovecsize;
    }
    for (i = 0; i < maxcopy; i++) {
        ovector[i] = (int)pairs[i];
    }
}

pcre_pattern_t *pcre_compile_legacy(const char *pattern, int options, const char **errmsg, int *errofs, const unsigned char *tableptr) {
    static char errbuf[256];
    int errcode;
    PCRE2_SIZE erroffset = 0;
    pcre2_code *code;
    (void)tableptr;

    code = pcre2_compile((PCRE2_SPTR)pattern, (PCRE2_SIZE)strlen(pattern), (uint32_t)options, &errcode, &erroffset, NULL);

    if (!code) {
        if (errmsg) {
            errbuf[0] = '\0';
            pcre2_get_error_message(errcode, (PCRE2_UCHAR *)errbuf, sizeof(errbuf));
            *errmsg = errbuf;
        }
        if (errofs) {
            *errofs = (int)erroffset;
        }
    }

    return (pcre_pattern_t *)code;
}

int pcre_exec_legacy(const pcre_pattern_t *pattern, const pcre_extra *extra, const char *subject, int length, int startoffset, int options, int *ovector, int ovecsize) {
    int rc;
    pcre2_match_data *matchdata;
    (void)extra;

    matchdata = pcre2_match_data_create_from_pattern((pcre2_code *)pattern, NULL);
    if (!matchdata) {
        return -1;
    }

    rc = pcre2_match((pcre2_code *)pattern, (PCRE2_SPTR)subject, (PCRE2_SIZE)length, (PCRE2_SIZE)startoffset, (uint32_t)options, matchdata, NULL);

    if (rc > 0) {
        copy_ovector_to_ints(pcre2_get_ovector_pointer(matchdata), rc, ovector, ovecsize);
    }

    pcre2_match_data_free(matchdata);
    return rc;
}

void pcre_free_legacy(void *pattern) {
    pcre2_code_free((pcre2_code *)pattern);
}

int pcre_copy_substring_legacy(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, int buffersize) {
    int start, end, len;
    (void)stringcount;

    if (!subject || !ovector || !buffer || buffersize <= 0 || stringnumber < 0) {
        return -1;
    }

    if (stringcount <= 0 || stringnumber >= stringcount) {
        return -1;
    }

    start = ovector[2 * stringnumber];
    end = ovector[2 * stringnumber + 1];

    if (start < 0 || end < start) {
        return -1;
    }

    len = end - start;
    if (len >= buffersize) {
        len = buffersize - 1;
    }

    memcpy(buffer, subject + start, (size_t)len);
    buffer[len] = '\0';
    return len;
}

pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs) {
    int errcode;
    PCRE2_SIZE erroffset = 0;
    pcre2_code *code;

    code = pcre2_compile((PCRE2_SPTR)pattern, (PCRE2_SIZE)strlen(pattern), (uint32_t)options, &errcode, &erroffset, NULL);

    if (!code) {
        if (errmsg && errmsg_size > 0) {
            pcre2_get_error_message(errcode, (PCRE2_UCHAR *)errmsg, (PCRE2_SIZE)errmsg_size);
        }
        if (errofs) {
            *errofs = (int)erroffset;
        }
    }

    return (pcre_pattern_t *)code;
}

int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int length, pcre_match_data_t *matchdata) {
    return pcre2_match((pcre2_code *)pattern, (PCRE2_SPTR)subject, (PCRE2_SIZE)length, 0, 0, (pcre2_match_data *)matchdata, NULL);
}

pcre_match_data_t *pcre_match_data_create_compat(void) {
    return (pcre_match_data_t *)pcre2_match_data_create(30, NULL);
}

void pcre_match_data_free_compat(pcre_match_data_t *matchdata) {
    pcre2_match_data_free((pcre2_match_data *)matchdata);
}

void pcre_free_compat(pcre_pattern_t *pattern) {
    pcre2_code_free((pcre2_code *)pattern);
}

int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *matchdata, int stringnumber, char *buffer, size_t buffersize) {
    PCRE2_SIZE len = (PCRE2_SIZE)buffersize;
    (void)subject;

    if (pcre2_substring_copy_bynumber((pcre2_match_data *)matchdata, (uint32_t)stringnumber, (PCRE2_UCHAR *)buffer, &len) == 0) {
        return (int)len;
    }

    return -1;
}

#else

pcre_pattern_t *pcre_compile_compat(const char *pattern, int options, char *errmsg, size_t errmsg_size, int *errofs) {
    const char *err;
    pcre *regexp = pcre_compile(pattern, options, &err, errofs, NULL);

    if (!regexp && errmsg && err && errmsg_size > 0) {
        strncpy(errmsg, err, errmsg_size - 1);
        errmsg[errmsg_size - 1] = '\0';
    }

    return (pcre_pattern_t *)regexp;
}

int pcre_exec_compat(pcre_pattern_t *pattern, const char *subject, int length, pcre_match_data_t *matchdata) {
    if (!matchdata) {
        return -1;
    }

    return pcre_exec((pcre *)pattern, NULL, subject, length, 0, 0, *matchdata, 30);
}

pcre_match_data_t *pcre_match_data_create_compat(void) {
    return calloc(1, sizeof(pcre_match_data_t));
}

void pcre_match_data_free_compat(pcre_match_data_t *matchdata) {
    free(matchdata);
}

void pcre_free_compat(pcre_pattern_t *pattern) {
    pcre_free((pcre *)pattern);
}

int pcre_copy_substring_compat(const char *subject, pcre_match_data_t *matchdata, int stringnumber, char *buffer, size_t buffersize) {
    if (!matchdata) {
        return -1;
    }

    return pcre_copy_substring(subject, *matchdata, 30, stringnumber, buffer, (int)buffersize);
}

#endif

pcre_pattern_t *pcre_compile_optional(const char *pattern, int options, const char **errmsg, int *errofs) {
    if (!pattern || !*pattern) {
        return NULL;
    }

#ifdef PCRE2
    return pcre_compile_legacy(pattern, options, errmsg, errofs, NULL);
#else
    return pcre_compile(pattern, options, errmsg, errofs, NULL);
#endif
}

int pcre_exec_capture(const pcre_pattern_t *pattern, const char *subject, int *ovector, size_t ovecsize) {
    if (!pattern || !subject || !ovector || ovecsize == 0) {
        return -1;
    }

#ifdef PCRE2
    return pcre_exec_legacy(pattern, NULL, subject, (int)strlen(subject), 0, 0, ovector, (int)ovecsize);
#else
    return pcre_exec((pcre *)pattern, NULL, subject, (int)strlen(subject), 0, 0, ovector, (int)ovecsize);
#endif
}

int pcre_copy_substring_ovector(const char *subject, int *ovector, int stringcount, int stringnumber, char *buffer, size_t buffersize) {
    if (!subject || !ovector || !buffer || buffersize == 0) {
        return -1;
    }

#ifdef PCRE2
    return pcre_copy_substring_legacy(subject, ovector, stringcount, stringnumber, buffer, (int)buffersize);
#else
    return pcre_copy_substring(subject, ovector, stringcount, stringnumber, buffer, (int)buffersize);
#endif
}

int pcre_match_pagelist(void *hostinfo, const pcre_pattern_t *pattern) {
    int ovector[30];
    char *pagename;

    if (!pattern) {
        return 0;
    }

    pagename = xmh_item_multi(hostinfo, XMH_PAGEPATH);

    while (pagename) {
        if (pcre_exec_capture(pattern, pagename, ovector, sizeof(ovector) / sizeof(ovector[0])) >= 0) {
            return 1;
        }
        pagename = xmh_item_multi(NULL, XMH_PAGEPATH);
    }

    return 0;
}

void pcre_free_pattern(pcre_pattern_t **pattern) {
    if (!pattern || !*pattern) {
        return;
    }

#ifdef PCRE2
    pcre_free_legacy(*pattern);
#else
    pcre_free((pcre *)*pattern);
#endif

    *pattern = NULL;
}

pcre_pattern_t *compile_pattern_with_error(const char *pattern, const char *patternname) {
    char errmsgbuf[120];
    int errofs = 0;
    pcre_pattern_t *compiled;

    compiled = pcre_compile_compat(pattern, PCRE_CASELESS, errmsgbuf, sizeof(errmsgbuf), &errofs);

    if (!compiled) {
        errprintf("PCRE compile of %s='%s' failed, error %s, offset %d\n", patternname, pattern, errmsgbuf, errofs);
    }

    return compiled;
}

void setup_disk_patterns(pcre_pattern_t **inclpattern, pcre_pattern_t **exclpattern, int *ptnsetup) {
    char *ptn;

    if (*ptnsetup) {
        return;
    }

    *ptnsetup = 1;

    ptn = getenv("RRDDISKS");
    if (ptn && *ptn) {
        *inclpattern = compile_pattern_with_error(ptn, "RRDDISKS");
    }

    ptn = getenv("NORRDDISKS");
    if (ptn && *ptn) {
        *exclpattern = compile_pattern_with_error(ptn, "NORRDDISKS");
    }
}

int disk_wanted(const char *diskname, pcre_pattern_t *inclpattern, pcre_pattern_t *exclpattern, pcre_match_data_t *matchdata) {
    int wanted = 1;
    int len = (int)strlen(diskname);

    if (exclpattern) {
        wanted = (pcre_exec_compat(exclpattern, diskname, len, matchdata) < 0);
    }

    if (wanted && inclpattern) {
        wanted = (pcre_exec_compat(inclpattern, diskname, len, matchdata) >= 0);
    }

    return wanted;
}

pcre_pattern_t *compile_single_pattern(pcre_pattern_t **pattern, const char *regex, const char *patternname) {
    if (*pattern) {
        return *pattern;
    }

    *pattern = compile_pattern_with_error(regex, patternname);
    return *pattern;
}

int match_and_extract(const char *subject, const char *pattern, int stringnumber, char *buffer, size_t buffersize, pcre_match_data_t **matchdata) {
    pcre_pattern_t *compiled;
    int rc;

    compiled = compile_pattern_with_error(pattern, "pattern");
    if (!compiled) {
        return -1;
    }

    if (!*matchdata) {
        *matchdata = pcre_match_data_create_compat();
    }

    rc = pcre_exec_compat(compiled, subject, (int)strlen(subject), *matchdata);

    if (rc >= 0 && stringnumber > 0 && buffer) {
        pcre_copy_substring_compat(subject, *matchdata, stringnumber, buffer, buffersize);
    }

    pcre_free_compat(compiled);
    return rc;
}
