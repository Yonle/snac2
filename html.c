/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"

#include "snac.h"

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
            "(`[^`]+`|\\*\\*?[^\\*]+\\*?\\*|https?:/" "/[^ ]*)");
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

    *f_content = s;

    return *f_content;
}


int login(snac *snac, char *headers)
/* tries a login */
{
    int logged_in = 0;
    char *auth = xs_dict_get(headers, "authorization");

    if (auth && xs_startswith(auth, "Basic ")) {
        int sz;
        xs *s1 = xs_crop(xs_dup(auth), 6, 0);
        xs *s2 = xs_base64_dec(s1, &sz);
        xs *l1 = xs_split_n(s2, ":", 1);

        if (xs_list_len(l1) == 2) {
            logged_in = check_password(
                xs_list_get(l1, 0), xs_list_get(l1, 1),
                xs_dict_get(snac->config, "passwd"));
        }
    }

    return logged_in;
}


d_char *html_msg_icon(snac *snac, d_char *s, char *msg)
{
    char *actor_id;
    xs *actor = NULL;

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(snac, actor_id, &actor))) {
        xs *name   = NULL;
        xs *avatar = NULL;
        char *v;

        /* get the name */
        if ((v = xs_dict_get(actor, "name")) == NULL) {
            if ((v = xs_dict_get(actor, "preferredUsername")) == NULL) {
                v = "user";
            }
        }

        name = xs_dup(v);

        /* get the avatar */
        if ((v = xs_dict_get(actor, "icon")) != NULL &&
            (v = xs_dict_get(v, "url")) != NULL) {
            avatar = xs_dup(v);
        }

        if (avatar == NULL)
            avatar = xs_fmt("data:image/png;base64, %s", susie);

        {
            xs *s1 = xs_fmt("<p><img class=\"snac-avatar\" src=\"%s\"/>\n", avatar);
            s = xs_str_cat(s, s1);
        }

        {
            xs *s1 = xs_fmt("<a href=\"%s\" class=\"p-author h-card snac-author\">%s</a>",
                actor_id, name);
            s = xs_str_cat(s, s1);
        }

        if (strcmp(xs_dict_get(msg, "type"), "Note") == 0) {
            xs *s1 = xs_fmt(" <a href=\"%s\">»</a>", xs_dict_get(msg, "id"));
            s = xs_str_cat(s, s1);
        }

        if (!is_msg_public(snac, msg))
            s = xs_str_cat(s, " <span title=\"private\">&#128274;</span>");

        if ((v = xs_dict_get(msg, "published")) == NULL)
            v = "&nbsp;";

        {
            xs *s1 = xs_fmt("<br>\n<time class=\"dt-published snac-pubdate\">%s</time>\n", v);
            s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    return s;
}


d_char *html_user_header(snac *snac, d_char *s, int local)
/* creates the HTML header */
{
    char *p, *v;

    s = xs_str_cat(s, "<!DOCTYPE html>\n<html>\n<head>\n");
    s = xs_str_cat(s, "<meta name=\"viewport\" "
                      "content=\"width=device-width, initial-scale=1\"/>\n");
    s = xs_str_cat(s, "<meta name=\"generator\" "
                      "content=\"" USER_AGENT "\"/>\n");

    /* add server CSS */
    p = xs_dict_get(srv_config, "cssurls");
    while (xs_list_iter(&p, &v)) {
        xs *s1 = xs_fmt("<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\"/>\n", v);
        s = xs_str_cat(s, s1);
    }

    /* add the user CSS */
    {
        xs *css = NULL;
        int size;

        if (valid_status(static_get(snac, "style.css", &css, &size))) {
            xs *s1 = xs_fmt("<style>%s</style>\n", css);
            s = xs_str_cat(s, s1);
        }
    }

    {
        xs *s1 = xs_fmt("<title>%s</title>\n", xs_dict_get(snac->config, "name"));
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</head>\n<body>\n");

    /* top nav */
    s = xs_str_cat(s, "<nav style=\"snac-top-nav\">");

    {
        xs *s1;

        if (local)
            s1 = xs_fmt("<a href=\"%s/admin\">%s</a></nav>", snac->actor, L("admin"));
        else
            s1 = xs_fmt("<a href=\"%s\">%s</a></nav>", snac->actor, L("public"));

        s = xs_str_cat(s, s1);
    }

    /* user info */
    {
        xs *bio = NULL;
        char *_tmpl =
            "<div class=\"h-card snac-top-user\">\n"
            "<p class=\"p-name snac-top-user-name\">%s</p>\n"
            "<p class=\"snac-top-user-id\">@%s@%s</p>\n"
            "<div class=\"p-note snac-top-user-bio\">%s</div>\n"
            "</div>\n";

        not_really_markdown(xs_dict_get(snac->config, "bio"), &bio);

        xs *s1 = xs_fmt(_tmpl,
            xs_dict_get(snac->config, "name"),
            xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"),
            bio
        );

        s = xs_str_cat(s, s1);
    }

    return s;
}


d_char *html_top_controls(snac *snac, d_char *s)
/* generates the top controls */
{
    char *_tmpl =
        "<div class=\"snac-top-controls\">\n"

        "<div class=\"snac-note\">\n"
        "<form method=\"post\" action=\"%s/admin/note\">\n"
        "<textarea class=\"snac-textarea\" name=\"content\" "
        "rows=\"8\" wrap=\"virtual\" required=\"required\"></textarea>\n"
        "<input type=\"hidden\" name=\"in_reply_to\" value=\"\">\n"
        "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form><p>\n"
        "</div>\n"

        "<div class=\"snac-top-controls-more\">\n"
        "<details><summary>%s</summary>\n"

        "<form method=\"post\" action=\"%s/admin/action\">\n"
        "<input type=\"text\" name=\"actor\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form></p>\n"

        "<form method=\"post\" action=\"%s\">\n"
        "<input type=\"text\" name=\"id\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form></p>\n"

        "<details><summary>%s</summary>\n"

        "<div class=\"snac-user-setup\">\n"
        "<form method=\"post\" action=\"%s/admin/user-setup\">\n"
        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"name\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"avatar\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<textarea name=\"bio\" cols=60 rows=4>%s</textarea></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd1\" value=\"\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd2\" value=\"\"></p>\n"

        "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form>\n"

        "</div>\n"
        "</details>\n"
        "</details>\n"
        "</div>\n"
        "</div>\n";

    xs *s1 = xs_fmt(_tmpl,
        snac->actor,
        L("Post"),

        L("More options..."),

        snac->actor,
        L("Follow"), L("(by URL or user@host)"),

        snac->actor,
        L("Boost"), L("(by URL)"),

        L("User setup..."),
        snac->actor,
        L("User name"),
        xs_dict_get(snac->config, "name"),
        L("Avatar URL"),
        xs_dict_get(snac->config, "avatar"),
        L("Bio"),
        xs_dict_get(snac->config, "bio"),
        L("Password (only to change it)"),
        L("Repeat Password"),
        L("Update user info")
    );

    s = xs_str_cat(s, s1);

    return s;
}


d_char *html_entry(snac *snac, d_char *s, char *msg, xs_set *seen, int level)
{
    char *id    = xs_dict_get(msg, "id");
    char *type  = xs_dict_get(msg, "type");
    char *meta  = xs_dict_get(msg, "_snac");
    xs *actor_o = NULL;
    char *actor;

    /* return if already seen */
    if (xs_set_add(seen, id) == 0)
        return s;

    if (strcmp(type, "Follow") == 0)
        return s;

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return s;

    if (!valid_status(actor_get(snac, actor, &actor_o)))
        return s;

    if (level == 0) {
        char *referrer;

        s = xs_str_cat(s, "<div class=\"snac-post\">\n");

        /* print the origin of the post, if any */
        if ((referrer = xs_dict_get(meta, "referrer")) != NULL) {
            xs *actor_r = NULL;

            if (valid_status(actor_get(snac, referrer, &actor_r))) {
                char *name;

                if ((name = xs_dict_get(actor_r, "name")) == NULL)
                    name = xs_dict_get(actor_r, "preferredUsername");

                xs *s1 = xs_fmt(
                    "<div class=\"snac-origin\">\n"
                    "<a href=\"%s\">%s</a> %s</div>",
                    xs_dict_get(actor_r, "id"),
                    name,
                    "boosted"
                );

                s = xs_str_cat(s, s1);
            }
        }
    }
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n");

    s = html_msg_icon(snac, s, msg);

    /* add the content */
    {
        xs *c = xs_dup(xs_dict_get(msg, "content"));

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop(c, 0, -4);

        c = xs_replace_i(c, "<br><br>", "<p>");

        if (!xs_startswith(c, "<p>")) {
            xs *s1 = c;
            c = xs_fmt("<p>%s</p>", s1);
        }

        xs *s1 = xs_fmt("<div class=\"e-content snac-content\">\n%s", c);

        s = xs_str_cat(s, s1);

        s = xs_str_cat(s, "</div>\n");
    }

    s = xs_str_cat(s, "</div>\n");

    return s;
}


d_char *html_timeline(snac *snac, char *list, int local)
/* returns the HTML for the timeline */
{
    d_char *s = xs_str_new(NULL);
    xs_set *seen = xs_set_new(4096);
    char *v;

    s = html_user_header(snac, s, local);

    if (!local)
        s = html_top_controls(snac, s);

    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&list, &v)) {
        xs *msg = timeline_get(snac, v);

        s = html_entry(snac, s, msg, seen, 0);
    }

    s = xs_str_cat(s, "</div>\n");

#if 0
    s = xs_str_cat(s, "<h1>HI</h1>\n");

    s = xs_str_cat(s, xs_fmt("len() == %d\n", xs_list_len(list)));

    {
        char *i = xs_list_get(list, 0);
        xs *msg = timeline_get(snac, i);

        s = html_msg_icon(snac, s, msg);
    }

    s = xs_str_cat(s, "</html>\n");
#endif

    {
        /* footer */
        xs *s1 = xs_fmt(
            "<div class=\"snac-footer\">\n"
            "<a href=\"%s\">%s</a> - "
            "powered by <abbr title=\"Social Networks Are Crap\">snac</abbr></div>\n",
            srv_baseurl,
            L("about this site")
        );

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</body>\n</html>\n");

    xs_set_free(seen);

    return s;
}


int html_get_handler(d_char *req, char *q_path, char **body, int *b_size, char **ctype)
{
    int status = 404;
    snac snac;
    char *uid, *p_path;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    if (p_path == NULL) {
        /* public timeline */
        xs *list = local_list(&snac, 0xfffffff);

        *body   = html_timeline(&snac, list, 1);
        *b_size = strlen(*body);
        status  = 200;
    }
    else
    if (strcmp(p_path, "admin") == 0) {
        /* private timeline */

        if (!login(&snac, req))
            status = 401;
        else {
            xs *list = timeline_list(&snac, 0xfffffff);

            *body   = html_timeline(&snac, list, 0);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/") == 0) {
        /* a timeline with just one entry */
    }
    else
    if (xs_startswith(p_path, "s/") == 0) {
        /* a static file */
    }
    else
    if (xs_startswith(p_path, "h/") == 0) {
        /* an entry from the history */
    }
    else
        status = 404;

    user_free(&snac);

    if (valid_status(status)) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(d_char *req, char *q_path, d_char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    int status = 0;

    return status;
}

