/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"

#include "snac.h"

int webfinger_request(const char *qs, char **actor, char **user)
/* queries the webfinger for qs and fills the required fields */
{
    int status;
    xs *payload = NULL;
    int p_size = 0;
    xs *headers = xs_dict_new();
    xs *l = NULL;
    d_char *host = NULL;
    xs *resource = NULL;

    if (xs_startswith(qs, "https:/" "/")) {
        /* actor query: pick the host */
        xs *s = xs_replace(qs, "https:/" "/", "");

        l = xs_split_n(s, "/", 1);

        host     = xs_list_get(l, 0);
        resource = xs_dup(qs);
    }
    else {
        /* it's a user */
        xs *s = xs_dup(qs);

        if (xs_startswith(s, "@"))
            s = xs_crop_i(s, 1, 0);

        l = xs_split_n(s, "@", 1);

        if (xs_list_len(l) == 2) {
            host     = xs_list_get(l, 1);
            resource = xs_fmt("acct:%s", s);
        }
    }

    if (host == NULL || resource == NULL)
        return 400;

    headers = xs_dict_append(headers, "accept", "application/json");

    /* is it a query about one of us? */
    if (strcmp(host, xs_dict_get(srv_config, "host")) == 0) {
        /* route internally */
        xs *req    = xs_dict_new();
        xs *q_vars = xs_dict_new();
        char *ctype;

        q_vars = xs_dict_append(q_vars, "resource", resource);
        req    = xs_dict_append(req, "q_vars", q_vars);

        status = webfinger_get_handler(req, "/.well-known/webfinger",
                                       &payload, &p_size, &ctype);
    }
    else {
        xs *url = xs_fmt("https:/" "/%s/.well-known/webfinger?resource=%s", host, resource);

        xs_http_request("GET", url, headers, NULL, 0, &status, &payload, &p_size, 0);
    }

    if (valid_status(status)) {
        xs *obj = xs_json_loads(payload);

        if (user != NULL) {
            char *subject = xs_dict_get(obj, "subject");

            if (subject)
                *user = xs_replace(subject, "acct:", "");
        }

        if (actor != NULL) {
            char *list = xs_dict_get(obj, "links");
            char *v;

            while (xs_list_iter(&list, &v)) {
                if (xs_type(v) == XSTYPE_DICT) {
                    char *type = xs_dict_get(v, "type");

                    if (type && strcmp(type, "application/activity+json") == 0) {
                        *actor = xs_dup(xs_dict_get(v, "href"));
                        break;
                    }
                }
            }
        }
    }

    return status;
}


int webfinger_get_handler(d_char *req, char *q_path,
                           char **body, int *b_size, char **ctype)
/* serves webfinger queries */
{
    int status;

    if (strcmp(q_path, "/.well-known/webfinger") != 0)
        return 0;

    char *q_vars   = xs_dict_get(req, "q_vars");
    char *resource = xs_dict_get(q_vars, "resource");

    if (resource == NULL)
        return 400;

    snac snac;
    int found = 0;

    if (xs_startswith(resource, "https:/" "/")) {
        /* actor search: find a user with this actor */
        xs *list = user_list();
        char *p, *uid;

        p = list;
        while (xs_list_iter(&p, &uid)) {
            if (user_open(&snac, uid)) {
                if (strcmp(snac.actor, resource) == 0) {
                    found = 1;
                    break;
                }

                user_free(&snac);
            }
        }
    }
    else
    if (xs_startswith(resource, "acct:")) {
        /* it's an account name */
        xs *an = xs_replace(resource, "acct:", "");
        xs *l = NULL;

        /* strip a possible leading @ */
        if (xs_startswith(an, "@"))
            an = xs_crop_i(an, 1, 0);

        l = xs_split_n(an, "@", 1);

        if (xs_list_len(l) == 2) {
            char *uid  = xs_list_get(l, 0);
            char *host = xs_list_get(l, 1);

            if (strcmp(host, xs_dict_get(srv_config, "host")) == 0)
                found = user_open(&snac, uid);
        }
    }

    if (found) {
        /* build the object */
        xs *acct;
        xs *aaj   = xs_dict_new();
        xs *links = xs_list_new();
        xs *obj   = xs_dict_new();
        d_char *j;

        acct = xs_fmt("acct:%s@%s",
            xs_dict_get(snac.config, "uid"), xs_dict_get(srv_config, "host"));

        aaj = xs_dict_append(aaj, "rel",  "self");
        aaj = xs_dict_append(aaj, "type", "application/activity+json");
        aaj = xs_dict_append(aaj, "href", snac.actor);

        links = xs_list_append(links, aaj);

        obj = xs_dict_append(obj, "subject", acct);
        obj = xs_dict_append(obj, "links",   links);

        j = xs_json_dumps_pp(obj, 4);

        user_free(&snac);

        status = 200;
        *body  = j;
        *ctype = "application/json";
    }
    else
        status = 404;

    return status;
}
