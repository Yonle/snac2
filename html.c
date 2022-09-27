/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_regex.h"

#include "snac.h"

d_char *not_really_markdown(char *content, d_char **f_content)
/* formats a content using some Markdown rules */
{
    d_char *s = NULL;
    int in_pre = 0;
    int in_blq = 0;
    xs *list;
    char *p, *v;
    xs *wrk = xs_dup(content);

    /* global changes */
    {
        /* backticks */
        xs *ml = xs_regex_matchall(wrk, "`[^`]+`");
        p = ml;

        while (xs_list_iter(&p, &v)) {
            xs *s1 = xs_crop(xs_dup(v), 1, -1);
            xs *s2 = xs_fmt("<code>%s</code>", s1);

            wrk = xs_replace_i(wrk, v, s2);
        }
    }

    {
        /* double asterisks */
        xs *ml = xs_regex_matchall(wrk, "\\*\\*[^\\*]+\\*\\*");
        p = ml;

        while (xs_list_iter(&p, &v)) {
            xs *s1 = xs_crop(xs_dup(v), 2, -2);
            xs *s2 = xs_fmt("<b>%s</b>", s1);

            wrk = xs_replace_i(wrk, v, s2);
        }
    }

    {
        /* single asterisks */
        xs *ml = xs_regex_matchall(wrk, "\\*[^\\*]+\\*");
        p = ml;

        while (xs_list_iter(&p, &v)) {
            xs *s1 = xs_crop(xs_dup(v), 1, -1);
            xs *s2 = xs_fmt("<i>%s</i>", s1);

            wrk = xs_replace_i(wrk, v, s2);
        }
    }

#if 0
    {
        /* urls */
        xs *done = xs_list_new();
        xs *ml = xs_regex_matchall(wrk, "https?:/" "/[^ ]+");
        p = ml;

        while (xs_list_iter(&p, &v)) {
            if (xs_list_in(done, v) == -1) {
                xs *s2 = xs_fmt("<a href=\"%s\">%s</a>", v, v);
                wrk = xs_replace_i(wrk, v, s2);

                /* keep track of already done replaces */
                done = xs_list_append(done, v);
            }
        }
    }
#endif

    /* now work on lines */

    p = list = xs_split(wrk, "\n");

    s = xs_str_new(NULL);

    while (xs_list_iter(&p, &v)) {
        xs *ss = xs_strip(xs_dup(v));

        if (xs_startswith(ss, "```")) {
            if (!in_pre)
                s = xs_str_cat(s, "<pre>");
            else
                s = xs_str_cat(s, "</pre>");

            in_pre = !in_pre;
            continue;
        }

        if (xs_startswith(ss, ">")) {
            /* delete the > and subsequent spaces */
            ss = xs_strip(xs_crop(ss, 1, 0));

            if (!in_blq) {
                s = xs_str_cat(s, "<blockquote>");
                in_blq = 1;
            }

            s = xs_str_cat(s, ss);
            s = xs_str_cat(s, "<br>");

            continue;
        }

        if (in_blq) {
            s = xs_str_cat(s, "</blockquote>");
            in_blq = 0;
        }

        s = xs_str_cat(s, ss);
        s = xs_str_cat(s, "<br>");
    }

    if (in_blq)
        s = xs_str_cat(s, "</blockquote>");
    if (in_pre)
        s = xs_str_cat(s, "</pre>");

    /* some beauty fixes */
    s = xs_replace_i(s, "</blockquote><br>", "</blockquote>");

    *f_content = s;

    return *f_content;
}
