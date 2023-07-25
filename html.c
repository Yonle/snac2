/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_openssl.h"
#include "xs_time.h"
#include "xs_mime.h"

#include "snac.h"

int login(snac *snac, const xs_dict *headers)
/* tries a login */
{
    int logged_in = 0;
    const char *auth = xs_dict_get(headers, "authorization");

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

    if (logged_in)
        lastlog_write(snac, "web");

    return logged_in;
}


xs_str *actor_name(xs_dict *actor)
/* gets the actor name */
{
    xs_list *p;
    char *v;
    xs_str *name;

    if (xs_is_null((v = xs_dict_get(actor, "name"))) || *v == '\0') {
        if (xs_is_null(v = xs_dict_get(actor, "preferredUsername")) || *v == '\0') {
            v = "anonymous";
        }
    }

    name = encode_html(v);

    /* replace the :shortnames: */
    if (!xs_is_null(p = xs_dict_get(actor, "tag"))) {
        xs *tag = NULL;
        if (xs_type(p) == XSTYPE_DICT) {
            /* not a list */
            tag = xs_list_new();
            tag = xs_list_append(tag, p);
        } else {
            /* is a list */
            tag = xs_dup(p);
        }

        xs_list *tags = tag;

        /* iterate the tags */
        while (xs_list_iter(&tags, &v)) {
            char *t = xs_dict_get(v, "type");

            if (t && strcmp(t, "Emoji") == 0) {
                char *n = xs_dict_get(v, "name");
                char *i = xs_dict_get(v, "icon");

                if (n && i) {
                    char *u = xs_dict_get(i, "url");
                    xs *img = xs_fmt("<img src=\"%s\" style=\"height: 1em; vertical-align: middle;\" loading=\"lazy\"/>", u);

                    name = xs_replace_i(name, n, img);
                }
            }
        }
    }

    return name;
}


xs_str *html_actor_icon(xs_str *os, char *actor,
    const char *date, const char *udate, const char *url, int priv)
{
    xs *s = xs_str_new(NULL);

    xs *avatar = NULL;
    char *v;

    xs *name = actor_name(actor);

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

    if (strcmp(xs_dict_get(actor, "type"), "Service") == 0)
        s = xs_str_cat(s, " <span title=\"bot\">&#129302;</span>");

    if (xs_is_null(date)) {
        s = xs_str_cat(s, "\n&nbsp;\n");
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

        xs *edt = encode_html(date_title);
        xs *edl = encode_html(date_label);
        xs *s1 = xs_fmt(
            "\n<time class=\"dt-published snac-pubdate\" title=\"%s\">%s</time>\n",
                edt, edl);

        s = xs_str_cat(s, s1);
    }

    {
        char *username, *id;
        xs *s1;

        if (xs_is_null(username = xs_dict_get(actor, "preferredUsername")) || *username == '\0') {
            /* This should never be reached */
            username = "anonymous";
        }

        if (xs_is_null(id = xs_dict_get(actor, "id")) || *id == '\0') {
            /* This should never be reached */
            id = "https://social.example.org/anonymous";
        }

        /* "LIKE AN ANIMAL" */
        xs *domain = xs_split(id, "/");
        xs *user   = xs_fmt("@%s@%s", username, xs_list_get(domain, 2));

        xs *u1 = encode_html(user);
        s1 = xs_fmt(
            "<br><a href=\"%s\" class=\"p-author-tag h-card snac-author-tag\">%s</a>",
                xs_dict_get(actor, "id"), u1);

        s = xs_str_cat(s, s1);
    }

    return xs_str_cat(os, s);
}


