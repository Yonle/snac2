/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef NO_MASTODON_API

#include "xs.h"
#include "xs_encdec.h"
#include "xs_openssl.h"
#include "xs_json.h"
#include "xs_io.h"
#include "xs_time.h"

#include "snac.h"

static xs_str *random_str(void)
/* just what is says in the tin */
{
    unsigned int data[4] = {0};
    FILE *f;

    if ((f = fopen("/dev/random", "r")) != NULL) {
        fread(data, sizeof(data), 1, f);
        fclose(f);
    }
    else {
        data[0] = random() % 0xffffffff;
        data[1] = random() % 0xffffffff;
        data[2] = random() % 0xffffffff;
        data[3] = random() % 0xffffffff;
    }

    return xs_hex_enc((char *)data, sizeof(data));
}


int app_add(const char *id, const xs_dict *app)
/* stores an app */
{
    if (!xs_is_hex(id))
        return 500;

    int status = 201;
    xs *fn     = xs_fmt("%s/app/", srv_basedir);
    FILE *f;

    mkdirx(fn);
    fn = xs_str_cat(fn, id);
    fn = xs_str_cat(fn, ".json");

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(app, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }
    else
        status = 500;

    return status;
}


xs_dict *app_get(const char *id)
/* gets an app */
{
    if (!xs_is_hex(id))
        return NULL;

    xs *fn       = xs_fmt("%s/app/%s.json", srv_basedir, id);
    xs_dict *app = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);
        fclose(f);

        app = xs_json_loads(j);
    }

    return app;
}


int app_del(const char *id)
/* deletes an app */
{
    if (!xs_is_hex(id))
        return -1;

    xs *fn = xs_fmt("%s/app/%s.json", srv_basedir, id);

    return unlink(fn);
}


int token_add(const char *id, const xs_dict *token)
/* stores a token */
{
    if (!xs_is_hex(id))
        return 500;

    int status = 201;
    xs *fn     = xs_fmt("%s/token/", srv_basedir);
    FILE *f;

    mkdirx(fn);
    fn = xs_str_cat(fn, id);
    fn = xs_str_cat(fn, ".json");

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(token, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }
    else
        status = 500;

    return status;
}


xs_dict *token_get(const char *id)
/* gets a token */
{
    if (!xs_is_hex(id))
        return NULL;

    xs *fn         = xs_fmt("%s/token/%s.json", srv_basedir, id);
    xs_dict *token = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);
        fclose(f);

        token = xs_json_loads(j);
    }

    return token;
}


int token_del(const char *id)
/* deletes a token */
{
    if (!xs_is_hex(id))
        return -1;

    xs *fn = xs_fmt("%s/token/%s.json", srv_basedir, id);

    return unlink(fn);
}


const char *login_page = ""
"<!DOCTYPE html>\n"
"<body><h1>%s OAuth identify</h1>\n"
"<div style=\"background-color: red; color: white\">%s</div>\n"
"<form method=\"post\" action=\"https:/" "/%s/oauth/x-snac-login\">\n"
"<p>Login: <input type=\"text\" name=\"login\"></p>\n"
"<p>Password: <input type=\"password\" name=\"passwd\"></p>\n"
"<input type=\"hidden\" name=\"redir\" value=\"%s\">\n"
"<input type=\"hidden\" name=\"cid\" value=\"%s\">\n"
"<input type=\"hidden\" name=\"state\" value=\"%s\">\n"
"<input type=\"submit\" value=\"OK\">\n"
"</form><p>%s</p></body>\n"
"";

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

/*    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth get:\n%s\n", j);
    }*/

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    srv_debug(1, xs_fmt("oauth_get_handler %s", q_path));

    if (strcmp(cmd, "/authorize") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *rtype = xs_dict_get(msg, "response_type");
        const char *state = xs_dict_get(msg, "state");

        status = 400;

        if (cid && ruri && rtype && strcmp(rtype, "code") == 0) {
            xs *app = app_get(cid);

            if (app != NULL) {
                const char *host = xs_dict_get(srv_config, "host");

                if (xs_is_null(state))
                    state = "";

                *body  = xs_fmt(login_page, host, "", host, ruri, cid, state, USER_AGENT);
                *ctype = "text/html";
                status = 200;

                srv_debug(0, xs_fmt("oauth authorize: generating login page"));
            }
            else
                srv_debug(0, xs_fmt("oauth authorize: bad client_id %s", cid));
        }
        else
            srv_debug(0, xs_fmt("oauth authorize: invalid or unset arguments"));
    }

    return status;
}


