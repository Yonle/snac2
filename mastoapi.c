/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

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
"<input type=\"submit\" value=\"OK\">\n"
"</form><p>%s</p></body>\n"
"";

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth get:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    srv_debug(0, xs_fmt("oauth_get_handler %s", q_path));

    if (strcmp(cmd, "/authorize") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *rtype = xs_dict_get(msg, "response_type");

        status = 400;

        if (cid && ruri && rtype && strcmp(rtype, "code") == 0) {
            xs *app = app_get(cid);

            if (app != NULL) {
                const char *host = xs_dict_get(srv_config, "host");

                *body  = xs_fmt(login_page, host, "", host, ruri, cid, USER_AGENT);
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

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth post:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "p_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    srv_debug(0, xs_fmt("oauth_post_handler %s", q_path));

    if (strcmp(cmd, "/x-snac-login") == 0) {
        const char *login  = xs_dict_get(msg, "login");
        const char *passwd = xs_dict_get(msg, "passwd");
        const char *redir  = xs_dict_get(msg, "redir");
        const char *cid    = xs_dict_get(msg, "cid");

        const char *host = xs_dict_get(srv_config, "host");

        /* by default, generate another login form with an error */
        *body  = xs_fmt(login_page, host, "LOGIN INCORRECT", host, redir, cid, USER_AGENT);
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

                    srv_debug(0, xs_fmt("oauth x-snac-login: success, redirect to %s", *body));

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
                    srv_debug(0, xs_fmt("oauth x-snac-login: login '%s' incorrect", login));

                user_free(&snac);
            }
            else
                srv_debug(0, xs_fmt("oauth x-snac-login: bad user '%s'", login));
        }
        else
            srv_debug(0, xs_fmt("oauth x-snac-login: invalid or unset arguments"));
    }
    else
    if (strcmp(cmd, "/token") == 0) {
        const char *gtype = xs_dict_get(msg, "grant_type");
        const char *code  = xs_dict_get(msg, "code");
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");

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
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *tokid = xs_dict_get(msg, "token");

        if (cid && csec && tokid) {
            xs *token = token_get(tokid);

            *body  = xs_str_new("{}");
            *ctype = "application/json";

            if (token == NULL || strcmp(csec, xs_dict_get(token, "client_secret")) != 0) {
                srv_debug(0, xs_fmt("oauth revoke: bad secret for token %s", tokid));
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


int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    srv_debug(0, xs_fmt("mastoapi_get_handler %s", q_path));
    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi get:\n%s\n", j);
    }

    int status    = 404;
    xs_dict *args = xs_dict_get(req, "q_vars");
    xs *cmd       = xs_replace(q_path, "/api/v1", "");
    char *v;

    snac snac = {0};
    int logged_in = 0;

    /* if there is an authorization field, try to validate it */
    if (!xs_is_null(v = xs_dict_get(req, "authorization")) && xs_startswith(v, "Bearer ")) {
        xs *tokid = xs_replace(v, "Bearer ", "");
        xs *token = token_get(tokid);

        if (token != NULL) {
            const char *uid = xs_dict_get(token, "uid");

            if (!xs_is_null(uid) && user_open(&snac, uid)) {
                logged_in = 1;
                srv_debug(0, xs_fmt("mastoapi auth: valid token for user %s", uid));
            }
            else
                srv_log(xs_fmt("mastoapi auth: corrupted token %s", tokid));
        }
        else
            srv_log(xs_fmt("mastoapi auth: invalid token %s", tokid));
    }

    if (strcmp(cmd, "/accounts/verify_credentials") == 0) {
        if (logged_in) {
            xs *acct = xs_dict_new();

            acct = xs_dict_append(acct, "id",           xs_dict_get(snac.config, "uid"));
            acct = xs_dict_append(acct, "username",     xs_dict_get(snac.config, "uid"));
            acct = xs_dict_append(acct, "acct",         xs_dict_get(snac.config, "uid"));
            acct = xs_dict_append(acct, "display_name", xs_dict_get(snac.config, "name"));
            acct = xs_dict_append(acct, "created_at",   xs_dict_get(snac.config, "published"));
            acct = xs_dict_append(acct, "note",         xs_dict_get(snac.config, "bio"));
            acct = xs_dict_append(acct, "url",          snac.actor);

            xs *avatar = NULL;
            char *av   = xs_dict_get(snac.config, "avatar");

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
    if (strcmp(cmd, "/timelines/home") == 0) {
        /* the private timeline */
        if (logged_in) {
            const char *max_id   = xs_dict_get(args, "max_id");
            const char *since_id = xs_dict_get(args, "since_id");
//            const char *min_id   = xs_dict_get(args, "min_id");
            const char *limit_s  = xs_dict_get(args, "limit");
            int limit = 20;
            int cnt = 0;

            if (!xs_is_null(limit_s))
                limit = atoi(limit_s);

            xs *timeline = timeline_simple_list(&snac, "private", 0, XS_ALL);

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

                /* get the entry */
                if (!valid_status(timeline_get_by_md5(&snac, v, &msg)))
                    continue;

                /* discard non-Notes */
                if (strcmp(xs_dict_get(msg, "type"), "Note") != 0)
                    continue;

                xs *actor = NULL;
                actor_get(&snac, xs_dict_get(msg, "attributedTo"), &actor);

                /* if the author is not here, discard */
                if (actor == NULL)
                    continue;

                /** shave the yak converting an ActivityPub Note to a Mastodon status **/

                xs *acct = xs_dict_new();

                const char *display_name = xs_dict_get(actor, "name");
                if (xs_is_null(display_name) || *display_name == '\0')
                    display_name = xs_dict_get(actor, "preferredUsername");

                const char *id = xs_dict_get(actor, "id");
                xs *acct_md5 = xs_md5_hex(id, strlen(id));
                acct = xs_dict_append(acct, "id",           acct_md5);
                acct = xs_dict_append(acct, "username",     xs_dict_get(actor, "preferredUsername"));
                acct = xs_dict_append(acct, "acct",         xs_dict_get(actor, "preferredUsername"));
                acct = xs_dict_append(acct, "display_name", display_name);
                acct = xs_dict_append(acct, "created_at",   xs_dict_get(actor, "published"));
                acct = xs_dict_append(acct, "note",         xs_dict_get(actor, "summary"));
                acct = xs_dict_append(acct, "url",          id);

                xs *avatar  = NULL;
                xs_dict *av = xs_dict_get(actor, "icon");

                if (xs_type(av) == XSTYPE_DICT)
                    avatar = xs_dup(xs_dict_get(av, "url"));
                else
                    avatar = xs_fmt("%s/susie.png", srv_baseurl);

                acct = xs_dict_append(acct, "avatar", avatar);

                xs *f   = xs_val_new(XSTYPE_FALSE);
                xs *t   = xs_val_new(XSTYPE_TRUE);
                xs *n   = xs_val_new(XSTYPE_NULL);
                xs *el  = xs_list_new();
                xs *idx = NULL;
                xs *ixc = NULL;

                char *tmp;
                id = xs_dict_get(msg, "id");

                xs *st = xs_dict_new();

                st = xs_dict_append(st, "id",           v);
                st = xs_dict_append(st, "uri",          id);
                st = xs_dict_append(st, "url",          id);
                st = xs_dict_append(st, "created_at",   xs_dict_get(msg, "published"));
                st = xs_dict_append(st, "account",      acct);
                st = xs_dict_append(st, "content",      xs_dict_get(msg, "content"));

                st = xs_dict_append(st, "visibility",
                    is_msg_public(&snac, msg) ? "public" : "private");

                tmp = xs_dict_get(msg, "sensitive");
                if (xs_is_null(tmp))
                    tmp = f;

                st = xs_dict_append(st, "sensitive",    tmp);

                tmp = xs_dict_get(msg, "summary");
                if (xs_is_null(tmp))
                    tmp = "";

                st = xs_dict_append(st, "spoiler_text", tmp);

                st = xs_dict_append(st, "media_attachments", el);
                st = xs_dict_append(st, "mentions",          el);
                st = xs_dict_append(st, "tags",              el);
                st = xs_dict_append(st, "emojis",            el);

                xs_free(idx);
                xs_free(ixc);
                idx = object_likes(id);
                ixc = xs_number_new(xs_list_len(idx));

                st = xs_dict_append(st, "favourites_count", ixc);
                st = xs_dict_append(st, "favourited",
                    xs_list_in(idx, snac.md5) != -1 ? t : f);

                xs_free(idx);
                xs_free(ixc);
                idx = object_announces(id);
                ixc = xs_number_new(xs_list_len(idx));

                st = xs_dict_append(st, "reblogs_count", ixc);
                st = xs_dict_append(st, "reblogged",
                    xs_list_in(idx, snac.md5) != -1 ? t : f);

                xs_free(idx);
                xs_free(ixc);
                idx = object_children(id);
                ixc = xs_number_new(xs_list_len(idx));

                st = xs_dict_append(st, "replies_count", ixc);

                tmp = xs_dict_get(msg, "inReplyTo");
                if (xs_is_null(tmp)) {
                    st = xs_dict_append(st, "in_reply_to_id",         n);
                    st = xs_dict_append(st, "in_reply_to_account_id", n);
                }
                else {
                    xs *irt_md5 = xs_md5_hex(tmp, strlen(tmp));
                    st = xs_dict_append(st, "in_reply_to_id", irt_md5);

                    xs *irto = NULL;
                    char *at = NULL;
                    if (valid_status(object_get(tmp, &irto)) &&
                        !xs_is_null(at = xs_dict_get(irto, "attributedTo"))) {
                        xs *at_md5 = xs_md5_hex(at, strlen(at));
                        st = xs_dict_append(st, "in_reply_to_account_id", at_md5);
                    }
                    else
                        st = xs_dict_append(st, "in_reply_to_account_id", n);
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

                out = xs_list_append(out, st);

                cnt++;
            }

            *body  = xs_json_dumps_pp(out, 4);
            *ctype = "application/json";
            status = 200;
        }
        else {
            status = 401; // unauthorized
        }
    }
    else
    if (strcmp(cmd, "/timelines/public") == 0) {
        /* the public timeline */
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    return status;
}


int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    srv_debug(0, xs_fmt("mastoapi_post_handler %s", q_path));
    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi post:\n%s\n", j);
    }

    int status    = 404;
    xs *msg       = NULL;
    char *i_ctype = xs_dict_get(req, "content-type");

    if (xs_startswith(i_ctype, "application/json"))
        msg = xs_json_loads(payload);
    else
        msg = xs_dup(xs_dict_get(req, "p_vars"));

    if (msg == NULL)
        return 400;

    {
        xs *j = xs_json_dumps_pp(msg, 4);
        printf("%s\n", j);
    }

    xs *cmd = xs_replace(q_path, "/api/v1", "");

    if (strcmp(cmd, "/apps") == 0) {
        const char *name  = xs_dict_get(msg, "client_name");
        const char *ruri  = xs_dict_get(msg, "redirect_uris");
        const char *scope = xs_dict_get(msg, "scope");

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

    return status;
}