xs_str *html_msg_icon(snac *snac, xs_str *os, const xs_dict *msg)
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
        const char *type = xs_dict_get(msg, "type");

        if (strcmp(type, "Note") == 0 || strcmp(type, "Question") == 0 || strcmp(type, "Page") == 0)
            url = xs_dict_get(msg, "id");

        priv = !is_msg_public(snac, msg);

        date  = xs_dict_get(msg, "published");
        udate = xs_dict_get(msg, "updated");

        os = html_actor_icon(os, actor, date, udate, url, priv);
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

        /* try to open the user css */
        if (!valid_status(static_get(snac, "style.css", &css, &size, NULL, NULL))) {
            /* it's not there; try to open the server-wide css */
            FILE *f;
            xs *g_css_fn = xs_fmt("%s/style.css", srv_basedir);

            if ((f = fopen(g_css_fn, "r")) != NULL) {
                css = xs_readall(f);
                fclose(f);
            }
        }

        if (css != NULL) {
            xs *s1 = xs_fmt("<style>%s</style>\n", css);
            s = xs_str_cat(s, s1);
        }
    }

    {
        xs *es1 = encode_html(xs_dict_get(snac->config, "name"));
        xs *es2 = encode_html(snac->uid);
        xs *es3 = encode_html(xs_dict_get(srv_config,   "host"));
        xs *s1 = xs_fmt("<title>%s (@%s@%s)</title>\n", es1, es2, es3);

        s = xs_str_cat(s, s1);
    }

    xs *avatar = xs_dup(xs_dict_get(snac->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());
    }

    {
        xs *s_bio = xs_dup(xs_dict_get(snac->config, "bio"));
        int n;

        /* shorten the bio */
        for (n = 0; s_bio[n] && s_bio[n] != '&' && s_bio[n] != '.' &&
                    s_bio[n] != '\r' && s_bio[n] != '\n' && n < 128; n++);
        s_bio[n] = '\0';

        xs *s_avatar = xs_dup(avatar);

        /* don't inline an empty avatar: create a real link */
        if (xs_startswith(s_avatar, "data:")) {
            xs_free(s_avatar);
            s_avatar = xs_fmt("%s/susie.png", srv_baseurl);
        }

        /* og properties */
        xs *es1 = encode_html(xs_dict_get(srv_config, "host"));
        xs *es2 = encode_html(xs_dict_get(snac->config, "name"));
        xs *es3 = encode_html(snac->uid);
        xs *es4 = encode_html(xs_dict_get(srv_config, "host"));
        xs *es5 = encode_html(s_bio);
        xs *es6 = encode_html(s_avatar);

        xs *s1 = xs_fmt(
            "<meta property=\"og:site_name\" content=\"%s\"/>\n"
            "<meta property=\"og:title\" content=\"%s (@%s@%s)\"/>\n"
            "<meta property=\"og:description\" content=\"%s\"/>\n"
            "<meta property=\"og:image\" content=\"%s\"/>\n"
            "<meta property=\"og:image:width\" content=\"300\"/>\n"
            "<meta property=\"og:image:height\" content=\"300\"/>\n",
            es1, es2, es3, es4, es5, es6);
        s = xs_str_cat(s, s1);
    }

    {
        xs *s1 = xs_fmt("<link rel=\"alternate\" type=\"application/rss+xml\" "
                        "title=\"RSS\" href=\"%s.rss\" />\n", snac->actor); /* snac->actor is likely need to be URLEncoded. */
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</head>\n<body>\n");

    /* top nav */
    s = xs_str_cat(s, "<nav class=\"snac-top-nav\">");

    {
        xs *s1;

        s1 = xs_fmt("<img src=\"%s\" class=\"snac-avatar\" alt=\"\"/>&nbsp;", avatar);

        s = xs_str_cat(s, s1);
    }

    {
        xs *s1;

        if (local)
            s1 = xs_fmt(
                "<a href=\"%s.rss\">%s</a> - "
                "<a href=\"%s/admin\" rel=\"nofollow\">%s</a></nav>\n",
                snac->actor, L("RSS"),
                snac->actor, L("private"));
        else {
            xs *n_list = notify_list(snac, 1);
            int n_len  = xs_list_len(n_list);
            xs *n_str  = NULL;

            /* show the number of new notifications, if there are any */
            if (n_len)
                n_str = xs_fmt("<sup style=\"background-color: red; "
                               "color: white;\"> %d </sup> ", n_len);
            else
                n_str = xs_str_new("");

            s1 = xs_fmt(
                "<a href=\"%s\">%s</a> - "
                "<a href=\"%s/admin\">%s</a> - "
                "<a href=\"%s/notifications\">%s</a>%s - "
                "<a href=\"%s/people\">%s</a></nav>\n",
                snac->actor, L("public"),
                snac->actor, L("private"),
                snac->actor, L("notifications"), n_str,
                snac->actor, L("people"));
        }

        s = xs_str_cat(s, s1);
    }

    /* user info */
    {
        char *_tmpl =
            "<div class=\"h-card snac-top-user\">\n"
            "<p class=\"p-name snac-top-user-name\">%s</p>\n"
            "<p class=\"snac-top-user-id\">@%s@%s</p>\n";

        xs *es1 = encode_html(xs_dict_get(snac->config, "name"));
        xs *es2 = encode_html(xs_dict_get(snac->config, "uid"));
        xs *es3 = encode_html(xs_dict_get(srv_config, "host"));

        xs *s1 = xs_fmt(_tmpl, es1, es2, es3);

        s = xs_str_cat(s, s1);

        if (local) {
            xs *es1  = encode_html(xs_dict_get(snac->config, "bio"));
            xs *bio1 = not_really_markdown(es1, NULL);
            xs *tags = xs_list_new();
            xs *bio2 = process_tags(snac, bio1, &tags);
            xs *s1   = xs_fmt("<div class=\"p-note snac-top-user-bio\">%s</div>\n", bio2);

            s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    return s;
}


d_char *html_top_controls(snac *snac, d_char *s)
/* generates the top controls */
{
    char *_tmpl =
        "<div class=\"snac-top-controls\">\n"

        "<div class=\"snac-note\">\n"
        "<details><summary>%s</summary>\n"
        "<form autocomplete=\"off\" method=\"post\" "
        "action=\"%s/admin/note\" enctype=\"multipart/form-data\">\n"
        "<textarea class=\"snac-textarea\" name=\"content\" "
        "rows=\"8\" wrap=\"virtual\" required=\"required\" placeholder=\"What's on your mind?\"></textarea>\n"
        "<input type=\"hidden\" name=\"in_reply_to\" value=\"\">\n"
        "<p>%s: <input type=\"checkbox\" name=\"sensitive\"> "
        "<input type=\"text\" name=\"summary\" placeholder=\"%s\">\n"
        "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"

        "<details><summary>%s</summary>\n" /** attach **/
        "<p>%s: <input type=\"file\" name=\"attach\">\n"
        "<p>%s: <input type=\"text\" name=\"alt_text\" placeholder=\"[Optional]\">\n"
        "</details>\n"

        "<p>"
        "<details><summary>%s</summary>\n" /** poll **/
        "<p>%s:<br>\n"
        "<textarea class=\"snac-textarea\" name=\"poll_options\" "
        "rows=\"6\" wrap=\"virtual\" placeholder=\"Option 1...\nOption 2...\nOption 3...\n....\"></textarea>\n"
        "<p><select name=\"poll_multiple\">\n"
        "<option value=\"off\">%s</option>\n"
        "<option value=\"on\">%s</option>\n"
        "</select>\n"
        "<select name=\"poll_end_secs\" id=\"poll_end_secs\">\n"
        "<option value=\"300\">%s</option>\n"
        "<option value=\"3600\">%s</option>\n"
        "<option value=\"86400\">%s</option>\n"
        "</select>\n"
        "</details>\n"

        "<p><input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form><p>\n"
        "</div>\n"
        "</details>\n"

        "<div class=\"snac-top-controls-more\">\n"
        "<details><summary>%s</summary>\n"

        "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/action\">\n" /** follow **/
        "<input type=\"text\" name=\"actor\" required=\"required\" placeholder=\"bob@example.com\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form><p>\n"

        "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/action\">\n" /** boost **/
        "<input type=\"text\" name=\"id\" required=\"required\" placeholder=\"https://fedi.example.com/bob/....\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form><p>\n"
        "</details>\n"

        "<details><summary>%s</summary>\n"

        "<div class=\"snac-user-setup\">\n" /** user setup **/
        "<form autocomplete=\"off\" method=\"post\" "
        "action=\"%s/admin/user-setup\" enctype=\"multipart/form-data\">\n"
        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"name\" value=\"%s\" placeholder=\"Your name.\"></p>\n"

        "<p>%s: <input type=\"file\" name=\"avatar_file\"></p>\n"

        "<p>%s:<br>\n"
        "<textarea name=\"bio\" cols=\"40\" rows=\"4\" placeholder=\"Write about yourself here....\">%s</textarea></p>\n"

        "<p><input type=\"checkbox\" name=\"cw\" id=\"cw\" %s>\n"
        "<label for=\"cw\">%s</label></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"email\" value=\"%s\" placeholder=\"bob@example.com\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"telegram_bot\" placeholder=\"Bot API key\" value=\"%s\"> "
        "<input type=\"text\" name=\"telegram_chat_id\" placeholder=\"Chat id\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"number\" name=\"purge_days\" value=\"%s\"></p>\n"

        "<p><input type=\"checkbox\" name=\"drop_dm_from_unknown\" id=\"drop_dm_from_unknown\" %s>\n"
        "<label for=\"drop_dm_from_unknown\">%s</label></p>\n"

        "<p><input type=\"checkbox\" name=\"bot\" id=\"bot\" %s>\n"
        "<label for=\"bot\">%s</label></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd1\" value=\"\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd2\" value=\"\"></p>\n"

        "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form>\n"

        "</div>\n"
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

    const char *d_dm_f_u = xs_dict_get(snac->config, "drop_dm_from_unknown");

    const char *bot = xs_dict_get(snac->config, "bot");

    xs *es1 = encode_html(xs_dict_get(snac->config, "name"));
    xs *es2 = encode_html(xs_dict_get(snac->config, "bio"));
    xs *es3 = encode_html(email);
    xs *es4 = encode_html(telegram_bot);
    xs *es5 = encode_html(telegram_chat_id);
    xs *es6 = encode_html(purge_days);

    xs *s1 = xs_fmt(_tmpl,
        L("New Post..."),
        snac->actor,
        L("Sensitive content"),
        L("Sensitive content description"),
        L("Only for mentioned people"),

        L("Attachment..."),
        L("File"),
        L("File description"),

        L("Poll..."),
        L("Poll options (one per line, up to 8)"),
        L("One choice"),
        L("Multiple choices"),
        L("End in 5 minutes"),
        L("End in 1 hour"),
        L("End in 1 day"),

        L("Post"),

        L("Operations..."),

        snac->actor,
        L("Follow"), L("(by URL or user@host)"),

        snac->actor,
        L("Boost"), L("(by URL)"),

        L("User Settings..."),
        snac->actor,
        L("Display name"),
        es1,
        L("Avatar"),
        L("Bio"),
        es2,
        strcmp(cw, "open") == 0 ? "checked" : "",
        L("Always show sensitive content"),
        L("Email address for notifications"),
        es3,
        L("Telegram notifications (bot key and chat id)"),
        es4,
        es5,
        L("Maximum days to keep posts (0: server settings)"),
        es6,
        xs_type(d_dm_f_u) == XSTYPE_TRUE ? "checked" : "",
        L("Drop direct messages from people you don't follow"),
        xs_type(bot) == XSTYPE_TRUE ? "checked" : "",
        L("This account is a bot"),
        L("New password"),
        L("Repeat new password"),
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


xs_str *build_mentions(snac *snac, const xs_dict *msg)
/* returns a string with the mentions in msg */
{
    xs_str *s = xs_str_new(NULL);
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


xs_str *html_entry_controls(snac *snac, xs_str *os, const xs_dict *msg, const char *md5)
{
    char *id    = xs_dict_get(msg, "id");
    char *actor = xs_dict_get(msg, "attributedTo");
    xs *likes   = object_likes(id);
    xs *boosts  = object_announces(id);

    xs *s   = xs_str_new(NULL);

    s = xs_str_cat(s, "<div class=\"snac-controls\">\n");

    {
        xs *s1 = xs_fmt(
            "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/action\">\n"
            "<input type=\"hidden\" name=\"id\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"actor\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"redir\" value=\"%s_entry\">\n"
            "\n",

            snac->actor, id, actor, md5
        );

        s = xs_str_cat(s, s1);
    }

    if (!xs_startswith(id, snac->actor)) {
        if (xs_list_in(likes, snac->md5) == -1) {
            /* not already liked; add button */
            s = html_button(s, "like", L("Like"));
        }
    }
    else {
        if (is_pinned(snac, id))
            s = html_button(s, "unpin", L("Unpin"));
        else
            s = html_button(s, "pin", L("Pin"));
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

    const char *prev_src1 = xs_dict_get(msg, "sourceContent");

    if (!xs_is_null(prev_src1) && strcmp(actor, snac->actor) == 0) { /** edit **/
        xs *prev_src = encode_html(prev_src1);
        const xs_val *sensitive = xs_dict_get(msg, "sensitive");
        const char *summary = xs_dict_get(msg, "summary");

        /* post can be edited */
        xs *s1 = xs_fmt(
            "<p><details><summary>%s</summary>\n"
            "<p><div class=\"snac-note\" id=\"%s_edit\">\n"
            "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/note\" "
            "enctype=\"multipart/form-data\" id=\"%s_edit_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"edit_id\" value=\"%s\">\n"

            "<p>%s: <input type=\"checkbox\" name=\"sensitive\" %s> "
            "<input type=\"text\" name=\"summary\" placeholder=\"%s\" value=\"%s\">\n"
            "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"

            "<details><summary>%s</summary>\n"
            "<p>%s: <input type=\"file\" name=\"attach\">\n"
            "<p>%s: <input type=\"text\" name=\"alt_text\">\n"
            "</details>\n"

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
            xs_type(sensitive) == XSTYPE_TRUE ? "checked" : "",
            L("Sensitive content description"),
            xs_is_null(summary) ? "" : summary,
            L("Only for mentioned people"),
            L("Attach..."),
            L("File"),
            L("File description"),
            md5,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    { /** reply **/
        /* the post textarea */
        xs *ct = build_mentions(snac, msg);

        const xs_val *sensitive = xs_dict_get(msg, "sensitive");
        const char *summary = xs_dict_get(msg, "summary");

        xs *s1 = xs_fmt(
            "<p><details><summary>%s</summary>\n"
            "<p><div class=\"snac-note\" id=\"%s_reply\">\n"
            "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/note\" "
            "enctype=\"multipart/form-data\" id=\"%s_reply_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"in_reply_to\" value=\"%s\">\n"

            "<p>%s: <input type=\"checkbox\" name=\"sensitive\" %s> "
            "<input type=\"text\" name=\"summary\" placeholder=\"%s\" value=\"%s\">\n"
            "<p>%s: <input type=\"checkbox\" name=\"mentioned_only\">\n"

            "<details><summary>%s</summary>\n"
            "<p>%s: <input type=\"file\" name=\"attach\">\n"
            "<p>%s: <input type=\"text\" name=\"alt_text\">\n"
            "</details>\n"

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
            xs_type(sensitive) == XSTYPE_TRUE ? "checked" : "",
            L("Sensitive content description"),
            xs_is_null(summary) ? "" : summary,
            L("Only for mentioned people"),
            L("Attach..."),
            L("File"),
            L("File description"),
            md5,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


xs_str *html_entry(snac *snac, xs_str *os, const xs_dict *msg, int local,
                   int level, const char *md5, int hide_children)
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

    /* hidden? do nothing more for this conversation */
    if (is_hidden(snac, id))
        return os;

    /* avoid too deep nesting, as it may be a loop */
    if (level >= 256)
        return os;

    xs *s = xs_str_new("<div>\n");

    {
        xs *s1 = xs_fmt("<a name=\"%s_entry\"></a>\n", md5);

        s = xs_str_cat(s, s1);
    }

    if (strcmp(type, "Follow") == 0) {
        s = xs_str_cat(s, "<div class=\"snac-post\">\n<div class=\"snac-post-header\">\n");

        xs *s1 = xs_fmt("<div class=\"snac-origin\">%s</div>\n", L("follows you"));
        s = xs_str_cat(s, s1);

        s = html_msg_icon(snac, s, msg);

        s = xs_str_cat(s, "</div>\n</div>\n");

        return xs_str_cat(os, s);
    }
    else
    if (strcmp(type, "Note") != 0 && strcmp(type, "Question") != 0 && strcmp(type, "Page") != 0) {
        /* skip oddities */
        return os;
    }

    /* ignore notes with "name", as they are votes to Questions */
    if (strcmp(type, "Note") == 0 && !xs_is_null(xs_dict_get(msg, "name")))
        return os;

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return os;

    /* ignore muted morons immediately */
    if (is_muted(snac, actor))
        return os;

    if (strcmp(actor, snac->actor) != 0 && !valid_status(actor_get(snac, actor, NULL)))
        return os;

    if (level == 0)
        s = xs_str_cat(s, "<div class=\"snac-post\">\n"); /** **/
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n"); /** **/

    s = xs_str_cat(s, "<div class=\"snac-post-header\">\n<div class=\"snac-score\">"); /** **/

    if (is_pinned(snac, id)) {
        /* add a pin emoji */
        xs *f = xs_fmt("<span title=\"%s\"> &#128204; </span>", L("Pinned"));
        s = xs_str_cat(s, f);
    }

    if (strcmp(type, "Question") == 0) {
        /* add the ballot box emoji */
        xs *f = xs_fmt("<span title=\"%s\"> &#128499; </span>", L("Poll"));
        s = xs_str_cat(s, f);

        if (was_question_voted(snac, id)) {
            /* add a check to show this poll was voted */
            xs *f2 = xs_fmt("<span title=\"%s\"> &#10003; </span>", L("Voted"));
            s = xs_str_cat(s, f2);
        }
    }

    /* if this is our post, add the score */
    if (xs_startswith(id, snac->actor)) {
        int n_likes  = object_likes_len(id);
        int n_boosts = object_announces_len(id);

        /* alternate emojis: %d &#128077; %d &#128257; */

        xs *s1 = xs_fmt("%d &#9733; %d &#8634;\n", n_likes, n_boosts);

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</div>\n");

    if (boosts == NULL)
        boosts = object_announces(id);

    if (xs_list_len(boosts)) {
        /* if somebody boosted this, show as origin */
        char *p = xs_list_get(boosts, -1);
        xs *actor_r = NULL;

        if (xs_list_in(boosts, snac->md5) != -1) {
            /* we boosted this */
            xs *es1 = encode_html(xs_dict_get(snac->config, "name"));
            xs *s1 = xs_fmt(
                "<div class=\"snac-origin\">"
                "<a href=\"%s\">%s</a> %s</a></div>",
                snac->actor, es1, L("boosted")
            );

            s = xs_str_cat(s, s1);
        }
        else
        if (valid_status(object_get_by_md5(p, &actor_r))) {
            xs *name = actor_name(actor_r);

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
    s = xs_str_cat(s, "</div>\n<div class=\"e-content snac-content\">\n"); /** **/

    if (!xs_is_null(v = xs_dict_get(msg, "name"))) {
        xs *es1 = encode_html(v);
        xs *s1  = xs_fmt("<h3 class=\"snac-entry-title\">%s</h3>\n", es1);
        s = xs_str_cat(s, s1);
    }

    /* is it sensitive? */
    if (!xs_is_null(v = xs_dict_get(msg, "sensitive")) && xs_type(v) == XSTYPE_TRUE) {
        if (xs_is_null(v = xs_dict_get(msg, "summary")) || *v == '\0')
            v = "...";
        /* only show it when not in the public timeline and the config setting is "open" */
        char *cw = xs_dict_get(snac->config, "cw");
        if (xs_is_null(cw) || local)
            cw = "";
        xs *es1 = encode_html(v);
        xs *s1 = xs_fmt("<details %s><summary>%s [%s]</summary>\n", cw, es1, L("SENSITIVE CONTENT"));
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
        const char *content = xs_dict_get(msg, "content");

        xs *c  = sanitize(xs_is_null(content) ? "" : content);
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
            xs *tag = NULL;
            if (xs_type(p) == XSTYPE_DICT) {
                /* not a list */
                tag = xs_list_new();
                tag = xs_list_append(tag, p);
            } else {
                /* is a list */
                tag = xs_dup(p);
            }

            xs_list *tags = tag;

            /* iterate the tags */
            while (xs_list_iter(&tags, &v)) {
                char *t = xs_dict_get(v, "type");

                if (t && strcmp(t, "Emoji") == 0) {
                    char *n = xs_dict_get(v, "name");
                    char *i = xs_dict_get(v, "icon");

                    if (n && i) {
                        char *u = xs_dict_get(i, "url");
                        xs *img = xs_fmt("<img src=\"%s\" style=\"height: 2em; vertical-align: middle;\" "
                                         "loading=\"lazy\" title=\"%s\"/>", u, n);

                        c = xs_replace_i(c, n, img);
                    }
                }
            }
        }

        if (strcmp(type, "Question") == 0) { /** question content **/
            xs_list *oo = xs_dict_get(msg, "oneOf");
            xs_list *ao = xs_dict_get(msg, "anyOf");
            xs_list *p;
            xs_dict *v;
            int closed = 0;

            if (xs_dict_get(msg, "closed"))
                closed = 2;
            else
            if (xs_startswith(id, snac->actor))
                closed = 1; /* we questioned; closed for us */
            else
            if (was_question_voted(snac, id))
                closed = 1; /* we already voted; closed for us */

            /* get the appropriate list of options */
            p = oo != NULL ? oo : ao;

            if (closed) {
                /* closed poll */
                c = xs_str_cat(c, "<table class=\"snac-poll-result\">\n");

                while (xs_list_iter(&p, &v)) {
                    const char *name       = xs_dict_get(v, "name");
                    const xs_dict *replies = xs_dict_get(v, "replies");

                    if (name && replies) {
                        int nr = xs_number_get(xs_dict_get(replies, "totalItems"));
                        xs *es1 = encode_html(name);
                        xs *l = xs_fmt("<tr><td>%s:</td><td>%d</td></tr>\n", es1, nr);

                        c = xs_str_cat(c, l);
                    }
                }

                c = xs_str_cat(c, "</table>\n");
            }
            else {
                /* poll still active */
                xs *s1 = xs_fmt("<div class=\"snac-poll-form\">\n"
                                "<form autocomplete=\"off\" "
                                "method=\"post\" action=\"%s/admin/vote\">\n"
                                "<input type=\"hidden\" name=\"actor\" value= \"%s\">\n"
                                "<input type=\"hidden\" name=\"irt\" value=\"%s\">\n",
                    snac->actor, actor, id);

                while (xs_list_iter(&p, &v)) {
                    const char *name = xs_dict_get(v, "name");

                    if (name) {
                        xs *es1 = encode_html(name);
                        xs *opt = xs_fmt("<input type=\"%s\""
                                    " id=\"%s\" value=\"%s\" name=\"question\"> %s<br>\n",
                                    !xs_is_null(oo) ? "radio" : "checkbox",
                                    es1, es1, es1);

                        s1 = xs_str_cat(s1, opt);
                    }
                }

                xs *s2 = xs_fmt("<p><input type=\"submit\" "
                                "class=\"button\" value=\"%s\">\n</form>\n</div>\n\n", L("Vote"));

                s1 = xs_str_cat(s1, s2);

                c = xs_str_cat(c, s1);
            }

            /* if it's *really* closed, say it */
            if (closed == 2) {
                xs *s1 = xs_fmt("<p>%s</p>\n", L("Closed"));
                c = xs_str_cat(c, s1);
            }
            else {
                /* show when the poll closes */
                const char *end_time = xs_dict_get(msg, "endTime");
                if (!xs_is_null(end_time)) {
                    time_t t0 = time(NULL);
                    time_t t1 = xs_parse_iso_date(end_time, 0);

                    if (t1 > 0 && t1 > t0) {
                        time_t diff_time = t1 - t0;
                        xs *tf = xs_str_time_diff(diff_time);
                        char *p = tf;

                        /* skip leading zeros */
                        for (; *p == '0' || *p == ':'; p++);

                        xs *es1 = encode_html(p);
                        xs *s1 = xs_fmt("<p>%s %s</p>", L("Closes in"), es1);
                        c = xs_str_cat(c, s1);
                    }
                }
            }
        }

        s = xs_str_cat(s, c);
    }

    s = xs_str_cat(s, "\n");

    /* add the attachments */
    v = xs_dict_get(msg, "attachment");

    if (!xs_is_null(v)) { /** attachments **/
        xs *attach = NULL;

        /* ensure it's a list */
        if (xs_type(v) == XSTYPE_DICT) {
            attach = xs_list_new();
            attach = xs_list_append(attach, v);
        }
        else
            attach = xs_dup(v);

        /* does the message have an image? */
        if (xs_type(v = xs_dict_get(msg, "image")) == XSTYPE_DICT) {
            /* add it to the attachment list */
            attach = xs_list_append(attach, v);
        }

        /* make custom css for attachments easier */
        s = xs_str_cat(s, "<div class=\"snac-content-attachments\">\n");

        xs_list *p = attach;

        while (xs_list_iter(&p, &v)) {
            const char *t = xs_dict_get(v, "mediaType");

            if (xs_is_null(t))
                t = xs_dict_get(v, "type");

            if (xs_is_null(t))
                continue;

            const char *url = xs_dict_get(v, "url");
            if (xs_is_null(url))
                url = xs_dict_get(v, "href");
            if (xs_is_null(url))
                continue;

            const char *name = xs_dict_get(v, "name");
            if (xs_is_null(name))
                name = xs_dict_get(msg, "name");
            if (xs_is_null(name))
                name = L("No description");

            xs *es1 = encode_html(name);
            xs *s1  = NULL;

            if (xs_startswith(t, "image/") || strcmp(t, "Image") == 0) {
                s1 = xs_fmt(
                    "<a href=\"%s\" target=\"_blank\">"
                    "<img src=\"%s\" alt=\"%s\" title=\"%s\" loading=\"lazy\"/></a>\n",
                        url, url, es1, es1);
            }
            else
            if (xs_startswith(t, "video/")) {
                s1 = xs_fmt("<video style=\"width: 100%\" class=\"snac-embedded-video\" "
                        "controls src=\"%s\">Video: "
                        "<a href=\"%s\">%s</a></video>\n", url, url, es1);
            }
            else
            if (xs_startswith(t, "audio/")) {
                s1 = xs_fmt("<audio style=\"width: 100%\" class=\"snac-embedded-audio\" "
                        "controls src=\"%s\">Audio: "
                        "<a href=\"%s\">%s</a></audio>\n", url, url, es1);
            }
            else
            if (strcmp(t, "Link") == 0) {
                xs *es2 = encode_html(url);
                s1  = xs_fmt("<p><a href=\"%s\">%s</a></p>\n", url, es2);
            }
            else {
                s1 = xs_fmt("<p><a href=\"%s\">Attachment: %s</a></p>\n", url, es1);
            }

            if (!xs_is_null(s1))
                s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    /* has this message an audience (i.e., comes from a channel or community)? */
    const char *audience = xs_dict_get(msg, "audience");
    if (strcmp(type, "Page") == 0 && !xs_is_null(audience)) {
        xs *es1 = encode_html(audience);
        xs *s1 = xs_fmt("<p>(<a href=\"%s\" title=\"%s\">%s</a>)</p>\n",
            audience, L("Source channel or community"), es1);
        s = xs_str_cat(s, s1);
    }

    if (sensitive)
        s = xs_str_cat(s, "</details><p>\n");

    s = xs_str_cat(s, "</div>\n");

    /** controls **/

    if (!local)
        s = html_entry_controls(snac, s, msg, md5);

    /** children **/
    if (!hide_children) {
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

                if (chd != NULL && xs_is_null(xs_dict_get(chd, "name"))) {
                    ss = html_entry(snac, ss, chd, local, level + 1, cmd5, hide_children);
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
    }

    s = xs_str_cat(s, "</div>\n</div>\n");

    return xs_str_cat(os, s);
}


xs_str *html_user_footer(xs_str *s)
{
    xs *s1 = xs_fmt(
        "<div class=\"snac-footer\">\n"
        "<a href=\"%s\">%s</a> - "
        "powered by <a href=\"%s\">"
        "<abbr title=\"Social Networks Are Crap\">snac</abbr></a></div>\n",
        srv_baseurl,
        L("about this site"),
        WHAT_IS_SNAC_URL
    );

    return xs_str_cat(s, s1);
}


xs_str *html_timeline(snac *snac, const xs_list *list, int local, int skip, int show, int show_more)
/* returns the HTML for the timeline */
{
    xs_str *s = xs_str_new(NULL);
    xs_list *p = (xs_list *)list;
    char *v;
    double t = ftime();

    s = html_user_header(snac, s, local);

    if (!local)
        s = html_top_controls(snac, s);

    s = xs_str_cat(s, "<a name=\"snac-posts\"></a>\n");
    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&p, &v)) {
        xs *msg = NULL;

        if (!valid_status(timeline_get_by_md5(snac, v, &msg)))
            continue;

        s = html_entry(snac, s, msg, local, 0, v, 0);
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
            "<a href=\"%s%s\" name=\"snac-more\">%s</a> - "
            "<a href=\"%s%s?skip=%d&show=%d\" name=\"snac-more\">%s</a>"
            "</p>\n",
            snac->actor, local ? "" : "/admin", L("Back to top"),
            snac->actor, local ? "" : "/admin", skip + show, show, L("Older entries...")
        );

        s = xs_str_cat(s, s1);
    }

    s = html_user_footer(s);

    s = xs_str_cat(s, "</body>\n</html>\n");

    return s;
}


d_char *html_people_list(snac *snac, d_char *os, d_char *list, const char *header, const char *t)
{
    xs *s = xs_str_new(NULL);
    xs *es1 = encode_html(header);
    xs *h = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", es1);
    char *p, *actor_id;

    s = xs_str_cat(s, h);

    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    p = list;
    while (xs_list_iter(&p, &actor_id)) {
        xs *md5 = xs_md5_hex(actor_id, strlen(actor_id));
        xs *actor = NULL;

        if (valid_status(actor_get(snac, actor_id, &actor))) {
            s = xs_str_cat(s, "<div class=\"snac-post\">\n<div class=\"snac-post-header\">\n");

            s = html_actor_icon(s, actor, xs_dict_get(actor, "published"), NULL, NULL, 0);

            s = xs_str_cat(s, "</div>\n");

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
                "<p><form autocomplete=\"off\" method=\"post\" action=\"%s/admin/action\">\n"
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
                "<form autocomplete=\"off\" method=\"post\" action=\"%s/admin/note\" "
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

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_people(snac *snac)
{
    d_char *s = xs_str_new(NULL);
    xs *wing = following_list(snac);
    xs *wers = follower_list(snac);

    s = html_user_header(snac, s, 0);

    s = html_people_list(snac, s, wing, L("People you follow"), "i");

    s = html_people_list(snac, s, wers, L("People that follow you"), "e");

    s = html_user_footer(s);

    s = xs_str_cat(s, "</body>\n</html>\n");

    return s;
}


xs_str *html_notifications(snac *snac)
{
    xs_str *s  = xs_str_new(NULL);
    xs *n_list = notify_list(snac, 0);
    xs *n_time = notify_check_time(snac, 0);
    xs_list *p = n_list;
    xs_str *v;
    enum { NHDR_NONE, NHDR_NEW, NHDR_OLD } stage = NHDR_NONE;

    s = html_user_header(snac, s, 0);

    xs *s1 = xs_fmt(
        "<form autocomplete=\"off\" "
        "method=\"post\" action=\"%s/admin/clear-notifications\" id=\"clear\">\n"
        "<input type=\"submit\" class=\"snac-btn-like\" value=\"%s\">\n"
        "</form><p>\n", snac->actor, L("Clear all"));
    s = xs_str_cat(s, s1);

    while (xs_list_iter(&p, &v)) {
        xs *noti = notify_get(snac, v);

        if (noti == NULL)
            continue;

        xs *obj = NULL;
        const char *type  = xs_dict_get(noti, "type");
        const char *utype = xs_dict_get(noti, "utype");
        const char *id    = xs_dict_get(noti, "objid");

        if (xs_is_null(id) || !valid_status(object_get(id, &obj)))
            continue;

        if (is_hidden(snac, id))
            continue;

        const char *actor_id = xs_dict_get(noti, "actor");
        xs *actor = NULL;

        if (!valid_status(actor_get(snac, actor_id, &actor)))
            continue;

        xs *a_name = actor_name(actor);

        if (strcmp(v, n_time) > 0) {
            /* unseen notification */
            if (stage == NHDR_NONE) {
                xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("New"));
                s = xs_str_cat(s, s1);

                s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

                stage = NHDR_NEW;
            }
        }
        else {
            /* already seen notification */
            if (stage != NHDR_OLD) {
                if (stage == NHDR_NEW)
                    s = xs_str_cat(s, "</div>\n");

                xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("Already seen"));
                s = xs_str_cat(s, s1);

                s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

                stage = NHDR_OLD;
            }
        }

        const char *label = type;

        if (strcmp(type, "Create") == 0)
            label = L("Mention");
        else
        if (strcmp(type, "Update") == 0 && strcmp(utype, "Question") == 0)
            label = L("Finished poll");
        else
        if (strcmp(type, "Undo") == 0 && strcmp(utype, "Follow") == 0)
            label = L("Unfollow");

        xs *es1 = encode_html(label);
        xs *s1 = xs_fmt("<div class=\"snac-post-with-desc\">\n"
                        "<p><b>%s by <a href=\"%s\">%s</a></b>:</p>\n",
            es1, actor_id, a_name);
        s = xs_str_cat(s, s1);

        if (strcmp(type, "Follow") == 0 || strcmp(utype, "Follow") == 0) {
            s = xs_str_cat(s, "<div class=\"snac-post\">\n");

            s = html_actor_icon(s, actor, NULL, NULL, NULL, 0);

            s = xs_str_cat(s, "</div>\n");
        }
        else {
            xs *md5 = xs_md5_hex(id, strlen(id));

            s = html_entry(snac, s, obj, 0, 0, md5, 1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    if (stage == NHDR_NONE) {
        xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("None"));
        s = xs_str_cat(s, s1);
    }
    else
        s = xs_str_cat(s, "</div>\n");

    s = html_user_footer(s);

    s = xs_str_cat(s, "</body>\n</html>\n");

    /* set the check time to now */
    xs *dummy = notify_check_time(snac, 1);
    dummy = xs_free(dummy);

    timeline_touch(snac);

    return s;
}


int html_get_handler(const xs_dict *req, const char *q_path,
                     char **body, int *b_size, char **ctype, xs_str **etag)
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

    if (p_path == NULL) { /** public timeline **/
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

            xs *pins = pinned_list(&snac);
            pins = xs_list_cat(pins, list);

            *body = html_timeline(&snac, pins, 1, skip, show, xs_list_len(next));

            *b_size = strlen(*body);
            status  = 200;

            if (save)
                history_add(&snac, h, *body, *b_size);
        }
    }
    else
    if (strcmp(p_path, "admin") == 0) { /** private timeline **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
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

                xs *pins = pinned_list(&snac);
                pins = xs_list_cat(pins, list);

                *body = html_timeline(&snac, pins, 0, skip, show, xs_list_len(next));

                *b_size = strlen(*body);
                status  = 200;

                if (save)
                    history_add(&snac, "timeline.html_", *body, *b_size);
            }
        }
    }
    else
    if (strcmp(p_path, "people") == 0) { /** the list of people **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
        else {
            *body   = html_people(&snac);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (strcmp(p_path, "notifications") == 0) { /** the list of notifications **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
        else {
            *body   = html_notifications(&snac);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/")) { /** a timeline with just one entry **/
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
    if (xs_startswith(p_path, "s/")) { /** a static file **/
        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);
        int sz;

        if (id && *id) {
            status = static_get(&snac, id, body, &sz,
                        xs_dict_get(req, "if-none-match"), etag);

            if (valid_status(status)) {
                *b_size = sz;
                *ctype  = xs_mime_by_ext(id);
            }
        }
    }
    else
    if (xs_startswith(p_path, "h/")) { /** an entry from the history **/
        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);

        if (id && *id) {
            if (xs_endswith(id, "timeline.html_")) {
                /* Don't let them in */
                *b_size = 0;
                status = 404;
            }
            else
            if ((*body = history_get(&snac, id)) != NULL) {
                *b_size = strlen(*body);
                status  = 200;
            }
        }
    }
    else
    if (strcmp(p_path, ".rss") == 0) { /** public timeline in RSS format **/
        d_char *rss;
        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *bio   = not_really_markdown(xs_dict_get(snac.config, "bio"), NULL);
        char *p, *v;

        xs *es1 = encode_html(xs_dict_get(snac.config, "name"));
        xs *es2 = encode_html(snac.uid);
        xs *es3 = encode_html(xs_dict_get(srv_config, "host"));
        xs *es4 = encode_html(bio);
        rss = xs_fmt(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rss version=\"0.91\">\n"
            "<channel>\n"
            "<title>%s (@%s@%s)</title>\n"
            "<language>en</language>\n"
            "<link>%s.rss</link>\n"
            "<description>%s</description>\n",
            es1,
            es2,
            es3,
            snac.actor,
            es4
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

            // We SHOULD only use sanitized one for description.
            // So, only encode for feed title, while the description just keep it sanitized as is.
            xs *es_title_enc = encode_html(xs_dict_get(msg, "content"));
            xs *es_title = xs_replace(es_title_enc, "<br>", "\n");
            xs *title   = xs_str_new(NULL);
            int i;

            for (i = 0; es_title[i] && es_title[i] != '\n' && i < 50; i++)
                title = xs_append_m(title, &es_title[i], 1);

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


int html_post_handler(const xs_dict *req, const char *q_path,
                      char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)ctype;

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
        *body  = xs_dup(uid);
        return 401;
    }

    p_vars = xs_dict_get(req, "p_vars");

#if 0
    {
        xs *j1 = xs_json_dumps_pp(p_vars, 4);
        printf("%s\n", j1);
    }
#endif

    if (p_path && strcmp(p_path, "admin/note") == 0) { /** **/
        /* post note */
        xs_str *content      = xs_dict_get(p_vars, "content");
        xs_str *in_reply_to  = xs_dict_get(p_vars, "in_reply_to");
        xs_str *attach_url   = xs_dict_get(p_vars, "attach_url");
        xs_list *attach_file = xs_dict_get(p_vars, "attach");
        xs_str *to           = xs_dict_get(p_vars, "to");
        xs_str *sensitive    = xs_dict_get(p_vars, "sensitive");
        xs_str *summary      = xs_dict_get(p_vars, "summary");
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
            xs *poll_opts = NULL;

            /* is there a valid set of poll options? */
            const char *v = xs_dict_get(p_vars, "poll_options");
            if (!xs_is_null(v) && *v) {
                xs *v2 = xs_strip_i(xs_replace(v, "\r", ""));

                poll_opts = xs_split(v2, "\n");
            }

            if (!xs_is_null(poll_opts) && xs_list_len(poll_opts)) {
                /* get the rest of poll configuration */
                const char *p_multiple = xs_dict_get(p_vars, "poll_multiple");
                const char *p_end_secs = xs_dict_get(p_vars, "poll_end_secs");
                int multiple = 0;

                int end_secs = atoi(!xs_is_null(p_end_secs) ? p_end_secs : "60");

                if (!xs_is_null(p_multiple) && strcmp(p_multiple, "on") == 0)
                    multiple = 1;

                msg = msg_question(&snac, content_2, attach_list,
                                   poll_opts, multiple, end_secs);

                enqueue_close_question(&snac, xs_dict_get(msg, "id"), end_secs);
            }
            else
                msg = msg_note(&snac, content_2, to, in_reply_to, attach_list, priv);

            if (sensitive != NULL) {
                msg = xs_dict_set(msg, "sensitive", xs_stock_true);
                msg = xs_dict_set(msg, "summary",   xs_is_null(summary) ? "..." : summary);
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
                    xs *updated = xs_str_utctime(0, ISO_DATE_SPEC);
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

            history_del(&snac, "timeline.html_");
        }

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/action") == 0) { /** **/
        /* action on an entry */
        char *id     = xs_dict_get(p_vars, "id");
        char *actor  = xs_dict_get(p_vars, "actor");
        char *action = xs_dict_get(p_vars, "action");

        if (action == NULL)
            return 404;

        snac_debug(&snac, 1, xs_fmt("web action '%s' received", action));

        status = 303;

        if (strcmp(action, L("Like")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Like");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 1);
            }
        }
        else
        if (strcmp(action, L("Boost")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Announce");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 0);
            }
        }
        else
        if (strcmp(action, L("MUTE")) == 0) { /** **/
            mute(&snac, actor);
        }
        else
        if (strcmp(action, L("Unmute")) == 0) { /** **/
            unmute(&snac, actor);
        }
        else
        if (strcmp(action, L("Hide")) == 0) { /** **/
            hide(&snac, id);
        }
        else
        if (strcmp(action, L("Follow")) == 0) { /** **/
            xs *msg = msg_follow(&snac, actor);

            if (msg != NULL) {
                /* reload the actor from the message, in may be different */
                actor = xs_dict_get(msg, "object");

                following_add(&snac, actor, msg);

                enqueue_output_by_actor(&snac, msg, actor, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow")) == 0) { /** **/
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
        if (strcmp(action, L("Delete")) == 0) { /** **/
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
        if (strcmp(action, L("Pin")) == 0) { /** **/
            pin(&snac, id);
            timeline_touch(&snac);
        }
        else
        if (strcmp(action, L("Unpin")) == 0) { /** **/
            unpin(&snac, id);
            timeline_touch(&snac);
        }
        else
            status = 404;

        /* delete the cached timeline */
        if (status == 303)
            history_del(&snac, "timeline.html_");
    }
    else
    if (p_path && strcmp(p_path, "admin/user-setup") == 0) { /** **/
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
        if ((v = xs_dict_get(p_vars, "drop_dm_from_unknown")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock_true);
        else
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock_false);
        if ((v = xs_dict_get(p_vars, "bot")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "bot", xs_stock_true);
        else
            snac.config = xs_dict_set(snac.config, "bot", xs_stock_false);

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
    else
    if (p_path && strcmp(p_path, "admin/clear-notifications") == 0) { /** **/
        notify_clear(&snac);
        timeline_touch(&snac);

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/vote") == 0) { /** **/
        char *irt         = xs_dict_get(p_vars, "irt");
        const char *opt   = xs_dict_get(p_vars, "question");
        const char *actor = xs_dict_get(p_vars, "actor");

        xs *ls = NULL;

        /* multiple choices? */
        if (xs_type(opt) == XSTYPE_LIST)
            ls = xs_dup(opt);
        else {
            ls = xs_list_new();
            ls = xs_list_append(ls, opt);
        }

        xs_list *p = ls;
        xs_str *v;

        while (xs_list_iter(&p, &v)) {
            xs *msg = msg_note(&snac, "", actor, irt, NULL, 1);

            /* set the option */
            msg = xs_dict_append(msg, "name", v);

            xs *c_msg = msg_create(&snac, msg);

            enqueue_message(&snac, c_msg);

            timeline_add(&snac, xs_dict_get(msg, "id"), msg);
        }

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