int oauth_post_handler(const xs_dict *req, const char *q_path,
                       const char *payload, int p_size,
                       char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

/*    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth post:\n%s\n", j);
    }*/

    int status   = 404;

    char *i_ctype = xs_dict_get(req, "content-type");
    xs *args      = NULL;

    if (i_ctype && xs_startswith(i_ctype, "application/json"))
        args = xs_json_loads(payload);
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    xs *cmd = xs_replace(q_path, "/oauth", "");

    srv_debug(1, xs_fmt("oauth_post_handler %s", q_path));

    if (strcmp(cmd, "/x-snac-login") == 0) {
        const char *login  = xs_dict_get(args, "login");
        const char *passwd = xs_dict_get(args, "passwd");
        const char *redir  = xs_dict_get(args, "redir");
        const char *cid    = xs_dict_get(args, "cid");
        const char *state  = xs_dict_get(args, "state");

        const char *host = xs_dict_get(srv_config, "host");

        /* by default, generate another login form with an error */
        *body  = xs_fmt(login_page, host, "LOGIN INCORRECT", host, redir, cid, state, USER_AGENT);
        *ctype = "text/html";
        status = 200;

        if (login && passwd && redir && cid) {
            snac snac;

            if (user_open(&snac, login)) {
                /* check the login + password */
                if (check_password(login, passwd,
                    xs_dict_get(snac.config, "passwd"))) {
                    /* success! redirect to the desired uri */
                    xs *code = random_str();

                    xs_free(*body);
                    *body = xs_fmt("%s?code=%s", redir, code);
                    status = 303;

                    /* if there is a state, add it */
                    if (!xs_is_null(state) && *state) {
                        *body = xs_str_cat(*body, "&state=");
                        *body = xs_str_cat(*body, state);
                    }

                    srv_log(xs_fmt("oauth x-snac-login: '%s' success, redirect to %s",
                                   login, *body));

                    /* assign the login to the app */
                    xs *app = app_get(cid);

                    if (app != NULL) {
                        app = xs_dict_set(app, "uid",  login);
                        app = xs_dict_set(app, "code", code);
                        app_add(cid, app);
                    }
                    else
                        srv_log(xs_fmt("oauth x-snac-login: error getting app %s", cid));
                }
                else
                    srv_debug(1, xs_fmt("oauth x-snac-login: login '%s' incorrect", login));

                user_free(&snac);
            }
            else
                srv_debug(1, xs_fmt("oauth x-snac-login: bad user '%s'", login));
        }
        else
            srv_debug(1, xs_fmt("oauth x-snac-login: invalid or unset arguments"));
    }
    else
    if (strcmp(cmd, "/token") == 0) {
        xs *wrk = NULL;
        const char *gtype = xs_dict_get(args, "grant_type");
        const char *code  = xs_dict_get(args, "code");
        const char *cid   = xs_dict_get(args, "client_id");
        const char *csec  = xs_dict_get(args, "client_secret");
        const char *ruri  = xs_dict_get(args, "redirect_uri");
        /* FIXME: this 'scope' parameter is mandatory for the official Mastodon API,
           but if it's enabled, it makes it crash after some more steps, which
           is FAR WORSE */
//        const char *scope = xs_dict_get(args, "scope");
        const char *scope = NULL;

        /* no client_secret? check if it's inside an authorization header
           (AndStatus does it this way) */
        if (xs_is_null(csec)) {
            const char *auhdr = xs_dict_get(req, "authorization");

            if (!xs_is_null(auhdr) && xs_startswith(auhdr, "Basic ")) {
                xs *s1 = xs_replace(auhdr, "Basic ", "");
                int size;
                xs *s2 = xs_base64_dec(s1, &size);

                if (!xs_is_null(s2)) {
                    xs *l1 = xs_split(s2, ":");

                    if (xs_list_len(l1) == 2) {
                        wrk = xs_dup(xs_list_get(l1, 1));
                        csec = wrk;
                    }
                }
            }
        }

        if (gtype && code && cid && csec && ruri) {
            xs *app = app_get(cid);

            if (app == NULL) {
                status = 401;
                srv_log(xs_fmt("oauth token: invalid app %s", cid));
            }
            else
            if (strcmp(csec, xs_dict_get(app, "client_secret")) != 0) {
                status = 401;
                srv_log(xs_fmt("oauth token: invalid client_secret for app %s", cid));
            }
            else {
                xs *rsp   = xs_dict_new();
                xs *cat   = xs_number_new(time(NULL));
                xs *tokid = random_str();

                rsp = xs_dict_append(rsp, "access_token", tokid);
                rsp = xs_dict_append(rsp, "token_type",   "Bearer");
                rsp = xs_dict_append(rsp, "created_at",   cat);

                if (!xs_is_null(scope))
                    rsp = xs_dict_append(rsp, "scope", scope);

                *body  = xs_json_dumps_pp(rsp, 4);
                *ctype = "application/json";
                status = 200;

                const char *uid = xs_dict_get(app, "uid");

                srv_debug(0, xs_fmt("oauth token: "
                                "successful login for %s, new token %s", uid, tokid));

                xs *token = xs_dict_new();
                token = xs_dict_append(token, "token",         tokid);
                token = xs_dict_append(token, "client_id",     cid);
                token = xs_dict_append(token, "client_secret", csec);
                token = xs_dict_append(token, "uid",           uid);
                token = xs_dict_append(token, "code",          code);

                token_add(tokid, token);
            }
        }
        else {
            srv_debug(0, xs_fmt("oauth token: invalid or unset arguments"));
            status = 400;
        }
    }
    else
    if (strcmp(cmd, "/revoke") == 0) {
        const char *cid   = xs_dict_get(args, "client_id");
        const char *csec  = xs_dict_get(args, "client_secret");
        const char *tokid = xs_dict_get(args, "token");

        if (cid && csec && tokid) {
            xs *token = token_get(tokid);

            *body  = xs_str_new("{}");
            *ctype = "application/json";

            if (token == NULL || strcmp(csec, xs_dict_get(token, "client_secret")) != 0) {
                srv_debug(1, xs_fmt("oauth revoke: bad secret for token %s", tokid));
                status = 403;
            }
            else {
                token_del(tokid);
                srv_debug(0, xs_fmt("oauth revoke: revoked token %s", tokid));
                status = 200;

                /* also delete the app, as it serves no purpose from now on */
                app_del(cid);
            }
        }
        else {
            srv_debug(0, xs_fmt("oauth revoke: invalid or unset arguments"));
            status = 403;
        }
    }

    return status;
}


xs_str *mastoapi_id(const xs_dict *msg)
/* returns a somewhat Mastodon-compatible status id */
{
    const char *id = xs_dict_get(msg, "id");
    xs *md5        = xs_md5_hex(id, strlen(id));

    return xs_fmt("%10.0f%s", object_ctime_by_md5(md5), md5);
}

#define MID_TO_MD5(id) (id + 10)


xs_dict *mastoapi_account(const xs_dict *actor)
/* converts an ActivityPub actor to a Mastodon account */
{
    xs_dict *acct = xs_dict_new();

    const char *display_name = xs_dict_get(actor, "name");
    if (xs_is_null(display_name) || *display_name == '\0')
        display_name = xs_dict_get(actor, "preferredUsername");

    const char *id  = xs_dict_get(actor, "id");
    const char *pub = xs_dict_get(actor, "published");
    xs *acct_md5 = xs_md5_hex(id, strlen(id));
    acct = xs_dict_append(acct, "id",           acct_md5);
    acct = xs_dict_append(acct, "username",     xs_dict_get(actor, "preferredUsername"));
    acct = xs_dict_append(acct, "acct",         xs_dict_get(actor, "preferredUsername"));
    acct = xs_dict_append(acct, "display_name", display_name);

    if (pub)
        acct = xs_dict_append(acct, "created_at", pub);
    else {
        /* unset created_at crashes Tusky, so lie like a mf */
        xs *date = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");
        acct = xs_dict_append(acct, "created_at", date);
    }

    const char *note = xs_dict_get(actor, "summary");
    if (xs_is_null(note))
        note = "";

    acct = xs_dict_append(acct, "note", note);

    acct = xs_dict_append(acct, "url", id);

    xs *avatar  = NULL;
    xs_dict *av = xs_dict_get(actor, "icon");

    if (xs_type(av) == XSTYPE_DICT)
        avatar = xs_dup(xs_dict_get(av, "url"));
    else
        avatar = xs_fmt("%s/susie.png", srv_baseurl);

    acct = xs_dict_append(acct, "avatar", avatar);

    /* emojis */
    xs_list *p;
    if (!xs_is_null(p = xs_dict_get(actor, "tag"))) {
        xs *eml = xs_list_new();
        xs_dict *v;
        xs *t = xs_val_new(XSTYPE_TRUE);

        while (xs_list_iter(&p, &v)) {
            const char *type = xs_dict_get(v, "type");

            if (!xs_is_null(type) && strcmp(type, "Emoji") == 0) {
                const char *name    = xs_dict_get(v, "name");
                const xs_dict *icon = xs_dict_get(v, "icon");

                if (!xs_is_null(name) && !xs_is_null(icon)) {
                    const char *url = xs_dict_get(icon, "url");

                    if (!xs_is_null(url)) {
                        xs *nm = xs_strip_chars_i(xs_dup(name), ":");
                        xs *d1 = xs_dict_new();

                        d1 = xs_dict_append(d1, "shortcode",         nm);
                        d1 = xs_dict_append(d1, "url",               url);
                        d1 = xs_dict_append(d1, "static_url",        url);
                        d1 = xs_dict_append(d1, "visible_in_picker", t);

                        eml = xs_list_append(eml, d1);
                    }
                }
            }
        }

        acct = xs_dict_append(acct, "emojis", eml);
    }

    return acct;
}


