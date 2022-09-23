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
        status = actor_add(snac, actor, payload);

        *data   = payload;
        payload = NULL;
    }

    return status;
}


int send_to_inbox(snac *snac, char *inbox, char *msg, d_char **payload, int *p_size)
/* sends a message to an Inbox */
{
    int status;
    d_char *response;

    response = http_signed_request(snac, "POST", inbox,
        NULL, msg, strlen(msg), &status, payload, p_size);

    free(response);

    return status;
}


int send_to_actor(snac *snac, char *actor, char *msg, d_char **payload, int *p_size)
/* sends a message to an actor */
{
    int status;
    xs *data = NULL;

    /* resolve the actor first */
    status = actor_request(snac, actor, &data);

    if (valid_status(status)) {
        char *inbox = xs_dict_get(data, "inbox");

        if (inbox != NULL)
            status = send_to_inbox(snac, inbox, msg, payload, p_size);
        else
            status = 400;
    }

    return status;
}
