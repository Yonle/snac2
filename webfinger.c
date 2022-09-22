/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"

#include "snac.h"

void webfinger_request(char *qs, int *status, char **actor, char **user)
/* queries the webfinger for qs and fills the required fields */
{
    xs *payload = NULL;
    int p_size = 0;
    xs *url = NULL;
    xs *headers = xs_dict_new();

    if (xs_startswith(qs, "https:/" "/")) {
        /* actor query: pick the host */
        xs *s = xs_replace(qs, "https:/" "/", "");
        xs *l = xs_split_n(s, "/", 1);

        url = xs_fmt("https:/" "/%s/.well-known/webfinger?resource=%s",
                xs_list_get(l, 0), qs);
    }
    else {
        /* it's a user */
        xs *s = xs_dup(qs);
        xs *l;

        if (xs_startswith(s, "@"))
            s = xs_crop(s, 1, 0);

        l = xs_split_n(s, "@", 1);

        if (xs_list_len(l) == 2) {
            url = xs_fmt("https:/" "/%s/.well-known/webfinger?resource:acct:%s",
                xs_list_get(l, 1), qs);
        }
    }

    if (url == NULL) {
        *status = 400;
        return;
    }

    headers = xs_dict_append(headers, "accept", "application/json");

    xs_http_request("GET", url, headers, NULL, 0, status, &payload, &p_size);
}


void webfinger_get_handler(d_char *req, char *q_path, int *status,
                           char **body, int *b_size, char **ctype)
/* serves webfinger queries */
{
    if (strcmp(q_path, "/.well-known/webfinger") != 0)
        return;

    char *q_vars   = xs_dict_get(req, "q_vars");
    char *resource = xs_dict_get(q_vars, "resource");

    if (resource == NULL) {
        *status = 400;
        return;
    }

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
            an = xs_crop(an, 1, 0);

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

        *status = 200;
        *body   = j;
        *ctype  = "application/json";
    }
}
