/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

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
        xs *s1 = xs_crop_i(xs_dup(auth), 6, 0);
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


d_char *html_actor_icon(snac *snac, d_char *os, char *actor,
    const char *date, const char *udate, const char *url, int priv)
{
    xs *s = xs_str_new(NULL);

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
                    xs *img = xs_fmt("<img src=\"%s\" style=\"height: 1em\" loading=\"lazy\"/>", u);

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
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());

    {
        xs *s1 = xs_fmt("<p><img class=\"snac-avatar\" src=\"%s\" alt=\"\" "
                        "loading=\"lazy\"/>\n", avatar);
        s = xs_str_cat(s, s1);
    }

    {
        xs *s1 = xs_fmt("<a href=\"%s\" class=\"p-author h-card snac-author\">%s</a>",
            xs_dict_get(actor, "id"), name);
        s = xs_str_cat(s, s1);
    }

    if (!xs_is_null(url)) {
        xs *s1 = xs_fmt(" <a href=\"%s\">»</a>", url);
        s = xs_str_cat(s, s1);
    }

    if (priv)
        s = xs_str_cat(s, " <span title=\"private\">&#128274;</span>");

    if (xs_is_null(date)) {
        s = xs_str_cat(s, "<br>\n&nbsp;\n");
    }
    else {
        xs *date_label = xs_crop_i(xs_dup(date), 0, 10);
        xs *date_title = xs_dup(date);

        if (!xs_is_null(udate)) {
            xs *sd = xs_crop_i(xs_dup(udate), 0, 10);

            date_label = xs_str_cat(date_label, " / ");
            date_label = xs_str_cat(date_label, sd);

            date_title = xs_str_cat(date_title, " / ");
            date_title = xs_str_cat(date_title, udate);
        }

        xs *s1 = xs_fmt(
            "<br>\n<time class=\"dt-published snac-pubdate\" title=\"%s\">%s</time>\n",
                date_title, date_label);

        s = xs_str_cat(s, s1);
    }

    return xs_str_cat(os, s);
}


