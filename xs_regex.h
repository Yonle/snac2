/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_REGEX_H

#define _XS_REGEX_H

d_char *xs_regex_split_n(const char *str, const char *rx, int count);
#define xs_regex_split(str, rx) xs_regex_split_n(str, rx, 0xfffffff)
d_char *xs_regex_match_n(const char *str, const char *rx, int count);
#define xs_regex_match(str, rx) xs_regex_match_n(str, rx, 0xfffffff)

#ifdef XS_IMPLEMENTATION

#include <regex.h>

d_char *xs_regex_split_n(const char *str, const char *rx, int count)
/* splits str by regex */
{
    regex_t re;
    regmatch_t rm;
    int offset = 0;
    d_char *list = NULL;
    const char *p;

    if (regcomp(&re, rx, REG_EXTENDED))
        return NULL;

    list = xs_list_new();

    while (count > 0 && !regexec(&re, (p = str + offset), 1, &rm, offset > 0 ? REG_NOTBOL : 0)) {
        /* add first the leading part of the string */
        list = xs_list_append_m(list, p, rm.rm_so);
        list = xs_str_cat(list, "");

        /* add now the matched text as the separator */
        list = xs_list_append_m(list, p + rm.rm_so, rm.rm_eo - rm.rm_so);
        list = xs_str_cat(list, "");

        /* move forward */
        offset += rm.rm_eo;

        count--;
    }

    /* add the rest of the string */
    list = xs_list_append(list, p);

    regfree(&re);

    return list;
}


d_char *xs_regex_match_n(const char *str, const char *rx, int count)
/* returns a list with upto count matches */
{
    d_char *list = xs_list_new();
    xs *split = NULL;
    char *p, *v;
    int n = 0;

    /* split */
    p = split = xs_regex_split_n(str, rx, count);

    /* now iterate to get only the 'separators' (odd ones) */
    while (xs_list_iter(&p, &v)) {
        if (n & 0x1)
            list = xs_list_append(list, v);

        n++;
    }

    return list;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_REGEX_H */
