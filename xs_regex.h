/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_REGEX_H

#define _XS_REGEX_H

d_char *xs_regex_match(char *str, char *rx, int count);
#define xs_regex_matchall(str, rx) xs_regex_match(str, rx, 0xfffffff)

#ifdef XS_IMPLEMENTATION

#include <regex.h>

d_char *xs_regex_match(char *str, char *rx, int count)
/* returns a list with upto count matches */
{
    regex_t re;
    regmatch_t rm;
    d_char *list = NULL;
    int offset = 0;
    char *p;

    if (regcomp(&re, rx, REG_EXTENDED))
        return NULL;

    list = xs_list_new();

    while (count > 0 && !regexec(&re, (p = str + offset), 1, &rm, offset > 0 ? REG_NOTBOL : 0)) {
        /* add the first part */
        list = xs_list_append_m(list, p + rm.rm_so, rm.rm_eo - rm.rm_so);

        /* add the asciiz */
        list = xs_str_cat(list, "");

        offset += rm.rm_eo;

        count--;
    }

    regfree(&re);

    return list;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_REGEX_H */