d_char *html_msg_icon(snac *snac, d_char *os, char *msg)
{
    char *actor_id;
    xs *actor = NULL;

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(snac, actor_id, &actor))) {
        char *date  = NULL;
        char *udate = NULL;
        char *url   = NULL;
        int priv    = 0;

        if (strcmp(xs_dict_get(msg, "type"), "Note") == 0)
            url = xs_dict_get(msg, "id");

        priv = !is_msg_public(snac, msg);

        date  = xs_dict_get(msg, "published");
        udate = xs_dict_get(msg, "updated");

        os = html_actor_icon(snac, os, actor, date, udate, url, priv);
    }

    return os;
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
        xs *s1 = xs_fmt("<title>%s (@%s@%s)</title>\n",
            xs_dict_get(snac->config, "name"),
            snac->uid,
            xs_dict_get(srv_config,   "host"));

        s = xs_str_cat(s, s1);
    }

    {
        xs *s1 = xs_fmt("<link rel=\"alternate\" type=\"application/rss+xml\" "
                        "title=\"RSS\" href=\"%s.rss\" />\n", snac->actor);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</head>\n<body>\n");

    /* top nav */
    s = xs_str_cat(s, "<nav class=\"snac-top-nav\">");

    {
        xs *s1;

        if (local)
            s1 = xs_fmt(
                "<a href=\"%s.rss\">%s</a> - "
                "<a href=\"%s/admin\" rel=\"nofollow\">%s</a></nav>\n",
                snac->actor, L("RSS"),
                snac->actor, L("private"));
        else
            s1 = xs_fmt(
                "<a href=\"%s\">%s</a> - "
                "<a href=\"%s/admin\">%s</a> - "
                "<a href=\"%s/people\">%s</a></nav>\n",
                snac->actor, L("public"),
                snac->actor, L("private"),
                snac->actor, L("people"));

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

        bio = not_really_markdown(xs_dict_get(snac->config, "bio"));

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
        "<p>%s: <input type=\"checkbox\" name=\"sensitive\">\n"
        "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"
        "<p><input type=\"file\" name=\"attach\">\n"
        "<p>%s: <input type=\"text\" name=\"alt_text\">\n"
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
        "<form method=\"post\" action=\"%s/admin/user-setup\" enctype=\"multipart/form-data\">\n"
        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"name\" value=\"%s\"></p>\n"

        "<p>%s: <input type=\"file\" name=\"avatar_file\"></p>\n"

        "<p>%s:<br>\n"
        "<textarea name=\"bio\" cols=\"40\" rows=\"4\">%s</textarea></p>\n"

        "<p><input type=\"checkbox\" name=\"cw\" id=\"cw\" %s>\n"
        "<label for=\"cw\">%s</label></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"email\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"telegram_bot\" placeholder=\"Bot API key\" value=\"%s\"> "
        "<input type=\"text\" name=\"telegram_chat_id\" placeholder=\"Chat id\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"number\" name=\"purge_days\" value=\"%s\"></p>\n"

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

    const char *email = "[disabled by admin]";

    if (xs_type(xs_dict_get(srv_config, "disable_email_notifications")) != XSTYPE_TRUE) {
        email = xs_dict_get(snac->config_o, "email");
        if (xs_is_null(email)) {
            email = xs_dict_get(snac->config, "email");

            if (xs_is_null(email))
                email = "";
        }
    }

    char *cw = xs_dict_get(snac->config, "cw");
    if (xs_is_null(cw))
        cw = "";

    char *telegram_bot = xs_dict_get(snac->config, "telegram_bot");
    if (xs_is_null(telegram_bot))
        telegram_bot = "";

    char *telegram_chat_id = xs_dict_get(snac->config, "telegram_chat_id");
    if (xs_is_null(telegram_chat_id))
        telegram_chat_id = "";

    const char *purge_days = xs_dict_get(snac->config, "purge_days");
    if (!xs_is_null(purge_days) && xs_type(purge_days) == XSTYPE_NUMBER)
        purge_days = xs_number_str(purge_days);
    else
        purge_days = "0";

    xs *s1 = xs_fmt(_tmpl,
        snac->actor,
        L("Sensitive content"),
        L("Only for mentioned people"),
        L("Image description"),
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
        L("Avatar"),
        L("Bio"),
        xs_dict_get(snac->config, "bio"),
        strcmp(cw, "open") == 0 ? "checked" : "",
        L("Always show sensitive content"),
        L("Email address for notifications"),
        email,
        L("Telegram notifications (bot key and chat id)"),
        telegram_bot,
        telegram_chat_id,
        L("Maximum days to keep posts (0: server settings)"),
        purge_days,
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
            xs *s1 = NULL;

            if (name[0] != '@') {
                s1 = xs_fmt("@%s", name);
                name = s1;
            }

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

    if (*s) {
        xs *s1 = s;
        s = xs_fmt("\n\n\nCC: %s", s1);
    }

    return s;
}


d_char *html_entry_controls(snac *snac, d_char *os, char *msg, const char *md5)
{
    char *id    = xs_dict_get(msg, "id");
    char *actor = xs_dict_get(msg, "attributedTo");
    xs *likes   = object_likes(id);
    xs *boosts  = object_announces(id);

    xs *s   = xs_str_new(NULL);

    s = xs_str_cat(s, "<div class=\"snac-controls\">\n");

    {
        xs *s1 = xs_fmt(
            "<form method=\"post\" action=\"%s/admin/action\">\n"
            "<input type=\"hidden\" name=\"id\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"actor\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"redir\" value=\"%s_entry\">\n"
            "\n",

            snac->actor, id, actor, md5
        );

        s = xs_str_cat(s, s1);
    }

    if (xs_list_in(likes, snac->md5) == -1) {
        /* not already liked; add button */
        s = html_button(s, "like", L("Like"));
    }

    if (is_msg_public(snac, msg)) {
        if (strcmp(actor, snac->actor) == 0 || xs_list_in(boosts, snac->md5) == -1) {
            /* not already boosted or us; add button */
            s = html_button(s, "boost", L("Boost"));
        }
    }

    if (strcmp(actor, snac->actor) != 0) {
        /* controls for other actors than this one */
        if (following_check(snac, actor)) {
            s = html_button(s, "unfollow", L("Unfollow"));
        }
        else {
            s = html_button(s, "follow", L("Follow"));
        }

        s = html_button(s, "mute", L("MUTE"));
    }

    s = html_button(s, "delete", L("Delete"));
    s = html_button(s, "hide",   L("Hide"));

    s = xs_str_cat(s, "</form>\n");

    char *prev_src = xs_dict_get(msg, "sourceContent");

    if (!xs_is_null(prev_src) && strcmp(actor, snac->actor) == 0) {
        /* post can be edited */
        xs *s1 = xs_fmt(
            "<p><details><summary>%s</summary>\n"
            "<p><div class=\"snac-note\" id=\"%s_edit\">\n"
            "<form method=\"post\" action=\"%s/admin/note\" "
            "enctype=\"multipart/form-data\" id=\"%s_edit_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"edit_id\" value=\"%s\">\n"

            "<p>%s: <input type=\"checkbox\" name=\"sensitive\">\n"
            "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"
            "<p><input type=\"file\" name=\"attach\">\n"
            "<p>%s: <input type=\"text\" name=\"alt_text\">\n"

            "<input type=\"hidden\" name=\"redir\" value=\"%s_entry\">\n"
            "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
            "</form><p></div>\n"
            "</details><p>"
            "\n",

            L("Edit..."),
            md5,
            snac->actor, md5,
            prev_src,
            id,
            L("Sensitive content"),
            L("Only for mentioned people"),
            L("Image description"),
            md5,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    {
        /* the post textarea */
        xs *ct = build_mentions(snac, msg);

        xs *s1 = xs_fmt(
            "<p><details><summary>%s</summary>\n"
            "<p><div class=\"snac-note\" id=\"%s_reply\">\n"
            "<form method=\"post\" action=\"%s/admin/note\" "
            "enctype=\"multipart/form-data\" id=\"%s_reply_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"in_reply_to\" value=\"%s\">\n"

            "<p>%s: <input type=\"checkbox\" name=\"sensitive\">\n"
            "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"
            "<p><input type=\"file\" name=\"attach\">\n"
            "<p>%s: <input type=\"text\" name=\"alt_text\">\n"

            "<input type=\"hidden\" name=\"redir\" value=\"%s_entry\">\n"
            "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
            "</form><p></div>\n"
            "</details><p>"
            "\n",

            L("Reply..."),
            md5,
            snac->actor, md5,
            ct,
            id,
            L("Sensitive content"),
            L("Only for mentioned people"),
            L("Image description"),
            md5,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_entry(snac *snac, d_char *os, char *msg, int local, int level, const char *md5)
{
    char *id    = xs_dict_get(msg, "id");
    char *type  = xs_dict_get(msg, "type");
    char *actor;
    int sensitive = 0;
    char *v;
    xs *boosts = NULL;

    /* do not show non-public messages in the public timeline */
    if (local && !is_msg_public(snac, msg))
        return os;

    xs *s = xs_str_new(NULL);

    /* top wrap */
    if (is_hidden(snac, id))
        s = xs_str_cat(s, "<div style=\"display: none\">\n");
    else
        s = xs_str_cat(s, "<div>\n");

    {
        xs *s1 = xs_fmt("<a name=\"%s_entry\"></a>\n", md5);

        s = xs_str_cat(s, s1);
    }

    if (strcmp(type, "Follow") == 0) {
        s = xs_str_cat(s, "<div class=\"snac-post\">\n");

        xs *s1 = xs_fmt("<div class=\"snac-origin\">%s</div>\n", L("follows you"));
        s = xs_str_cat(s, s1);

        s = html_msg_icon(snac, s, msg);

        s = xs_str_cat(s, "</div>\n");

        return xs_str_cat(os, s);
    }
    else
    if (strcmp(type, "Note") != 0) {
        /* skip oddities */
        return os;
    }

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return os;

    /* ignore muted morons immediately */
    if (is_muted(snac, actor))
        return os;

    if (strcmp(actor, snac->actor) != 0 && !valid_status(actor_get(snac, actor, NULL)))
        return os;

    /* if this is our post, add the score */
    if (xs_startswith(id, snac->actor)) {
        int n_likes  = object_likes_len(id);
        int n_boosts = object_announces_len(id);

        /* alternate emojis: %d &#128077; %d &#128257; */

        xs *s1 = xs_fmt(
            "<div class=\"snac-score\">%d &#9733; %d &#8634;</div>\n",
            n_likes, n_boosts);

        s = xs_str_cat(s, s1);
    }

    if (level == 0)
        s = xs_str_cat(s, "<div class=\"snac-post\">\n");
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n");

    if (boosts == NULL)
        boosts = object_announces(id);

    if (xs_list_len(boosts)) {
        /* if somebody boosted this, show as origin */
        char *p = xs_list_get(boosts, -1);
        xs *actor_r = NULL;

        if (xs_list_in(boosts, snac->md5) != -1) {
            /* we boosted this */
            xs *s1 = xs_fmt(
                "<div class=\"snac-origin\">"
                "<a href=\"%s\">%s</a> %s</a></div>",
                snac->actor, xs_dict_get(snac->config, "name"), L("boosted")
            );

            s = xs_str_cat(s, s1);
        }
        else
        if (valid_status(object_get_by_md5(p, &actor_r))) {
            char *name;

            if ((name = xs_dict_get(actor_r, "name")) == NULL)
                name = xs_dict_get(actor_r, "preferredUsername");

            if (!xs_is_null(name)) {
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
    }
    else
    if (strcmp(type, "Note") == 0) {
        if (level == 0) {
            /* is the parent not here? */
            char *parent = xs_dict_get(msg, "inReplyTo");

            if (!xs_is_null(parent) && *parent && !timeline_here(snac, parent)) {
                xs *s1 = xs_fmt(
                    "<div class=\"snac-origin\">%s "
                    "<a href=\"%s\">»</a></div>\n",
                    L("in reply to"), parent
                );

                s = xs_str_cat(s, s1);
            }
        }
    }

    s = html_msg_icon(snac, s, msg);

    /* add the content */
    s = xs_str_cat(s, "<div class=\"e-content snac-content\">\n");

    /* is it sensitive? */
    if (!xs_is_null(v = xs_dict_get(msg, "sensitive")) && xs_type(v) == XSTYPE_TRUE) {
        if (xs_is_null(v = xs_dict_get(msg, "summary")) || *v == '\0')
            v = "...";
        /* only show it when not in the public timeline and the config setting is "open" */
        char *cw = xs_dict_get(snac->config, "cw");
        if (xs_is_null(cw) || local)
            cw = "";
        xs *s1 = xs_fmt("<details %s><summary>%s [%s]</summary>\n", cw, v, L("SENSITIVE CONTENT"));
        s = xs_str_cat(s, s1);
        sensitive = 1;
    }

#if 0
    {
        xs *md5 = xs_md5_hex(id, strlen(id));
        xs *s1  = xs_fmt("<p><code>%s</code></p>\n", md5);
        s = xs_str_cat(s, s1);
    }
#endif

    {
        xs *c  = sanitize(xs_dict_get(msg, "content"));
        char *p, *v;

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop_i(c, 0, -4);

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
                        xs *img = xs_fmt("<img src=\"%s\" style=\"height: 1em\" "
                                         "loading=\"lazy\"/>", u);

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
                    xs *s1 = xs_fmt("<p><img src=\"%s\" alt=\"%s\" loading=\"lazy\"/></p>\n",
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

    if (sensitive)
        s = xs_str_cat(s, "</details><p>\n");

    s = xs_str_cat(s, "</div>\n");

    /** controls **/

    if (!local)
        s = html_entry_controls(snac, s, msg, md5);

    /** children **/
    xs *children = object_children(id);
    int left     = xs_list_len(children);

    if (left) {
        char *p, *cmd5;
        int older_open = 0;
        xs *ss = xs_str_new(NULL);
        int n_children = 0;

        ss = xs_str_cat(ss, "<details open><summary>...</summary><p>\n");

        if (level < 4)
            ss = xs_str_cat(ss, "<div class=\"snac-children\">\n");
        else
            ss = xs_str_cat(ss, "<div>\n");

        if (left > 3) {
            xs *s1 = xs_fmt("<details><summary>%s</summary>\n", L("Older..."));
            ss = xs_str_cat(ss, s1);
            older_open = 1;
        }

        p = children;
        while (xs_list_iter(&p, &cmd5)) {
            xs *chd = NULL;
            timeline_get_by_md5(snac, cmd5, &chd);

            if (older_open && left <= 3) {
                ss = xs_str_cat(ss, "</details>\n");
                older_open = 0;
            }

            if (chd != NULL) {
                ss = html_entry(snac, ss, chd, local, level + 1, cmd5);
                n_children++;
            }
            else
                snac_debug(snac, 2, xs_fmt("cannot read from timeline child %s", cmd5));

            left--;
        }

        if (older_open)
            ss = xs_str_cat(ss, "</details>\n");

        ss = xs_str_cat(ss, "</div>\n");
        ss = xs_str_cat(ss, "</details>\n");

        if (n_children)
            s = xs_str_cat(s, ss);
    }

    s = xs_str_cat(s, "</div>\n</div>\n");

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


d_char *html_timeline(snac *snac, char *list, int local, int skip, int show, int show_more)
/* returns the HTML for the timeline */
{
    d_char *s = xs_str_new(NULL);
    char *v;
    double t = ftime();

    s = html_user_header(snac, s, local);

    if (!local)
        s = html_top_controls(snac, s);

    s = xs_str_cat(s, "<a name=\"snac-posts\"></a>\n");
    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&list, &v)) {
        xs *msg = NULL;

        if (!valid_status(timeline_get_by_md5(snac, v, &msg)))
            continue;

        s = html_entry(snac, s, msg, local, 0, v);
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
                        "<li><a href=\"%s/h/%s\">%s</a></li>\n",
                        snac->actor, v, fn);

            s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</ul></div>\n");
    }

    {
        xs *s1 = xs_fmt("<!-- %lf seconds -->\n", ftime() - t);
        s = xs_str_cat(s, s1);
    }

    if (show_more) {
        xs *s1 = xs_fmt(
            "<p>"
            "<a href=\"%s%s?skip=%d&show=%d\" name=\"snac-more\">%s</a>"
            "</p>\n", snac->actor, local ? "" : "/admin", skip + show, show, L("Load more..."));

        s = xs_str_cat(s, s1);
    }

    s = html_user_footer(snac, s);

    s = xs_str_cat(s, "</body>\n</html>\n");

    return s;
}


d_char *html_people_list(snac *snac, d_char *os, d_char *list, const char *header, const char *t)
{
    xs *s = xs_str_new(NULL);
    xs *h = xs_fmt("<h2>%s</h2>\n", header);
    char *p, *actor_id;

    s = xs_str_cat(s, h);

    p = list;
    while (xs_list_iter(&p, &actor_id)) {
        xs *md5 = xs_md5_hex(actor_id, strlen(actor_id));
        xs *actor = NULL;

        if (valid_status(actor_get(snac, actor_id, &actor))) {
            s = xs_str_cat(s, "<div class=\"snac-post\">\n");

            s = html_actor_icon(snac, s, actor, xs_dict_get(actor, "published"), NULL, NULL, 0);


            /* content (user bio) */
            char *c = xs_dict_get(actor, "summary");

            if (!xs_is_null(c)) {
                s = xs_str_cat(s, "<div class=\"snac-content\">\n");

                xs *sc = sanitize(c);

                if (xs_startswith(sc, "<p>"))
                    s = xs_str_cat(s, sc);
                else {
                    xs *s1 = xs_fmt("<p>%s</p>", sc);
                    s = xs_str_cat(s, s1);
                }

                s = xs_str_cat(s, "</div>\n");
            }


            /* buttons */
            s = xs_str_cat(s, "<div class=\"snac-controls\">\n");

            xs *s1 = xs_fmt(
                "<p><form method=\"post\" action=\"%s/admin/action\">\n"
                "<input type=\"hidden\" name=\"actor\" value=\"%s\">\n"
                "<input type=\"hidden\" name=\"actor-form\" value=\"yes\">\n",

                snac->actor, actor_id
            );
            s = xs_str_cat(s, s1);

            if (following_check(snac, actor_id))
                s = html_button(s, "unfollow", L("Unfollow"));
            else {
                s = html_button(s, "follow", L("Follow"));

                if (follower_check(snac, actor_id))
                    s = html_button(s, "delete", L("Delete"));
            }

            if (is_muted(snac, actor_id))
                s = html_button(s, "unmute", L("Unmute"));
            else
                s = html_button(s, "mute", L("MUTE"));

            s = xs_str_cat(s, "</form>\n");

            /* the post textarea */
            xs *s2 = xs_fmt(
                "<p><details><summary>%s</summary>\n"
                "<p><div class=\"snac-note\" id=\"%s_%s_dm\">\n"
                "<form method=\"post\" action=\"%s/admin/note\" "
                "enctype=\"multipart/form-data\" id=\"%s_reply_form\">\n"
                "<textarea class=\"snac-textarea\" name=\"content\" "
                "rows=\"4\" wrap=\"virtual\" required=\"required\"></textarea>\n"
                "<input type=\"hidden\" name=\"to\" value=\"%s\">\n"
                "<p><input type=\"file\" name=\"attach\">\n"
                "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
                "</form><p></div>\n"
                "</details><p>\n",

                L("Direct Message..."),
                md5, t,
                snac->actor, md5,
                actor_id,
                L("Post")
            );
            s = xs_str_cat(s, s2);

            s = xs_str_cat(s, "</div>\n");

            s = xs_str_cat(s, "</div>\n");
        }
    }

    return xs_str_cat(os, s);
}


d_char *html_people(snac *snac)
{
    d_char *s = xs_str_new(NULL);
    xs *wing = following_list(snac);
    xs *wers = follower_list(snac);

    s = html_user_header(snac, s, 0);

    s = html_people_list(snac, s, wing, L("People you follow"), "i");

    s = html_people_list(snac, s, wers, L("People that follows you"), "e");

    s = html_user_footer(snac, s);

    s = xs_str_cat(s, "</body>\n</html>\n");

    return s;
}


int html_get_handler(d_char *req, char *q_path, char **body, int *b_size, char **ctype)
{
    char *accept = xs_dict_get(req, "accept");
    int status = 404;
    snac snac;
    xs *uid = NULL;
    char *p_path;
    int cache = 1;
    int save = 1;
    char *v;

    xs *l = xs_split_n(q_path, "/", 2);
    v = xs_list_get(l, 1);

    if (xs_is_null(v)) {
        srv_log(xs_fmt("html_get_handler bad query '%s'", q_path));
        return 404;
    }

    uid = xs_dup(v);

    /* rss extension? */
    if (xs_endswith(uid, ".rss")) {
        uid = xs_crop_i(uid, 0, -4);
        p_path = ".rss";
    }
    else
        p_path = xs_list_get(l, 2);

    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    /* return the RSS if requested by Accept header */
    if (accept != NULL) {
        if (xs_str_in(accept, "text/xml") != -1 ||
            xs_str_in(accept, "application/rss+xml") != -1)
            p_path = ".rss";
    }

    /* check if server config variable 'disable_cache' is set */
    if ((v = xs_dict_get(srv_config, "disable_cache")) && xs_type(v) == XSTYPE_TRUE)
        cache = 0;

    int skip = 0;
    int show = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));
    char *q_vars = xs_dict_get(req, "q_vars");
    if ((v = xs_dict_get(q_vars, "skip")) != NULL)
        skip = atoi(v), cache = 0, save = 0;
    if ((v = xs_dict_get(q_vars, "show")) != NULL)
        show = atoi(v), cache = 0, save = 0;

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
            xs *list = timeline_list(&snac, "public", skip, show);
            xs *next = timeline_list(&snac, "public", skip + show, 1);

            *body = html_timeline(&snac, list, 1, skip, show, xs_list_len(next));

            *b_size = strlen(*body);
            status  = 200;

            if (save)
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

                xs *list = timeline_list(&snac, "private", skip, show);
                xs *next = timeline_list(&snac, "private", skip + show, 1);

                *body   = html_timeline(&snac, list, 0, skip, show, xs_list_len(next));

                *b_size = strlen(*body);
                status  = 200;

                if (save)
                    history_add(&snac, "timeline.html_", *body, *b_size);
            }
        }
    }
    else
    if (strcmp(p_path, "people") == 0) {
        /* the list of people */

        if (!login(&snac, req))
            status = 401;
        else {
            *body   = html_people(&snac);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/")) {
        /* a timeline with just one entry */
        xs *id  = xs_fmt("%s/%s", snac.actor, p_path);
        xs *msg = NULL;

        if (valid_status(object_get(id, &msg))) {
            xs *md5  = xs_md5_hex(id, strlen(id));
            xs *list = xs_list_new();

            list = xs_list_append(list, md5);

            *body   = html_timeline(&snac, list, 1, 0, 0, 0);
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
    if (strcmp(p_path, ".rss") == 0) {
        /* public timeline in RSS format */
        d_char *rss;
        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *bio   = not_really_markdown(xs_dict_get(snac.config, "bio"));
        char *p, *v;

        /* escape tags */
        bio = xs_replace_i(bio, "<", "&lt;");
        bio = xs_replace_i(bio, ">", "&gt;");

        rss = xs_fmt(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rss version=\"0.91\">\n"
            "<channel>\n"
            "<title>%s (@%s@%s)</title>\n"
            "<language>en</language>\n"
            "<link>%s.rss</link>\n"
            "<description>%s</description>\n",
            xs_dict_get(snac.config, "name"),
            snac.uid,
            xs_dict_get(srv_config, "host"),
            snac.actor,
            bio
        );

        p = elems;
        while (xs_list_iter(&p, &v)) {
            xs *msg  = NULL;

            if (!valid_status(timeline_get_by_md5(&snac, v, &msg)))
                continue;

            char *id = xs_dict_get(msg, "id");

            if (!xs_startswith(id, snac.actor))
                continue;

            xs *content = sanitize(xs_dict_get(msg, "content"));
            xs *title   = xs_str_new(NULL);
            int i;

            /* escape tags */
            content = xs_replace_i(content, "<", "&lt;");
            content = xs_replace_i(content, ">", "&gt;");

            for (i = 0; content[i] && content[i] != '<' && content[i] != '&' && i < 40; i++)
                title = xs_append_m(title, &content[i], 1);

            xs *s = xs_fmt(
                "<item>\n"
                "<title>%s...</title>\n"
                "<link>%s</link>\n"
                "<description>%s</description>\n"
                "</item>\n",
                title, id, content
            );

            rss = xs_str_cat(rss, s);
        }

        rss = xs_str_cat(rss, "</channel>\n</rss>\n");

        *body   = rss;
        *b_size = strlen(rss);
        *ctype  = "application/rss+xml; charset=utf-8";
        status  = 200;

        snac_debug(&snac, 1, xs_fmt("serving RSS"));
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
    xs_dict *p_vars;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_post_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    /* all posts must be authenticated */
    if (!login(&snac, req)) {
        user_free(&snac);
        return 401;
    }

    p_vars = xs_dict_get(req, "p_vars");

#if 0
    {
        xs *j1 = xs_json_dumps_pp(p_vars, 4);
        printf("%s\n", j1);
    }
#endif

    if (p_path && strcmp(p_path, "admin/note") == 0) {
        /* post note */
        xs_str *content      = xs_dict_get(p_vars, "content");
        xs_str *in_reply_to  = xs_dict_get(p_vars, "in_reply_to");
        xs_str *attach_url   = xs_dict_get(p_vars, "attach_url");
        xs_list *attach_file = xs_dict_get(p_vars, "attach");
        xs_str *to           = xs_dict_get(p_vars, "to");
        xs_str *sensitive    = xs_dict_get(p_vars, "sensitive");
        xs_str *edit_id      = xs_dict_get(p_vars, "edit_id");
        xs_str *alt_text     = xs_dict_get(p_vars, "alt_text");
        int priv             = !xs_is_null(xs_dict_get(p_vars, "mentioned_only"));
        xs *attach_list      = xs_list_new();

        /* default alt text */
        if (xs_is_null(alt_text))
            alt_text = "";

        /* is attach_url set? */
        if (!xs_is_null(attach_url) && *attach_url != '\0') {
            xs *l = xs_list_new();

            l = xs_list_append(l, attach_url);
            l = xs_list_append(l, alt_text);

            attach_list = xs_list_append(attach_list, l);
        }

        /* is attach_file set? */
        if (!xs_is_null(attach_file) && xs_type(attach_file) == XSTYPE_LIST) {
            char *fn = xs_list_get(attach_file, 0);

            if (*fn != '\0') {
                char *ext = strrchr(fn, '.');
                xs *hash  = xs_md5_hex(fn, strlen(fn));
                xs *id    = xs_fmt("%s%s", hash, ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                int fo    = xs_number_get(xs_list_get(attach_file, 1));
                int fs    = xs_number_get(xs_list_get(attach_file, 2));

                /* store */
                static_put(&snac, id, payload + fo, fs);

                xs *l = xs_list_new();

                l = xs_list_append(l, url);
                l = xs_list_append(l, alt_text);

                attach_list = xs_list_append(attach_list, l);
            }
        }

        if (content != NULL) {
            xs *msg       = NULL;
            xs *c_msg     = NULL;
            xs *content_2 = xs_replace(content, "\r", "");

            msg = msg_note(&snac, content_2, to, in_reply_to, attach_list, priv);

            if (sensitive != NULL) {
                xs *t = xs_val_new(XSTYPE_TRUE);

                msg = xs_dict_set(msg, "sensitive", t);
                msg = xs_dict_set(msg, "summary",   "...");
            }

            if (xs_is_null(edit_id)) {
                /* new message */
                c_msg = msg_create(&snac, msg);
                timeline_add(&snac, xs_dict_get(msg, "id"), msg);
            }
            else {
                /* an edition of a previous message */
                xs *p_msg = NULL;

                if (valid_status(object_get(edit_id, &p_msg))) {
                    /* copy relevant fields from previous version */
                    char *fields[] = { "id", "context", "url", "published",
                                       "to", "inReplyTo", NULL };
                    int n;

                    for (n = 0; fields[n]; n++) {
                        char *v = xs_dict_get(p_msg, fields[n]);
                        msg = xs_dict_set(msg, fields[n], v);
                    }

                    /* set the updated field */
                    xs *updated = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");
                    msg = xs_dict_set(msg, "updated", updated);

                    /* overwrite object, not updating the indexes */
                    object_add_ow(edit_id, msg);

                    /* update message */
                    c_msg = msg_update(&snac, msg);
                }
                else
                    snac_log(&snac, xs_fmt("cannot get object '%s' for editing", edit_id));
            }

            if (c_msg != NULL)
                enqueue_message(&snac, c_msg);

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

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 1);
            }
        }
        else
        if (strcmp(action, L("Boost")) == 0) {
            xs *msg = msg_admiration(&snac, id, "Announce");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 0);
            }
        }
        else
        if (strcmp(action, L("MUTE")) == 0) {
            mute(&snac, actor);
        }
        else
        if (strcmp(action, L("Unmute")) == 0) {
            unmute(&snac, actor);
        }
        else
        if (strcmp(action, L("Hide")) == 0) {
            hide(&snac, id);
        }
        else
        if (strcmp(action, L("Follow")) == 0) {
            xs *msg = msg_follow(&snac, actor);

            if (msg != NULL) {
                /* reload the actor from the message, in may be different */
                actor = xs_dict_get(msg, "object");

                following_add(&snac, actor, msg);

                enqueue_output_by_actor(&snac, msg, actor, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow")) == 0) {
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, actor, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, actor);

                enqueue_output_by_actor(&snac, msg, actor, 0);

                snac_log(&snac, xs_fmt("unfollowed actor %s", actor));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Delete")) == 0) {
            char *actor_form = xs_dict_get(p_vars, "actor-form");
            if (actor_form != NULL) {
                /* delete follower */
                if (valid_status(follower_del(&snac, actor)))
                    snac_log(&snac, xs_fmt("deleted follower %s", actor));
                else
                    snac_log(&snac, xs_fmt("error deleting follower %s", actor));
            }
            else {
                /* delete an entry */
                if (xs_startswith(id, snac.actor)) {
                    /* it's a post by us: generate a delete */
                    xs *msg = msg_delete(&snac, id);

                    enqueue_message(&snac, msg);

                    snac_log(&snac, xs_fmt("posted tombstone for %s", id));
                }

                timeline_del(&snac, id);

                snac_log(&snac, xs_fmt("deleted entry %s", id));
            }
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
        if ((v = xs_dict_get(p_vars, "cw")) != NULL &&
            strcmp(v, "on") == 0) {
            snac.config = xs_dict_set(snac.config, "cw", "open");
        } else { /* if the checkbox is not set, the parameter is missing */
            snac.config = xs_dict_set(snac.config, "cw", "");
        }
        if ((v = xs_dict_get(p_vars, "email")) != NULL)
            snac.config = xs_dict_set(snac.config, "email", v);
        if ((v = xs_dict_get(p_vars, "telegram_bot")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_bot", v);
        if ((v = xs_dict_get(p_vars, "telegram_chat_id")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_chat_id", v);
        if ((v = xs_dict_get(p_vars, "purge_days")) != NULL) {
            xs *days    = xs_number_new(atof(v));
            snac.config = xs_dict_set(snac.config, "purge_days", days);
        }

        /* avatar upload */
        xs_list *avatar_file = xs_dict_get(p_vars, "avatar_file");
        if (!xs_is_null(avatar_file) && xs_type(avatar_file) == XSTYPE_LIST) {
            char *fn = xs_list_get(avatar_file, 0);

            if (*fn != '\0') {
                char *ext = strrchr(fn, '.');
                xs *id    = xs_fmt("avatar%s", ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                int fo    = xs_number_get(xs_list_get(avatar_file, 1));
                int fs    = xs_number_get(xs_list_get(avatar_file, 2));

                /* store */
                static_put(&snac, id, payload + fo, fs);

                snac.config = xs_dict_set(snac.config, "avatar", url);
            }
        }

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

        enqueue_message(&snac, u_msg);

        status = 303;
    }

    if (status == 303) {
        char *redir = xs_dict_get(p_vars, "redir");

        if (xs_is_null(redir))
            redir = "snac-posts";

        *body   = xs_fmt("%s/admin#%s", snac.actor, redir);
        *b_size = strlen(*body);
    }

    user_free(&snac);

    return status;
}
