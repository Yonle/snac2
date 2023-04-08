/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
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


int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    if (strcmp(cmd, "/authorize") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *rtype = xs_dict_get(msg, "response_type");
        const char *scope = xs_dict_get(msg, "scope");

        if (cid && ruri && rtype && strcmp(rtype, "code") == 0) {
            /* redirect to an identification page */
            status = 303;
//            *body  = xs_fmt("%s/test1/admin?redir=%s", srv_baseurl, ruri);
            *body  = xs_fmt("%s/test1/admin", srv_baseurl);
        }
        else
            status = 400;
    }

    return status;
}


int oauth_post_handler(const xs_dict *req, const char *q_path,
                      const char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "p_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    printf("oauth: %s\n", q_path);

    if (strcmp(cmd, "/token") == 0) {
        const char *gtype = xs_dict_get(msg, "grant_type");
        const char *code  = xs_dict_get(msg, "code");
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *scope = xs_dict_get(msg, "scope");

        if (gtype && code && cid && csec && ruri) {
            xs *rsp   = xs_dict_new();
            xs *cat   = xs_number_new(time(NULL));
            xs *token = random_str();

            rsp = xs_dict_append(rsp, "access_token", token);
            rsp = xs_dict_append(rsp, "token_type",   "Bearer");
            rsp = xs_dict_append(rsp, "scope",        scope);
            rsp = xs_dict_append(rsp, "created_at",   cat);

            *body  = xs_json_dumps_pp(rsp, 4);
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 400;
    }
    else
    if (strcmp(cmd, "/revoke") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *token = xs_dict_get(msg, "token");

        if (cid && csec && token) {
            *body  = xs_str_new("{}");
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 400;
    }

    return status;
}


int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                      const char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

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
        xs *j = xs_json_dumps_pp(req, 4);
        printf("%s\n", j);
    }
    {
        xs *j = xs_json_dumps_pp(msg, 4);
        printf("%s\n", j);
    }

    xs *cmd = xs_replace(q_path, "/api/v1", "");

    if (strcmp(cmd, "/apps") == 0) {
        const char *name = xs_dict_get(msg, "client_name");
        const char *ruri = xs_dict_get(msg, "redirect_uris");

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
        }
    }

    return status;
}
