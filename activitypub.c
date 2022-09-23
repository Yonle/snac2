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
    xs *payload;
    int p_size;

    /* check if it's an url for this same site */
    /* ... */

    /* get from the net */
    response = http_signed_request(snac, "GET", url,
        NULL, NULL, 0, &status, &payload, &p_size);

    {
        xs *j = xs_json_loads(response);
        printf("%s\n", j);
    }

    if (valid_status(status)) {
        *data = xs_json_loads(payload);
    }

    return status;
}


#if 0
int actor_request(snac *snac, char *actor, d_char **data)
/* request an actor */
{
    int status;
    xs *response = NULL;
    xs *payload;
    int p_size;

    /* get from disk first */
    status = actor_get(snac, actor, data);

    if (status == 200)
        return;

    /* get from the net */
    response = http_signed_request(snac, "GET", actor,
        NULL, NULL, 0, &status, &payload, &p_size);

//    response = http_signed_request(&snac, "GET", "https://mastodon.social/users/VictorMoral",
//        headers, NULL, 0, &status, &payload, &p_size);

    return status;
}
#endif
