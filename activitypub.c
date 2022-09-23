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
    xs *j_msg = xs_json_dumps_pp(msg, 4);

    response = http_signed_request(snac, "POST", inbox,
        NULL, j_msg, strlen(j_msg), &status, payload, p_size);

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

    snac_log(snac, xs_fmt("send_to_actor %s %d", actor, status));

    return status;
}


void process_queue(snac *snac)
/* processes the queue */
{
    xs *list;
    char *p, *fn;
    int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

    list = queue(snac);

    p = list;
    while (xs_list_iter(&p, &fn)) {
        xs *q_item = dequeue(snac, fn);
        char *type;

        if ((type = xs_dict_get(q_item, "type")) == NULL)
            type = "output";

        if (strcmp(type, "output") == 0) {
            int status;
            char *actor = xs_dict_get(q_item, "actor");
            char *msg   = xs_dict_get(q_item, "object");
            int retries = xs_number_get(xs_dict_get(q_item, "retries"));

            /* deliver */
            status = send_to_actor(snac, actor, msg, NULL, 0);

            if (!valid_status(status)) {
                /* error sending; reenqueue? */
                if (retries > queue_retry_max)
                    snac_log(snac, xs_fmt("process_queue giving up %s %d", actor, status));
                else {
                    /* reenqueue */
                    enqueue_output(snac, actor, msg, retries + 1);
                    snac_log(snac, xs_fmt("process_queue requeue %s %d", actor, retries + 1));
                }
            }
        }
    }
}


int activitypub_post_handler(d_char *req, char *q_path,
                             d_char *payload, int p_size,
                             char **body, int *b_size, char **ctype)
/* processes an input message */
{
    int status = 200;
    char *i_ctype = xs_dict_get(req, "content-type");
    snac snac;

    if (xs_str_in(i_ctype, "application/activity+json") == -1 &&
        xs_str_in(i_ctype, "application/ld+json") == -1)
        return 0;

    xs *l = xs_split_n(q_path, "/", 2);
    char *uid;

    if (xs_list_len(l) != 3 || strcmp(xs_list_get(l, 2), "inbox") != 0) {
        /* strange q_path */
        srv_log(xs_fmt("activitypub_post_handler unsupported path %s", q_path));
        return 404;
    }

    uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("activitypub_post_handler bad user %s", uid));
        return 404;
    }

    user_free(&snac);

    return status;
}
