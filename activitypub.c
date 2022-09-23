/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"

#include "snac.h"

const char *public_address = "https:/" "/www.w3.org/ns/activitystreams#Public";

int activitypub_request(snac *snac, char *url, d_char **data)
/* request an object */
{
    int status;
    xs *response = NULL;
    xs *payload = NULL;
    int p_size;
    char *ctype;

    /* check if it's an url for this same site */
    /* ... */

    /* get from the net */
    response = http_signed_request(snac, "GET", url,
        NULL, NULL, 0, &status, &payload, &p_size);

    if (valid_status(status)) {
        if (dbglevel >= 3) {
            xs *j = xs_json_dumps_pp(response, 4);
            fprintf(stderr, "%s\n", j);
        }

        /* ensure it's ActivityPub data */
        ctype = xs_dict_get(response, "content-type");

        if (xs_str_in(ctype, "application/activity+json") != -1)
            *data = xs_json_loads(payload);
        else
            status = 500;
    }

    if (!valid_status(status))
        *data = NULL;

    return status;
}


int actor_request(snac *snac, char *actor, d_char **data)
/* request an actor */
{
    int status, status2;
    xs *payload = NULL;

    /* get from disk first */
    status = actor_get(snac, actor, data);

    if (status == 200)
        return status;

    /* actor data non-existent or stale: get from the net */
    status2 = activitypub_request(snac, actor, &payload);

    if (valid_status(status2)) {
        /* renew data */
        xs *j = xs_json_dumps_pp(payload, 4);
        status = actor_add(snac, actor, j);

        *data   = payload;
        payload = NULL;
    }

    return status;
}
