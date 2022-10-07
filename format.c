/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_regex.h"

#include "snac.h"

/* emoticons, people laughing and such */

struct {
    const char *key;
    const char *value;
} smileys[] = {
    { ":-)",        "&#128578;" },
    { ":-D",        "&#128512;" },
    { "X-D",        "&#128518;" },
    { ";-)",        "&#128521;" },
    { "B-)",        "&#128526;" },
    { ":-(",        "&#128542;" },
    { ":-*",        "&#128536;" },
    { ":-/",        "&#128533;" },
    { "8-o",        "&#128563;" },
    { "%-)",        "&#129322;" },
    { ":_(",        "&#128546;" },
    { ":-|",        "&#128528;" },
    { ":facepalm:", "&#129318;" },
    { ":shrug:",    "&#129335;" },
    { NULL,         NULL }
};


d_char *not_really_markdown(char *content, d_char **f_content)
/* formats a content using some Markdown rules */
{
    d_char *s = NULL;
    int in_pre = 0;
    int in_blq = 0;
    xs *list;
    char *p, *v;
    xs *wrk = xs_str_new(NULL);

    {
        /* split by special markup */
        xs *sm = xs_regex_split(content,
            "(`[^`]+`|\\*\\*?[^\\*]+\\*?\\*|https?:/" "/[^[:space:]]+)");
        int n = 0;

        p = sm;
        while (xs_list_iter(&p, &v)) {
            if ((n & 0x1)) {
                /* markup */
                if (xs_startswith(v, "`")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<code>%s</code>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "**")) {
                    xs *s1 = xs_crop(xs_dup(v), 2, -2);
                    xs *s2 = xs_fmt("<b>%s</b>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "*")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<i>%s</i>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "http")) {
                    xs *s1 = xs_fmt("<a href=\"%s\">%s</a>", v, v);
                    wrk = xs_str_cat(wrk, s1);
                }
                else
                    /* what the hell is this */
                    wrk = xs_str_cat(wrk, v);
            }
            else
                /* surrounded text, copy directly */
                wrk = xs_str_cat(wrk, v);

            n++;
        }
    }

    /* now work by lines */
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

    {
        /* traditional emoticons */
        int n;

        for (n = 0; smileys[n].key; n++)
            s = xs_replace_i(s, smileys[n].key, smileys[n].value);
    }

    *f_content = s;

    return *f_content;
}