xs_dict *mastoapi_status(snac *snac, const xs_dict *msg)
/* converts an ActivityPub note to a Mastodon status */
{
    xs *actor = NULL;
    actor_get(snac, xs_dict_get(msg, "attributedTo"), &actor);

    /* if the author is not here, discard */
    if (actor == NULL)
        return NULL;

    xs *acct = mastoapi_account(actor);

    /** shave the yak converting an ActivityPub Note to a Mastodon status **/

    xs *f   = xs_val_new(XSTYPE_FALSE);
    xs *t   = xs_val_new(XSTYPE_TRUE);
    xs *n   = xs_val_new(XSTYPE_NULL);
    xs *el  = xs_list_new();
    xs *idx = NULL;
    xs *ixc = NULL;

    char *tmp;
    char *id = xs_dict_get(msg, "id");
    xs *mid  = mastoapi_id(msg);

    xs_dict *st = xs_dict_new();

    st = xs_dict_append(st, "id",           mid);
    st = xs_dict_append(st, "uri",          id);
    st = xs_dict_append(st, "url",          id);
    st = xs_dict_append(st, "created_at",   xs_dict_get(msg, "published"));
    st = xs_dict_append(st, "account",      acct);
    st = xs_dict_append(st, "content",      xs_dict_get(msg, "content"));

    st = xs_dict_append(st, "visibility",
        is_msg_public(snac, msg) ? "public" : "private");

    tmp = xs_dict_get(msg, "sensitive");
    if (xs_is_null(tmp))
        tmp = f;

    st = xs_dict_append(st, "sensitive",    tmp);

    tmp = xs_dict_get(msg, "summary");
    if (xs_is_null(tmp))
        tmp = "";

    st = xs_dict_append(st, "spoiler_text", tmp);

    /* create the list of attachments */
    xs *matt = xs_list_new();
    xs_list *att = xs_dict_get(msg, "attachment");
    xs_str *aobj;

    while (xs_list_iter(&att, &aobj)) {
        const char *mtype = xs_dict_get(aobj, "mediaType");

        if (!xs_is_null(mtype) && xs_startswith(mtype, "image/")) {
            xs *matteid = xs_fmt("%s_%d", id, xs_list_len(matt));
            xs *matte   = xs_dict_new();

            matte = xs_dict_append(matte, "id",          matteid);
            matte = xs_dict_append(matte, "type",        "image");
            matte = xs_dict_append(matte, "url",         xs_dict_get(aobj, "url"));
            matte = xs_dict_append(matte, "preview_url", xs_dict_get(aobj, "url"));
            matte = xs_dict_append(matte, "remote_url",  xs_dict_get(aobj, "url"));

            const char *name = xs_dict_get(aobj, "name");
            if (xs_is_null(name))
                name = "";

            matte = xs_dict_append(matte, "description", name);

            matt = xs_list_append(matt, matte);
        }
    }

    st = xs_dict_append(st, "media_attachments", matt);

    {
        xs *ml  = xs_list_new();
        xs *htl = xs_list_new();
        xs *eml = xs_list_new();
        xs_list *p = xs_dict_get(msg, "tag");
        xs_dict *v;
        int n = 0;

        while (xs_list_iter(&p, &v)) {
            const char *type = xs_dict_get(v, "type");

            if (xs_is_null(type))
                continue;

            xs *d1 = xs_dict_new();

            if (strcmp(type, "Mention") == 0) {
                const char *name = xs_dict_get(v, "name");
                const char *href = xs_dict_get(v, "href");

                if (!xs_is_null(name) && !xs_is_null(href) &&
                    strcmp(href, snac->actor) != 0) {
                    xs *nm = xs_strip_chars_i(xs_dup(name), "@");

                    xs *id = xs_fmt("%d", n++);
                    d1 = xs_dict_append(d1, "id", id);
                    d1 = xs_dict_append(d1, "username", nm);
                    d1 = xs_dict_append(d1, "acct", nm);
                    d1 = xs_dict_append(d1, "url", href);

                    ml = xs_list_append(ml, d1);
                }
            }
            else
            if (strcmp(type, "Hashtag") == 0) {
                const char *name = xs_dict_get(v, "name");
                const char *href = xs_dict_get(v, "href");

                if (!xs_is_null(name) && !xs_is_null(href)) {
                    xs *nm = xs_strip_chars_i(xs_dup(name), "#");

                    d1 = xs_dict_append(d1, "name", nm);
                    d1 = xs_dict_append(d1, "url", href);

                    htl = xs_list_append(htl, d1);
                }
            }
            else
            if (strcmp(type, "Emoji") == 0) {
                const char *name    = xs_dict_get(v, "name");
                const xs_dict *icon = xs_dict_get(v, "icon");

                if (!xs_is_null(name) && !xs_is_null(icon)) {
                    const char *url = xs_dict_get(icon, "url");

                    if (!xs_is_null(url)) {
                        xs *nm = xs_strip_chars_i(xs_dup(name), ":");
                        xs *t  = xs_val_new(XSTYPE_TRUE);

                        d1 = xs_dict_append(d1, "shortcode", nm);
                        d1 = xs_dict_append(d1, "url", url);
                        d1 = xs_dict_append(d1, "static_url", url);
                        d1 = xs_dict_append(d1, "visible_in_picker", t);
                        d1 = xs_dict_append(d1, "category", "Emojis");

                        eml = xs_list_append(eml, d1);
                    }
                }
            }
        }

        st = xs_dict_append(st, "mentions", ml);
        st = xs_dict_append(st, "tags",     htl);
        st = xs_dict_append(st, "emojis",   eml);
    }

    xs_free(idx);
    xs_free(ixc);
    idx = object_likes(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "favourites_count", ixc);
    st = xs_dict_append(st, "favourited",
        xs_list_in(idx, snac->md5) != -1 ? t : f);

    xs_free(idx);
    xs_free(ixc);
    idx = object_announces(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "reblogs_count", ixc);
    st = xs_dict_append(st, "reblogged",
        xs_list_in(idx, snac->md5) != -1 ? t : f);

    xs_free(idx);
    xs_free(ixc);
    idx = object_children(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "replies_count", ixc);

    /* default in_reply_to values */
    st = xs_dict_append(st, "in_reply_to_id",         n);
    st = xs_dict_append(st, "in_reply_to_account_id", n);

    tmp = xs_dict_get(msg, "inReplyTo");
    if (!xs_is_null(tmp)) {
        xs *irto = NULL;

        if (valid_status(object_get(tmp, &irto))) {
            xs *irt_mid = mastoapi_id(irto);
            st = xs_dict_set(st, "in_reply_to_id", irt_mid);

            char *at = NULL;
            if (!xs_is_null(at = xs_dict_get(irto, "attributedTo"))) {
                xs *at_md5 = xs_md5_hex(at, strlen(at));
                st = xs_dict_set(st, "in_reply_to_account_id", at_md5);
            }
        }
    }

    st = xs_dict_append(st, "reblog",   n);
    st = xs_dict_append(st, "poll",     n);
    st = xs_dict_append(st, "card",     n);
    st = xs_dict_append(st, "language", n);

    tmp = xs_dict_get(msg, "sourceContent");
    if (xs_is_null(tmp))
        tmp = "";

    st = xs_dict_append(st, "text", tmp);

    tmp = xs_dict_get(msg, "updated");
    if (xs_is_null(tmp))
        tmp = n;

    st = xs_dict_append(st, "edited_at", tmp);

    return st;
}


