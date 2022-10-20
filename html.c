/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_openssl.h"
#include "xs_time.h"
#include "xs_mime.h"

#include "snac.h"

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


d_char *html_msg_icon(snac *snac, d_char *os, char *msg)
{
    char *actor_id;
    xs *actor = NULL;

    xs *s = xs_str_new(NULL);

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(snac, actor_id, &actor))) {
        xs *name   = NULL;
        xs *avatar = NULL;
        char *p, *v;

        /* get the name */
        if (xs_is_null((v = xs_dict_get(actor, "name"))) || *v == '\0') {
            if (xs_is_null(v = xs_dict_get(actor, "preferredUsername"))) {
                v = "user";
            }
        }

        name = xs_dup(v);

        /* replace the :shortnames: */
        if (!xs_is_null(p = xs_dict_get(actor, "tag"))) {
            /* iterate the tags */
            while (xs_list_iter(&p, &v)) {
                char *t = xs_dict_get(v, "type");

                if (t && strcmp(t, "Emoji") == 0) {
                    char *n = xs_dict_get(v, "name");
                    char *i = xs_dict_get(v, "icon");

                    if (n && i) {
                        char *u = xs_dict_get(i, "url");
                        xs *img = xs_fmt("<img src=\"%s\" style=\"height: 1em\"/>", u);

                        name = xs_replace_i(name, n, img);
                    }
                }
            }
        }

        /* get the avatar */
        if ((v = xs_dict_get(actor, "icon")) != NULL &&
            (v = xs_dict_get(v, "url")) != NULL) {
            avatar = xs_dup(v);
        }

        if (avatar == NULL)
            avatar = xs_fmt("data:image/png;base64, %s", susie);

        {
            xs *s1 = xs_fmt("<p><img class=\"snac-avatar\" src=\"%s\" alt=\"\"/>\n", avatar);
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

        if ((v = xs_dict_get(msg, "published")) == NULL) {
            s = xs_str_cat(s, "<br>\n<time>&nbsp;</time>\n");
        }
        else {
            xs *sd = xs_crop(xs_dup(v), 0, 10);
            xs *s1 = xs_fmt(
                "<br>\n<time class=\"dt-published snac-pubdate\">%s</time>\n", sd);

            s = xs_str_cat(s, s1);
        }
    }

    return xs_str_cat(os, s);
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
    s = xs_str_cat(s, "<nav class=\"snac-top-nav\">");

    {
        xs *s1;

        if (local)
            s1 = xs_fmt("<a href=\"%s/admin\">%s</a></nav>\n", snac->actor, L("admin"));
        else
            s1 = xs_fmt("<a href=\"%s\">%s</a></nav>\n", snac->actor, L("public"));

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
        "<form method=\"post\" action=\"%s/admin/note\" enctype=\"multipart/form-data\">\n"
        "<textarea class=\"snac-textarea\" name=\"content\" "
        "rows=\"8\" wrap=\"virtual\" required=\"required\"></textarea>\n"
        "<input type=\"hidden\" name=\"in_reply_to\" value=\"\">\n"
        "<p><input type=\"file\" name=\"attach\">\n"
        "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form><p>\n"
        "</div>\n"

        "<div class=\"snac-top-controls-more\">\n"
        "<details><summary>%s</summary>\n"

        "<form method=\"post\" action=\"%s/admin/action\">\n"
        "<input type=\"text\" name=\"actor\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form><p>\n"

        "<form method=\"post\" action=\"%s/admin/action\">\n"
        "<input type=\"text\" name=\"id\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form><p>\n"

        "<details><summary>%s</summary>\n"

        "<div class=\"snac-user-setup\">\n"
        "<form method=\"post\" action=\"%s/admin/user-setup\">\n"
        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"name\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"avatar\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<textarea name=\"bio\" cols=\"40\" rows=\"4\">%s</textarea></p>\n"

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


d_char *html_button(d_char *s, char *clss, char *label)
{
    xs *s1 = xs_fmt(
               "<input type=\"submit\" name=\"action\" "
               "class=\"snac-btn-%s\" value=\"%s\">\n",
                clss, label);

    return xs_str_cat(s, s1);
}


d_char *build_mentions(snac *snac, char *msg)
/* returns a string with the mentions in msg */
{
    d_char *s = xs_str_new(NULL);
    char *list = xs_dict_get(msg, "tag");
    char *v;

    while (xs_list_iter(&list, &v)) {
        char *type = xs_dict_get(v, "type");
        char *href = xs_dict_get(v, "href");
        char *name = xs_dict_get(v, "name");

        if (type && strcmp(type, "Mention") == 0 &&
            href && strcmp(href, snac->actor) != 0 && name) {
            xs *l = xs_split(name, "@");

            /* is it a name without a host? */
            if (xs_list_len(l) < 3) {
                /* split the href and pick the host name LIKE AN ANIMAL */
                /* would be better to query the webfinger but *won't do that* here */
                xs *l2 = xs_split(href, "/");

                if (xs_list_len(l2) >= 3) {
                    xs *s1 = xs_fmt("%s@%s ", name, xs_list_get(l2, 2));
                    s = xs_str_cat(s, s1);
                }
            }
            else {
                s = xs_str_cat(s, name);
                s = xs_str_cat(s, " ");
            }
        }
    }

    return s;
}


d_char *html_entry_controls(snac *snac, d_char *os, char *msg)
{
    char *id    = xs_dict_get(msg, "id");
    char *actor = xs_dict_get(msg, "attributedTo");
    char *meta  = xs_dict_get(msg, "_snac");

    xs *s   = xs_str_new(NULL);
    xs *md5 = xs_md5_hex(id, strlen(id));

    s = xs_str_cat(s, "<div class=\"snac-controls\">\n");

    {
        xs *s1 = xs_fmt(
            "<form method=\"post\" action=\"%s/admin/action\">\n"
            "<input type=\"hidden\" name=\"id\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"actor\" value=\"%s\">\n"
            "<input type=\"button\" name=\"action\" "
            "value=\"%s\" onclick=\""
                "x = document.getElementById('%s_reply'); "
                "if (x.style.display == 'block') "
                "   x.style.display = 'none'; else "
                "   x.style.display = 'block';"
            "\">\n",

            snac->actor, id, actor,
            L("Reply"),
            md5
        );

        s = xs_str_cat(s, s1);
    }

    if (strcmp(actor, snac->actor) != 0) {
        /* controls for other actors than this one */
        char *l;

        l = xs_dict_get(meta, "liked_by");
        if (xs_list_in(l, snac->actor) == -1) {
            /* not already liked; add button */
            s = html_button(s, "like", L("Like"));
        }

        l = xs_dict_get(meta, "announced_by");
        if (xs_list_in(l, snac->actor) == -1) {
            /* not already boosted; add button */
            s = html_button(s, "boost", L("Boost"));
        }

        if (following_check(snac, actor)) {
            s = html_button(s, "unfollow", L("Unfollow"));
        }
        else {
            s = html_button(s, "follow", L("Follow"));
            s = html_button(s, "mute", L("MUTE"));
        }
    }

    s = html_button(s, "delete", L("Delete"));

    s = xs_str_cat(s, "</form>\n");

    {
        /* the post textarea */
        xs *ct = build_mentions(snac, msg);

        xs *s1 = xs_fmt(
            "<p><div class=\"snac-note\" style=\"display: none\" id=\"%s_reply\">\n"
            "<form method=\"post\" action=\"%s/admin/note\" "
            "enctype=\"multipart/form-data\" id=\"%s_reply_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"in_reply_to\" value=\"%s\">\n"
            "<p><input type=\"file\" name=\"attach\">\n"
            "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
            "</form><p></div>\n",

            md5,
            snac->actor, md5,
            ct,
            id,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_entry(snac *snac, d_char *os, char *msg, xs_set *seen, int local, int level)
{
    char *id    = xs_dict_get(msg, "id");
    char *type  = xs_dict_get(msg, "type");
    char *meta  = xs_dict_get(msg, "_snac");
    xs *actor_o = NULL;
    char *actor;

    /* do not show non-public messages in the public timeline */
    if (local && !is_msg_public(snac, msg))
        return os;

    /* return if already seen */
    if (xs_set_add(seen, id) == 0)
        return os;

    xs *s = xs_str_new(NULL);

    if (strcmp(type, "Follow") == 0) {
        s = xs_str_cat(s, "<div class=\"snac-post\">\n");

        xs *s1 = xs_fmt("<div class=\"snac-origin\">%s</div>\n", L("follows you"));
        s = xs_str_cat(s, s1);

        s = html_msg_icon(snac, s, msg);

        s = xs_str_cat(s, "</div>\n");

        return xs_str_cat(os, s);
    }

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return os;

    /* ignore muted morons immediately */
    if (is_muted(snac, actor))
        return os;

    if (strcmp(actor, snac->actor) == 0)
        actor_o = msg_actor(snac);
    else
    if (!valid_status(actor_get(snac, actor, &actor_o)))
        return os;

    /* if this is our post, add the score */
    if (xs_startswith(id, snac->actor)) {
        int likes  = xs_list_len(xs_dict_get(meta, "liked_by"));
        int boosts = xs_list_len(xs_dict_get(meta, "announced_by"));

        /* alternate emojis: %d &#128077; %d &#128257; */

        xs *s1 = xs_fmt(
            "<div class=\"snac-score\">%d &#9733; %d &#8634;</div>\n",
            likes, boosts);

        s = xs_str_cat(s, s1);
    }

    if (level == 0) {
        char *p;

        s = xs_str_cat(s, "<div class=\"snac-post\">\n");

        /* print the origin of the post, if any */
        if (!xs_is_null(p = xs_dict_get(meta, "referrer"))) {
            xs *actor_r = NULL;

            if (valid_status(actor_get(snac, p, &actor_r))) {
                char *name;

                if ((name = xs_dict_get(actor_r, "name")) == NULL)
                    name = xs_dict_get(actor_r, "preferredUsername");

                xs *s1 = xs_fmt(
                    "<div class=\"snac-origin\">"
                    "<a href=\"%s\">%s</a> %s</div>\n",
                    xs_dict_get(actor_r, "id"),
                    name,
                    L("boosted")
                );

                s = xs_str_cat(s, s1);
            }
        }
        else
        if (!xs_is_null((p = xs_dict_get(meta, "parent"))) && *p) {
            /* this may happen if any of the autor or the parent aren't here */
            xs *s1 = xs_fmt(
                "<div class=\"snac-origin\">%s "
                "<a href=\"%s\">»</a></div>\n",
                L("in reply to"), p
            );

            s = xs_str_cat(s, s1);
        }
        else
        if (!xs_is_null((p = xs_dict_get(meta, "announced_by"))) &&
            xs_list_in(p, snac->actor) != -1) {
            /* we boosted this */
            xs *s1 = xs_fmt(
                "<div class=\"snac-origin\">"
                "<a href=\"%s\">%s</a> %s</a></div>",
                snac->actor, xs_dict_get(snac->config, "name"), L("liked")
            );

            s = xs_str_cat(s, s1);
        }
        else
        if (!xs_is_null((p = xs_dict_get(meta, "liked_by"))) &&
            xs_list_in(p, snac->actor) != -1) {
            /* we liked this */
            xs *s1 = xs_fmt(
                "<div class=\"snac-origin\">"
                "<a href=\"%s\">%s</a> %s</a></div>",
                snac->actor, xs_dict_get(snac->config, "name"), L("liked")
            );

            s = xs_str_cat(s, s1);
        }
    }
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n");

    s = html_msg_icon(snac, s, msg);

    /* add the content */
    s = xs_str_cat(s, "<div class=\"e-content snac-content\">\n");

    {
        xs *c = xs_dup(xs_dict_get(msg, "content"));
        char *p, *v;

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop(c, 0, -4);

        c = xs_replace_i(c, "<br><br>", "<p>");

        if (!xs_startswith(c, "<p>")) {
            xs *s1 = c;
            c = xs_fmt("<p>%s</p>", s1);
        }

        /* replace the :shortnames: */
        if (!xs_is_null(p = xs_dict_get(msg, "tag"))) {
            /* iterate the tags */
            while (xs_list_iter(&p, &v)) {
                char *t = xs_dict_get(v, "type");

                if (t && strcmp(t, "Emoji") == 0) {
                    char *n = xs_dict_get(v, "name");
                    char *i = xs_dict_get(v, "icon");

                    if (n && i) {
                        char *u = xs_dict_get(i, "url");
                        xs *img = xs_fmt("<img src=\"%s\" style=\"height: 1em\"/>", u);

                        c = xs_replace_i(c, n, img);
                    }
                }
            }
        }


        s = xs_str_cat(s, c);
    }

    s = xs_str_cat(s, "\n");

    /* add the attachments */
    char *attach;

    if ((attach = xs_dict_get(msg, "attachment")) != NULL) {
        char *v;
        while (xs_list_iter(&attach, &v)) {
            char *t = xs_dict_get(v, "mediaType");

            if (xs_is_null(t))
                continue;

            if (xs_startswith(t, "image/")) {
                char *url  = xs_dict_get(v, "url");
                char *name = xs_dict_get(v, "name");

                if (url != NULL) {
                    xs *s1 = xs_fmt("<p><img src=\"%s\" alt=\"%s\"/></p>\n",
                        url, xs_is_null(name) ? "" : name);

                    s = xs_str_cat(s, s1);
                }
            }
            else
            if (xs_startswith(t, "video/")) {
                char *url  = xs_dict_get(v, "url");

                if (url != NULL) {
                    xs *s1 = xs_fmt("<p><object data=\"%s\"></object></p>\n", url);

                    s = xs_str_cat(s, s1);
                }
            }
        }
    }

    s = xs_str_cat(s, "</div>\n");

    /** controls **/

    if (!local)
        s = html_entry_controls(snac, s, msg);

    /** children **/

    char *children = xs_dict_get(meta, "children");

    if (xs_list_len(children)) {
        int left = xs_list_len(children);
        char *id;

        if (level < 4)
            s = xs_str_cat(s, "<div class=\"snac-children\">\n");
        else
            s = xs_str_cat(s, "<div>\n");

        if (left > 3)
            s = xs_str_cat(s, "<details><summary>...</summary>\n");

        while (xs_list_iter(&children, &id)) {
            xs *chd = timeline_find(snac, id);

            if (left == 3)
                s = xs_str_cat(s, "</details>\n");

            if (chd != NULL)
                s = html_entry(snac, s, chd, seen, local, level + 1);
            else
                snac_debug(snac, 2, xs_fmt("cannot read from timeline child %s", id));

            left--;
        }

        s = xs_str_cat(s, "</div>\n");
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_user_footer(snac *snac, d_char *s)
{
    xs *s1 = xs_fmt(
        "<div class=\"snac-footer\">\n"
        "<a href=\"%s\">%s</a> - "
        "powered by <abbr title=\"Social Networks Are Crap\">snac</abbr></div>\n",
        srv_baseurl,
        L("about this site")
    );

    return xs_str_cat(s, s1);
}


d_char *html_timeline(snac *snac, char *list, int local)
/* returns the HTML for the timeline */
{
    d_char *s = xs_str_new(NULL);
    xs_set *seen = xs_set_new(4096);
    char *v;
    double t = ftime();

    s = html_user_header(snac, s, local);

    if (!local)
        s = html_top_controls(snac, s);

    s = xs_str_cat(s, "<a name=\"snac-posts\"></a>\n");
    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&list, &v)) {
        xs *msg = timeline_get(snac, v);

        s = html_entry(snac, s, msg, seen, local, 0);
    }

    s = xs_str_cat(s, "</div>\n");

    if (local) {
        xs *s1 = xs_fmt(
            "<div class=\"snac-history\">\n"
            "<p class=\"snac-history-title\">%s</p><ul>\n",
            L("History")
        );

        s = xs_str_cat(s, s1);

        xs *list = history_list(snac);
        char *p, *v;

        p = list;
        while (xs_list_iter(&p, &v)) {
            xs *fn = xs_replace(v, ".html", "");
            xs *s1 = xs_fmt(
                        "<li><a href=\"%s/h/%s\">%s</li>\n",
                        snac->actor, v, fn);

            s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</ul></div>\n");
    }

    s = html_user_footer(snac, s);

    {
        xs *s1 = xs_fmt("<!-- %lf seconds -->\n", ftime() - t);
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
    int cache = 1;
    char *v;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    /* check if server config variable 'disable_cache' is set */
    if ((v = xs_dict_get(srv_config, "disable_cache")) && xs_type(v) == XSTYPE_TRUE)
        cache = 0;

    p_path = xs_list_get(l, 2);

    if (p_path == NULL) {
        /* public timeline */
        xs *h = xs_str_localtime(0, "%Y-%m.html");

        if (cache && history_mtime(&snac, h) > timeline_mtime(&snac)) {
            snac_debug(&snac, 1, xs_fmt("serving cached local timeline"));

            *body   = history_get(&snac, h);
            *b_size = strlen(*body);
            status  = 200;
        }
        else {
            xs *list = local_list(&snac, 0xfffffff);

            *body   = html_timeline(&snac, list, 1);
            *b_size = strlen(*body);
            status  = 200;

            history_add(&snac, h, *body, *b_size);
        }
    }
    else
    if (strcmp(p_path, "admin") == 0) {
        /* private timeline */

        if (!login(&snac, req))
            status = 401;
        else {
            if (cache && history_mtime(&snac, "timeline.html_") > timeline_mtime(&snac)) {
                snac_debug(&snac, 1, xs_fmt("serving cached timeline"));

                *body   = history_get(&snac, "timeline.html_");
                *b_size = strlen(*body);
                status  = 200;
            }
            else {
                snac_debug(&snac, 1, xs_fmt("building timeline"));

                xs *list = timeline_list(&snac, 0xfffffff);

                *body   = html_timeline(&snac, list, 0);
                *b_size = strlen(*body);
                status  = 200;

                history_add(&snac, "timeline.html_", *body, *b_size);
            }
        }
    }
    else
    if (xs_startswith(p_path, "p/")) {
        /* a timeline with just one entry */
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);
        xs *fn = _timeline_find_fn(&snac, id);

        if (fn != NULL) {
            xs *list = xs_list_new();
            list = xs_list_append(list, fn);

            *body   = html_timeline(&snac, list, 1);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "s/")) {
        /* a static file */
        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);
        int sz;

        if (valid_status(static_get(&snac, id, body, &sz))) {
            *b_size = sz;
            *ctype  = xs_mime_by_ext(id);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "h/")) {
        /* an entry from the history */
        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);

        if ((*body = history_get(&snac, id)) != NULL) {
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
        status = 404;

    user_free(&snac);

    if (valid_status(status) && *ctype == NULL) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(d_char *req, char *q_path, d_char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    int status = 0;
    snac snac;
    char *uid, *p_path;
    char *p_vars;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    /* all posts must be authenticated */
    if (!login(&snac, req))
        return 401;

    p_vars = xs_dict_get(req, "p_vars");

#if 0
    {
        xs *j1 = xs_json_dumps_pp(p_vars, 4);
        printf("%s\n", j1);
    }
#endif

    if (p_path && strcmp(p_path, "admin/note") == 0) {
        /* post note */
        char *content     = xs_dict_get(p_vars, "content");
        char *in_reply_to = xs_dict_get(p_vars, "in_reply_to");
        char *attach_url  = xs_dict_get(p_vars, "attach_url");
        char *attach_file = xs_dict_get(p_vars, "attach");
        xs *attach_list   = xs_list_new();

        /* is attach_url set? */
        if (!xs_is_null(attach_url) && *attach_url != '\0')
            attach_list = xs_list_append(attach_list, attach_url);

        /* is attach_file set? */
        if (!xs_is_null(attach_file) && xs_type(attach_file) == XSTYPE_LIST) {
            char *fn = xs_list_get(attach_file, 0);

            if (*fn != '\0') {
                char *ext = strrchr(fn, '.');
                xs *ntid  = tid(0);
                xs *id    = xs_fmt("%s%s", ntid, ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                int fo    = xs_number_get(xs_list_get(attach_file, 1));
                int fs    = xs_number_get(xs_list_get(attach_file, 2));

                /* store */
                static_put(&snac, id, payload + fo, fs);

                attach_list = xs_list_append(attach_list, url);
            }
        }

        if (content != NULL) {
            xs *msg   = NULL;
            xs *c_msg = NULL;

            msg = msg_note(&snac, content, NULL, in_reply_to, attach_list);

            c_msg = msg_create(&snac, msg);

            post(&snac, c_msg);

            timeline_add(&snac, xs_dict_get(msg, "id"), msg, in_reply_to, NULL);
        }

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/action") == 0) {
        /* action on an entry */
        char *id     = xs_dict_get(p_vars, "id");
        char *actor  = xs_dict_get(p_vars, "actor");
        char *action = xs_dict_get(p_vars, "action");

        if (action == NULL)
            return 404;

        snac_debug(&snac, 1, xs_fmt("web action '%s' received", action));

        status = 303;

        if (strcmp(action, L("Like")) == 0) {
            xs *msg = msg_admiration(&snac, id, "Like");
            post(&snac, msg);
            timeline_admire(&snac, id, snac.actor, 1);
        }
        else
        if (strcmp(action, L("Boost")) == 0) {
            xs *msg = msg_admiration(&snac, id, "Announce");
            post(&snac, msg);
            timeline_admire(&snac, id, snac.actor, 0);
        }
        else
        if (strcmp(action, L("MUTE")) == 0) {
            mute(&snac, actor);
        }
        else
        if (strcmp(action, L("Follow")) == 0) {
            xs *msg = msg_follow(&snac, actor);

            /* reload the actor from the message, in may be different */
            actor = xs_dict_get(msg, "object");

            following_add(&snac, actor, msg);

            enqueue_output(&snac, msg, actor, 0);
        }
        else
        if (strcmp(action, L("Unfollow")) == 0) {
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, actor, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, actor);

                enqueue_output(&snac, msg, actor, 0);

                snac_log(&snac, xs_fmt("unfollowed actor %s", actor));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Delete")) == 0) {
            /* delete an entry */
            if (xs_startswith(id, snac.actor)) {
                /* it's a post by us: generate a delete */
                xs *msg = msg_delete(&snac, id);

                post(&snac, msg);

                snac_log(&snac, xs_fmt("posted tombstone for %s", id));
            }

            timeline_del(&snac, id);

            snac_log(&snac, xs_fmt("deleted entry %s", id));
        }
        else
            status = 404;

        /* delete the cached timeline */
        if (status == 303)
            history_del(&snac, "timeline.html_");
    }
    else
    if (p_path && strcmp(p_path, "admin/user-setup") == 0) {
        /* change of user data */
        char *v;
        char *p1, *p2;

        if ((v = xs_dict_get(p_vars, "name")) != NULL)
            snac.config = xs_dict_set(snac.config, "name", v);
        if ((v = xs_dict_get(p_vars, "avatar")) != NULL)
            snac.config = xs_dict_set(snac.config, "avatar", v);
        if ((v = xs_dict_get(p_vars, "bio")) != NULL)
            snac.config = xs_dict_set(snac.config, "bio", v);

        /* password change? */
        if ((p1 = xs_dict_get(p_vars, "passwd1")) != NULL &&
            (p2 = xs_dict_get(p_vars, "passwd2")) != NULL &&
            *p1 && strcmp(p1, p2) == 0) {
            xs *pw = hash_password(snac.uid, p1, NULL);
            snac.config = xs_dict_set(snac.config, "passwd", pw);
        }

        xs *fn  = xs_fmt("%s/user.json", snac.basedir);
        xs *bfn = xs_fmt("%s.bak", fn);
        FILE *f;

        rename(fn, bfn);

        if ((f = fopen(fn, "w")) != NULL) {
            xs *j = xs_json_dumps_pp(snac.config, 4);
            fwrite(j, strlen(j), 1, f);
            fclose(f);
        }
        else
            rename(bfn, fn);

        history_del(&snac, "timeline.html_");

        xs *a_msg = msg_actor(&snac);
        xs *u_msg = msg_update(&snac, a_msg);

        post(&snac, u_msg);

        status = 303;
    }

    if (status == 303) {
        *body   = xs_fmt("%s/admin#snac-posts", snac.actor);
        *b_size = strlen(*body);
    }

    return status;
}