xs_dict *mastoapi_relationship(snac *snac, const char *md5)
{
    xs_dict *rel = NULL;
    xs *actor_o  = NULL;

    if (valid_status(object_get_by_md5(md5, &actor_o))) {
        xs *t   = xs_val_new(XSTYPE_TRUE);
        xs *f   = xs_val_new(XSTYPE_FALSE);
        rel = xs_dict_new();

        const char *actor = xs_dict_get(actor_o, "id");

        rel = xs_dict_append(rel, "id",                   md5);
        rel = xs_dict_append(rel, "following",
            following_check(snac, actor) ? t : f);

        rel = xs_dict_append(rel, "showing_reblogs",      t);
        rel = xs_dict_append(rel, "notifying",            f);
        rel = xs_dict_append(rel, "followed_by",
            follower_check(snac, actor) ? t : f);

        rel = xs_dict_append(rel, "blocking",
            is_muted(snac, actor) ? t : f);

        rel = xs_dict_append(rel, "muting",               f);
        rel = xs_dict_append(rel, "muting_notifications", f);
        rel = xs_dict_append(rel, "requested",            f);
        rel = xs_dict_append(rel, "domain_blocking",      f);
        rel = xs_dict_append(rel, "endorsed",             f);
        rel = xs_dict_append(rel, "note",                 "");
    }

    return rel;
}


int process_auth_token(snac *snac, const xs_dict *req)
/* processes an authorization token, if there is one */
{
    int logged_in = 0;
    char *v;

    /* if there is an authorization field, try to validate it */
    if (!xs_is_null(v = xs_dict_get(req, "authorization")) && xs_startswith(v, "Bearer ")) {
        xs *tokid = xs_replace(v, "Bearer ", "");
        xs *token = token_get(tokid);

        if (token != NULL) {
            const char *uid = xs_dict_get(token, "uid");

            if (!xs_is_null(uid) && user_open(snac, uid)) {
                logged_in = 1;

                /* this counts as a 'login' */
                lastlog_write(snac);

                srv_debug(2, xs_fmt("mastoapi auth: valid token for user %s", uid));
            }
            else
                srv_log(xs_fmt("mastoapi auth: corrupted token %s", tokid));
        }
        else
            srv_log(xs_fmt("mastoapi auth: invalid token %s", tokid));
    }

    return logged_in;
}


int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    srv_debug(1, xs_fmt("mastoapi_get_handler %s", q_path));
/*    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi get:\n%s\n", j);
    }*/

    int status    = 404;
    xs_dict *args = xs_dict_get(req, "q_vars");
    xs *cmd       = xs_replace(q_path, "/api", "");

    snac snac1 = {0};
    int logged_in = process_auth_token(&snac1, req);

    if (strcmp(cmd, "/v1/accounts/verify_credentials") == 0) {
        if (logged_in) {
            xs *acct = xs_dict_new();

            acct = xs_dict_append(acct, "id",           xs_dict_get(snac1.config, "uid"));
            acct = xs_dict_append(acct, "username",     xs_dict_get(snac1.config, "uid"));
            acct = xs_dict_append(acct, "acct",         xs_dict_get(snac1.config, "uid"));
            acct = xs_dict_append(acct, "display_name", xs_dict_get(snac1.config, "name"));
            acct = xs_dict_append(acct, "created_at",   xs_dict_get(snac1.config, "published"));
            acct = xs_dict_append(acct, "note",         xs_dict_get(snac1.config, "bio"));
            acct = xs_dict_append(acct, "url",          snac1.actor);
            acct = xs_dict_append(acct, "header",       "");

            xs *avatar = NULL;
            char *av   = xs_dict_get(snac1.config, "avatar");

            if (xs_is_null(av) || *av == '\0')
                avatar = xs_fmt("%s/susie.png", srv_baseurl);
            else
                avatar = xs_dup(av);

            acct = xs_dict_append(acct, "avatar", avatar);

            *body  = xs_json_dumps_pp(acct, 4);
            *ctype = "application/json";
            status = 200;
        }
        else {
            status = 422;   // "Unprocessable entity" (no login)
        }
    }
    else
    if (strcmp(cmd, "/v1/accounts/relationships") == 0) {
        /* find if an account is followed, blocked, etc. */
        /* the account to get relationships about is in args "id[]" */

        if (logged_in) {
            xs *res         = xs_list_new();
            const char *md5 = xs_dict_get(args, "id[]");

            if (!xs_is_null(md5)) {
                xs *rel = mastoapi_relationship(&snac1, md5);

                if (rel != NULL)
                    res = xs_list_append(res, rel);
            }

            *body  = xs_json_dumps_pp(res, 4);
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 422;
    }
    else
    if (xs_startswith(cmd, "/v1/accounts/")) {
        /* account-related information */
        xs *l = xs_split(cmd, "/");
        const char *uid = xs_list_get(l, 3);
        const char *opt = xs_list_get(l, 4);

        if (uid != NULL) {
            snac snac2;
            xs *out   = NULL;
            xs *actor = NULL;

            /* is it a local user? */
            if (user_open(&snac2, uid) || user_open_by_md5(&snac2, uid)) {
                if (opt == NULL) {
                    /* account information */
                    actor = msg_actor(&snac2);
                    out   = mastoapi_account(actor);
                }
                else
                if (strcmp(opt, "statuses") == 0) {
                    /* the public list of posts of a user */
                    xs *timeline = timeline_simple_list(&snac2, "public", 0, 256);
                    xs_list *p   = timeline;
                    xs_str *v;

                    out = xs_list_new();

                    while (xs_list_iter(&p, &v)) {
                        xs *msg = NULL;

                        if (valid_status(timeline_get_by_md5(&snac2, v, &msg))) {
                            /* add only posts by the author */
                            if (strcmp(xs_dict_get(msg, "type"), "Note") == 0 &&
                                xs_startswith(xs_dict_get(msg, "id"), snac2.actor)) {
                                xs *st = mastoapi_status(&snac2, msg);

                                out = xs_list_append(out, st);
                            }
                        }
                    }
                }

                user_free(&snac2);
            }
            else {
                /* try the uid as the md5 of a possibly loaded actor */
                if (logged_in && valid_status(object_get_by_md5(uid, &actor))) {
                    if (opt == NULL) {
                        /* account information */
                        out = mastoapi_account(actor);
                    }
                    else
                    if (strcmp(opt, "statuses") == 0) {
                        /* we don't serve statuses of others; return the empty list */
                        out = xs_list_new();
                    }
                }
            }

            if (out != NULL) {
                *body  = xs_json_dumps_pp(out, 4);
                *ctype = "application/json";
                status = 200;
            }
        }
    }
    else
    if (strcmp(cmd, "/v1/timelines/home") == 0) {
        /* the private timeline */
        if (logged_in) {
            const char *max_id   = xs_dict_get(args, "max_id");
            const char *since_id = xs_dict_get(args, "since_id");
            const char *min_id   = xs_dict_get(args, "min_id");
            const char *limit_s  = xs_dict_get(args, "limit");
            int limit = 0;
            int cnt   = 0;

            if (!xs_is_null(limit_s))
                limit = atoi(limit_s);

            if (limit == 0)
                limit = 20;

            xs *timeline = timeline_simple_list(&snac1, "private", 0, 256);

            xs *out      = xs_list_new();
            xs_list *p   = timeline;
            xs_str *v;

            while (xs_list_iter(&p, &v) && cnt < limit) {
                xs *msg = NULL;

                /* only return entries older that max_id */
                if (max_id) {
                    if (strcmp(v, max_id) == 0)
                        max_id = NULL;

                    continue;
                }

                /* only returns entries newer than since_id */
                if (since_id) {
                    if (strcmp(v, since_id) == 0)
                        break;
                }

                /* only returns entries newer than min_id */
                /* what does really "Return results immediately newer than ID" mean? */
                if (min_id) {
                    if (strcmp(v, min_id) == 0)
                        break;
                }

                /* get the entry */
                if (!valid_status(timeline_get_by_md5(&snac1, v, &msg)))
                    continue;

                /* discard non-Notes */
                if (strcmp(xs_dict_get(msg, "type"), "Note") != 0)
                    continue;

                /* drop notes from muted morons */
                if (is_muted(&snac1, xs_dict_get(msg, "attributedTo")))
                    continue;

                /* convert the Note into a Mastodon status */
                xs *st = mastoapi_status(&snac1, msg);

                if (st != NULL)
                    out = xs_list_append(out, st);

                cnt++;
            }

            *body  = xs_json_dumps_pp(out, 4);
            *ctype = "application/json";
            status = 200;

            srv_debug(2, xs_fmt("mastoapi timeline: returned %d entries", xs_list_len(out)));
        }
        else {
            status = 401; // unauthorized
        }
    }
    else
    if (strcmp(cmd, "/v1/timelines/public") == 0) {
        /* the public timeline (public timelines for all users) */

        /* this is an ugly kludge: first users in the list get all the fame */

        const char *limit_s  = xs_dict_get(args, "limit");
        int limit = 0;
        int cnt   = 0;

        if (!xs_is_null(limit_s))
            limit = atoi(limit_s);

        if (limit == 0)
            limit = 20;

        xs *out    = xs_list_new();
        xs *users  = user_list();
        xs_list *p = users;
        xs_str *uid;

        while (xs_list_iter(&p, &uid) && cnt < limit) {
            snac user;

            if (user_open(&user, uid)) {
                xs *timeline = timeline_simple_list(&user, "public", 0, 4);
                xs_list *p2  = timeline;
                xs_str *v;

                while (xs_list_iter(&p2, &v) && cnt < limit) {
                    xs *msg = NULL;

                    /* get the entry */
                    if (!valid_status(timeline_get_by_md5(&user, v, &msg)))
                        continue;

                    /* discard non-Notes */
                    if (strcmp(xs_dict_get(msg, "type"), "Note") != 0)
                        continue;

                    /* discard entries not by this user */
                    if (!xs_startswith(xs_dict_get(msg, "id"), user.actor))
                        continue;

                    /* convert the Note into a Mastodon status */
                    xs *st = mastoapi_status(&user, msg);

                    if (st != NULL) {
                        out = xs_list_append(out, st);
                        cnt++;
                    }
                }

                user_free(&user);
            }
        }

        *body  = xs_json_dumps_pp(out, 4);
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/conversations") == 0) {
        /* TBD */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/notifications") == 0) {
        if (logged_in) {
            xs *l      = notify_list(&snac1, 0);
            xs *out    = xs_list_new();
            xs_list *p = l;
            xs_dict *v;

            while (xs_list_iter(&p, &v)) {
                xs *noti = notify_get(&snac1, v);

                if (noti == NULL)
                    continue;

                const char *type  = xs_dict_get(noti, "type");
                const char *objid = xs_dict_get(noti, "objid");
                xs *actor = NULL;
                xs *entry = NULL;

                if (!valid_status(object_get(xs_dict_get(noti, "actor"), &actor)))
                    continue;

                if (objid != NULL && !valid_status(object_get(objid, &entry)))
                    continue;

                if (is_hidden(&snac1, objid))
                    continue;

                /* convert the type */
                if (strcmp(type, "Like") == 0)
                    type = "favourite";
                else
                if (strcmp(type, "Announce") == 0)
                    type = "reblog";
                else
                if (strcmp(type, "Follow") == 0)
                    type = "follow";
                else
                if (strcmp(type, "Create") == 0)
                    type = "mention";
                else
                    continue;

                xs *mn = xs_dict_new();

                mn = xs_dict_append(mn, "type", type);

                xs *id = xs_replace(xs_dict_get(noti, "id"), ".", "");
                mn = xs_dict_append(mn, "id", id);

                mn = xs_dict_append(mn, "created_at", xs_dict_get(noti, "date"));

                xs *acct = mastoapi_account(actor);
                mn = xs_dict_append(mn, "account", acct);

                if (strcmp(type, "follow") != 0 && !xs_is_null(objid)) {
                    xs *st = mastoapi_status(&snac1, entry);
                    mn = xs_dict_append(mn, "status", st);
                }

                out = xs_list_append(out, mn);
            }

            *body  = xs_json_dumps_pp(out, 4);
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 401;
    }
    else
    if (strcmp(cmd, "/v1/filters") == 0) {
        /* snac will never have filters */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/favourites") == 0) {
        /* snac will never support a list of favourites */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/bookmarks") == 0) {
        /* snac does not support bookmarks */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/lists") == 0) {
        /* snac does not support lists */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/scheduled_statuses") == 0) {
        /* snac does not scheduled notes */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/follow_requests") == 0) {
        /* snac does not support optional follow confirmations */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/announcements") == 0) {
        /* snac has no announcements (yet?) */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/custom_emojis") == 0) {
        /* are you kidding me? */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/instance") == 0) {
        /* returns an instance object */
        xs *ins = xs_dict_new();
        const char *host = xs_dict_get(srv_config, "host");

        ins = xs_dict_append(ins, "uri",         host);
        ins = xs_dict_append(ins, "domain",      host);
        ins = xs_dict_append(ins, "title",       host);
        ins = xs_dict_append(ins, "version",     "4.0.0 (not true; really " USER_AGENT ")");
        ins = xs_dict_append(ins, "source_url",  WHAT_IS_SNAC_URL);
        ins = xs_dict_append(ins, "description", host);

        ins = xs_dict_append(ins, "short_description", host);

        xs *susie = xs_fmt("%s/susie.png", srv_baseurl);
        ins = xs_dict_append(ins, "thumbnail", susie);

        const char *v = xs_dict_get(srv_config, "admin_email");
        if (xs_is_null(v) || *v == '\0')
            v = "admin@localhost";

        ins = xs_dict_append(ins, "email", v);

        xs *l1 = xs_list_new();
        ins = xs_dict_append(ins, "rules", l1);

        l1 = xs_list_append(l1, "en");
        ins = xs_dict_append(ins, "languages", l1);

        xs *d1 = xs_dict_new();
        ins = xs_dict_append(ins, "urls", d1);

        xs *z = xs_number_new(0);
        d1 = xs_dict_append(d1, "user_count", z);
        d1 = xs_dict_append(d1, "status_count", z);
        d1 = xs_dict_append(d1, "domain_count", z);
        ins = xs_dict_append(ins, "stats", d1);

        xs *f = xs_val_new(XSTYPE_FALSE);
        ins = xs_dict_append(ins, "registrations", f);
        ins = xs_dict_append(ins, "approval_required", f);
        ins = xs_dict_append(ins, "invites_enabled", f);

        xs *cfg = xs_dict_new();

        {
            xs *d11 = xs_dict_new();
            xs *mc  = xs_number_new(100000);
            xs *mm  = xs_number_new(8);
            xs *cr  = xs_number_new(32);

            d11 = xs_dict_append(d11, "max_characters", mc);
            d11 = xs_dict_append(d11, "max_media_attachments", mm);
            d11 = xs_dict_append(d11, "characters_reserved_per_url", cr);

            cfg = xs_dict_append(cfg, "statuses", d11);
        }

        {
            xs *d11 = xs_dict_new();
            xs *mt  = xs_list_new();

            mt = xs_list_append(mt, "image/jpeg");
            mt = xs_list_append(mt, "image/png");
            mt = xs_list_append(mt, "image/gif");

            d11 = xs_dict_append(d11, "supported_mime_types", mt);

            d11 = xs_dict_append(d11, "image_size_limit", z);
            d11 = xs_dict_append(d11, "image_matrix_limit", z);
            d11 = xs_dict_append(d11, "video_size_limit", z);
            d11 = xs_dict_append(d11, "video_matrix_limit", z);
            d11 = xs_dict_append(d11, "video_frame_rate_limit", z);

            cfg = xs_dict_append(cfg, "media_attachments", d11);
        }

        ins = xs_dict_append(ins, "configuration", cfg);

        *body  = xs_json_dumps_pp(ins, 4);
        *ctype = "application/json";
        status = 200;
    }
    else
    if (xs_startswith(cmd, "/v1/statuses/")) {
        /* information about a status */
        xs *l = xs_split(cmd, "/");
        const char *id = xs_list_get(l, 3);
        const char *op = xs_list_get(l, 4);

        if (!xs_is_null(id)) {
            xs *msg = NULL;
            xs *out = NULL;

            /* skip the 'fake' part of the id */
            id = MID_TO_MD5(id);

            if (valid_status(timeline_get_by_md5(&snac1, id, &msg))) {
                if (op == NULL) {
                    if (!is_muted(&snac1, xs_dict_get(msg, "attributedTo"))) {
                        /* return the status itself */
                        out = mastoapi_status(&snac1, msg);
                    }
                }
                else
                if (strcmp(op, "context") == 0) {
                    /* return ancestors and children */
                    xs *anc = xs_list_new();
                    xs *des = xs_list_new();
                    xs_list *p;
                    xs_str *v;
                    char pid[64];

                    /* build the [grand]parent list, moving up */
                    strncpy(pid, id, sizeof(pid));

                    while (object_parent(pid, pid, sizeof(pid))) {
                        xs *m2 = NULL;

                        if (valid_status(timeline_get_by_md5(&snac1, pid, &m2))) {
                            xs *st = mastoapi_status(&snac1, m2);
                            anc = xs_list_insert(anc, 0, st);
                        }
                        else
                            break;
                    }

                    /* build the children list */
                    xs *children = object_children(xs_dict_get(msg, "id"));
                    p = children;

                    while (xs_list_iter(&p, &v)) {
                        xs *m2 = NULL;

                        if (valid_status(timeline_get_by_md5(&snac1, v, &m2))) {
                            xs *st = mastoapi_status(&snac1, m2);
                            des = xs_list_append(des, st);
                        }
                    }

                    out = xs_dict_new();
                    out = xs_dict_append(out, "ancestors",   anc);
                    out = xs_dict_append(out, "descendants", des);
                }
                else
                if (strcmp(op, "reblogged_by") == 0 ||
                    strcmp(op, "favourited_by") == 0) {
                    /* return the list of people who liked or boosted this */
                    out = xs_list_new();

                    xs *l = NULL;

                    if (op[0] == 'r')
                        l = object_announces(xs_dict_get(msg, "id"));
                    else
                        l = object_likes(xs_dict_get(msg, "id"));

                    xs_list *p = l;
                    xs_str *v;

                    while (xs_list_iter(&p, &v)) {
                        xs *actor2 = NULL;

                        if (valid_status(object_get_by_md5(v, &actor2))) {
                            xs *acct2 = mastoapi_account(actor2);

                            out = xs_list_append(out, acct2);
                        }
                    }
                }
            }
            else
                srv_debug(1, xs_fmt("mastoapi status: bad id %s", id));

            if (out != NULL) {
                *body  = xs_json_dumps_pp(out, 4);
                *ctype = "application/json";
                status = 200;
            }
        }
    }
    else
    if (strcmp(cmd, "/v1/filters") == 0) {
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/preferences") == 0) {
        *body  = xs_dup("{}");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/markers") == 0) {
        *body  = xs_dup("{}");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v1/followed_tags") == 0) {
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = 200;
    }
    else
    if (strcmp(cmd, "/v2/search") == 0) {
        const char *q      = xs_dict_get(args, "q");
        const char *type   = xs_dict_get(args, "type");
        const char *offset = xs_dict_get(args, "offset");

        xs *acl = xs_list_new();
        xs *stl = xs_list_new();
        xs *htl = xs_list_new();
        xs *res = xs_dict_new();

        if (xs_is_null(offset) || strcmp(offset, "0") == 0) {
            /* reply something only for offset 0; otherwise,
               apps like Tusky keep asking again and again */

            if (!xs_is_null(q) && strcmp(type, "accounts") == 0) {
                /* do a webfinger query */
                char *actor = NULL;
                char *user  = NULL;

                if (valid_status(webfinger_request(q, &actor, &user))) {
                    xs *actor_o = NULL;

                    if (valid_status(actor_request(&snac1, actor, &actor_o))) {
                        xs *acct = mastoapi_account(actor_o);

                        acl = xs_list_append(acl, acct);
                    }
                }
            }
        }

        res = xs_dict_append(res, "accounts", acl);
        res = xs_dict_append(res, "statuses", stl);
        res = xs_dict_append(res, "hashtags", htl);

        *body  = xs_json_dumps_pp(res, 4);
        *ctype = "application/json";
        status = 200;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac1);

    return status;
}


int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    srv_debug(1, xs_fmt("mastoapi_post_handler %s", q_path));
/*    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi post:\n%s\n", j);
    }*/

    int status    = 404;
    xs *args      = NULL;
    char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json"))
        args = xs_json_loads(payload);
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return 400;

/*    {
        xs *j = xs_json_dumps_pp(args, 4);
        printf("%s\n", j);
    }*/

    xs *cmd = xs_replace(q_path, "/api", "");

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    if (strcmp(cmd, "/v1/apps") == 0) {
        const char *name  = xs_dict_get(args, "client_name");
        const char *ruri  = xs_dict_get(args, "redirect_uris");
        const char *scope = xs_dict_get(args, "scope");

        if (xs_type(ruri) == XSTYPE_LIST)
            ruri = xs_dict_get(ruri, 0);

        if (name && ruri) {
            xs *app  = xs_dict_new();
            xs *id   = xs_replace_i(tid(0), ".", "");
            xs *cid  = random_str();
            xs *csec = random_str();
            xs *vkey = random_str();

            app = xs_dict_append(app, "name",          name);
            app = xs_dict_append(app, "redirect_uri",  ruri);
            app = xs_dict_append(app, "client_id",     cid);
            app = xs_dict_append(app, "client_secret", csec);
            app = xs_dict_append(app, "vapid_key",     vkey);
            app = xs_dict_append(app, "id",            id);

            *body  = xs_json_dumps_pp(app, 4);
            *ctype = "application/json";
            status = 200;

            app = xs_dict_append(app, "code", "");

            if (scope)
                app = xs_dict_append(app, "scope", scope);

            app_add(cid, app);

            srv_debug(0, xs_fmt("mastoapi apps: new app %s", cid));
        }
    }
    else
    if (strcmp(cmd, "/v1/statuses") == 0) {
        if (logged_in) {
            /* post a new Note */
/*    {
        xs *j = xs_json_dumps_pp(args, 4);
        printf("%s\n", j);
    }*/
            const char *content    = xs_dict_get(args, "status");
            const char *mid        = xs_dict_get(args, "in_reply_to_id");
            const char *visibility = xs_dict_get(args, "visibility");
            const char *summary    = xs_dict_get(args, "spoiler_text");
            const char *media_ids  = xs_dict_get(args, "media_ids");

            if (xs_is_null(media_ids))
                media_ids = xs_dict_get(args, "media_ids[]");

            xs *attach_list = xs_list_new();
            xs *irt         = NULL;

            /* is it a reply? */
            if (mid != NULL) {
                xs *r_msg = NULL;
                const char *md5 = MID_TO_MD5(mid);

                if (valid_status(object_get_by_md5(md5, &r_msg)))
                    irt = xs_dup(xs_dict_get(r_msg, "id"));
            }

            /* does it have attachments? */
            if (!xs_is_null(media_ids)) {
                xs *mi = NULL;

                if (xs_type(media_ids) == XSTYPE_LIST)
                    mi = xs_dup(media_ids);
                else {
                    mi = xs_list_new();
                    mi = xs_list_append(mi, media_ids);
                }

                xs_list *p = mi;
                xs_str *v;

                while (xs_list_iter(&p, &v)) {
                    xs *l    = xs_list_new();
                    xs *url  = xs_fmt("%s/s/%s", snac.actor, v);
                    xs *desc = static_get_meta(&snac, v);

                    l = xs_list_append(l, url);
                    l = xs_list_append(l, desc);

                    attach_list = xs_list_append(attach_list, l);
                }
            }

            /* prepare the message */
            xs *msg = msg_note(&snac, content, NULL, irt, attach_list,
                        strcmp(visibility, "public") == 0 ? 0 : 1);

            if (!xs_is_null(summary) && *summary) {
                xs *t = xs_val_new(XSTYPE_TRUE);
                msg = xs_dict_set(msg, "sensitive", t);
                msg = xs_dict_set(msg, "summary",   summary);
            }

            /* store */
            timeline_add(&snac, xs_dict_get(msg, "id"), msg);

            /* 'Create' message */
            xs *c_msg = msg_create(&snac, msg);
            enqueue_message(&snac, c_msg);

            timeline_touch(&snac);

            /* convert to a mastodon status as a response code */
            xs *st = mastoapi_status(&snac, msg);

            *body  = xs_json_dumps_pp(st, 4);
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 401;
    }
    else
    if (xs_startswith(cmd, "/v1/statuses")) {
        if (logged_in) {
            /* operations on a status */
            xs *l = xs_split(cmd, "/");
            const char *mid = xs_list_get(l, 3);
            const char *op  = xs_list_get(l, 4);

            if (!xs_is_null(mid)) {
                xs *msg = NULL;
                xs *out = NULL;

                /* skip the 'fake' part of the id */
                mid = MID_TO_MD5(mid);

                if (valid_status(timeline_get_by_md5(&snac, mid, &msg))) {
                    char *id = xs_dict_get(msg, "id");

                    if (op == NULL) {
                        /* no operation (?) */
                    }
                    else
                    if (strcmp(op, "favourite") == 0) {
                        xs *n_msg = msg_admiration(&snac, id, "Like");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);
                            timeline_admire(&snac, xs_dict_get(n_msg, "object"), snac.actor, 1);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "unfavourite") == 0) {
                        /* snac does not support Undo+Like */
                    }
                    else
                    if (strcmp(op, "reblog") == 0) {
                        xs *n_msg = msg_admiration(&snac, id, "Announce");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);
                            timeline_admire(&snac, xs_dict_get(n_msg, "object"), snac.actor, 0);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "unreblog") == 0) {
                        /* snac does not support Undo+Announce */
                    }
                    else
                    if (strcmp(op, "bookmark") == 0) {
                        /* snac does not support bookmarks */
                    }
                    else
                    if (strcmp(op, "unbookmark") == 0) {
                        /* snac does not support bookmarks */
                    }
                    else
                    if (strcmp(op, "pin") == 0) {
                        /* snac does not support pinning */
                    }
                    else
                    if (strcmp(op, "unpin") == 0) {
                        /* snac does not support pinning */
                    }
                    else
                    if (strcmp(op, "mute") == 0) {
                        /* Mastodon's mute is snac's hide */
                    }
                    else
                    if (strcmp(op, "unmute") == 0) {
                        /* Mastodon's unmute is snac's unhide */
                    }
                }

                if (out != NULL) {
                    *body  = xs_json_dumps_pp(out, 4);
                    *ctype = "application/json";
                    status = 200;
                }
            }
        }
        else
            status = 401;
    }
    else
    if (strcmp(cmd, "/v1/notifications/clear") == 0) {
        if (logged_in) {
            notify_clear(&snac);
            timeline_touch(&snac);

            *body  = xs_dup("{}");
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 401;
    }
    else
    if (strcmp(cmd, "/v1/push/subscription") == 0) {
        /* I don't know what I'm doing */
        if (logged_in) {
            char *v;

            xs *wpush = xs_dict_new();

            wpush = xs_dict_append(wpush, "id", "1");

            v = xs_dict_get(args, "data");
            v = xs_dict_get(v, "alerts");
            wpush = xs_dict_append(wpush, "alerts", v);

            v = xs_dict_get(args, "subscription");
            v = xs_dict_get(v, "endpoint");
            wpush = xs_dict_append(wpush, "endpoint", v);

            xs *server_key = random_str();
            wpush = xs_dict_append(wpush, "server_key", server_key);

            *body  = xs_json_dumps_pp(wpush, 4);
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 401;
    }
    else
    if (strcmp(cmd, "/v1/media") == 0 || strcmp(cmd, "/v2/media") == 0) {
        if (logged_in) {
/*    {
        xs *j = xs_json_dumps_pp(args, 4);
        printf("%s\n", j);
    }*/
            const xs_list *file = xs_dict_get(args, "file");
            const char *desc    = xs_dict_get(args, "description");

            if (xs_is_null(desc))
                desc = "";

            status = 400;

            if (xs_type(file) == XSTYPE_LIST) {
                const char *fn = xs_list_get(file, 0);

                if (*fn != '\0') {
                    char *ext = strrchr(fn, '.');
                    xs *hash  = xs_md5_hex(fn, strlen(fn));
                    xs *id    = xs_fmt("%s%s", hash, ext);
                    xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                    int fo    = xs_number_get(xs_list_get(file, 1));
                    int fs    = xs_number_get(xs_list_get(file, 2));

                    /* store */
                    static_put(&snac, id, payload + fo, fs);
                    static_put_meta(&snac, id, desc);

                    /* prepare a response */
                    xs *rsp = xs_dict_new();

                    rsp = xs_dict_append(rsp, "id",          id);
                    rsp = xs_dict_append(rsp, "type",        "image");
                    rsp = xs_dict_append(rsp, "url",         url);
                    rsp = xs_dict_append(rsp, "preview_url", url);
                    rsp = xs_dict_append(rsp, "remote_url",  url);
                    rsp = xs_dict_append(rsp, "description", desc);

                    *body  = xs_json_dumps_pp(rsp, 4);
                    *ctype = "application/json";
                    status = 200;
                }
            }
        }
        else
            status = 401;
    }
    else
    if (xs_startswith(cmd, "/v1/accounts")) {
        if (logged_in) {
            /* account-related information */
            xs *l = xs_split(cmd, "/");
            const char *md5 = xs_list_get(l, 3);
            const char *opt = xs_list_get(l, 4);
            xs *rsp = NULL;

            if (!xs_is_null(md5) && *md5) {
                xs *actor_o = NULL;

                if (xs_is_null(opt)) {
                    /* ? */
                }
                else
                if (strcmp(opt, "follow") == 0) {
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        xs *msg = msg_follow(&snac, actor);

                        if (msg != NULL) {
                            /* reload the actor from the message, in may be different */
                            actor = xs_dict_get(msg, "object");

                            following_add(&snac, actor, msg);

                            enqueue_output_by_actor(&snac, msg, actor, 0);

                            rsp = mastoapi_relationship(&snac, md5);
                        }
                    }
                }
                else
                if (strcmp(opt, "unfollow") == 0) {
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        /* get the following object */
                        xs *object = NULL;

                        if (valid_status(following_get(&snac, actor, &object))) {
                            xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                            following_del(&snac, actor);

                            enqueue_output_by_actor(&snac, msg, actor, 0);

                            rsp = mastoapi_relationship(&snac, md5);
                        }
                    }
                }
            }

            if (rsp != NULL) {
                *body  = xs_json_dumps_pp(rsp, 4);
                *ctype = "application/json";
                status = 200;
            }
        }
        else
            status = 401;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    return status;
}


int mastoapi_put_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    srv_debug(1, xs_fmt("mastoapi_post_handler %s", q_path));
/*    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi put:\n%s\n", j);
    }*/

    int status    = 404;
    xs *args      = NULL;
    char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json"))
        args = xs_json_loads(payload);
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return 400;

    xs *cmd = xs_replace(q_path, "/api", "");

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    if (xs_startswith(cmd, "/v1/media") || xs_startswith(cmd, "/v2/media")) {
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *stid = xs_list_get(l, 3);

            if (!xs_is_null(stid)) {
                const char *desc = xs_dict_get(args, "description");

                /* set the image metadata */
                static_put_meta(&snac, stid, desc);

                /* prepare a response */
                xs *rsp = xs_dict_new();
                xs *url = xs_fmt("%s/s/%s", snac.actor, stid);

                rsp = xs_dict_append(rsp, "id",          stid);
                rsp = xs_dict_append(rsp, "type",        "image");
                rsp = xs_dict_append(rsp, "url",         url);
                rsp = xs_dict_append(rsp, "preview_url", url);
                rsp = xs_dict_append(rsp, "remote_url",  url);
                rsp = xs_dict_append(rsp, "description", desc);

                *body  = xs_json_dumps_pp(rsp, 4);
                *ctype = "application/json";
                status = 200;
            }
        }
        else
            status = 401;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    return status;
}


#endif /* #ifndef NO_MASTODON_API */
