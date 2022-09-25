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


/** messages **/

d_char *msg_base(snac *snac, char *type, char *id, char *actor, char *date)
/* creates a base ActivityPub message */
{
    d_char *msg = xs_dict_new();

    msg = xs_dict_append(msg, "@context", "https:/" "/www.w3.org/ns/activitystreams");
    msg = xs_dict_append(msg, "type",     type);

    if (id != NULL)
        msg = xs_dict_append(msg, "id", id);

    if (actor != NULL)
        msg = xs_dict_append(msg, "actor", actor);

    if (date != NULL) {
        xs *published = xs_utc_time("%Y-%m-%dT%H:%M:%SZ");
        msg = xs_dict_append(msg, "published", published);
    }

    return msg;
}


d_char *msg_collection(snac *snac, char *id)
/* creates an empty OrderedCollection message */
{
    d_char *msg = msg_base(snac, "OrderedCollection", id, NULL, NULL);
    xs *ol = xs_list_new();
    xs *nz = xs_number_new(0);

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "orderedItems", ol);
    msg = xs_dict_append(msg, "totalItems",   nz);

    return msg;
}


d_char *msg_update(snac *snac, char *object)
/* creates an Update message */
{
    xs *id = xs_fmt("%s/Update", xs_dict_get(object, "id"));
    d_char *msg = msg_base(snac, "Update", id, snac->actor, "");

    msg = xs_dict_append(msg, "to",     public_address);
    msg = xs_dict_append(msg, "object", object);

    return msg;
}


/** queues **/

void process_message(snac *snac, char *msg, char *req)
/* processes an ActivityPub message from the input queue */
{
    /* they exist, were checked previously */
    char *actor = xs_dict_get(msg, "actor");
    char *type  = xs_dict_get(msg, "type");

    /* check the signature */
    /* ... */

    if (strcmp(type, "Follow") == 0) {
    }
    else
    if (strcmp(type, "Undo") == 0) {
    }
    else
    if (strcmp(type, "Create") == 0) {
    }
    else
    if (strcmp(type, "Accept") == 0) {
    }
    else
    if (strcmp(type, "Like") == 0 || strcmp(type, "Announce") == 0) {
    }
    else
    if (strcmp(type, "Update") == 0) {
    }
    else
    if (strcmp(type, "Delete") == 0) {
    }
    else
        snac_debug(snac, 1, xs_fmt("process_message type '%s' ignored", type));
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
                    enqueue_output(snac, msg, actor, retries + 1);
                    snac_log(snac, xs_fmt("process_queue requeue %s %d", actor, retries + 1));
                }
            }
        }
        else
        if (strcmp(type, "input") == 0) {
            /* process the message */
            char *msg = xs_dict_get(q_item, "object");
            char *req = xs_dict_get(q_item, "req");

            process_message(snac, msg, req);
        }
    }
}


int activitypub_get_handler(d_char *req, char *q_path,
                            char **body, int *b_size, char **ctype)
{
    int status = 200;
    char *accept = xs_dict_get(req, "accept");
    snac snac;
    xs *msg = NULL;

    if (accept == NULL)
        return 400;

    if (xs_str_in(accept, "application/activity+json") == -1 &&
        xs_str_in(accept, "application/ld+json") == -1)
        return 0;

    xs *l = xs_split_n(q_path, "/", 2);
    char *uid, *p_path;

    uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("activitypub_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    if (p_path == NULL) {
        /* if there was no component after the user, it's an actor request */
    }
    else
    if (strcmp(p_path, "outbox") == 0) {
        xs *id = xs_fmt("%s/outbox", snac.actor);
        msg = msg_collection(&snac, id);

        /* replace the 'orderedItems' with the latest posts */
        /* ... */
    }
    else
    if (strcmp(p_path, "followers") == 0 || strcmp(p_path, "following") == 0) {
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);
        msg = msg_collection(&snac, id);
    }
    else
    if (xs_startswith(p_path, "p/")) {
    }
    else
        status = 404;

    if (status == 200 && msg != NULL) {
        *body   = xs_json_dumps_pp(msg, 4);
        *b_size = strlen(*body);
    }

    user_free(&snac);

    return status;
}


int activitypub_post_handler(d_char *req, char *q_path,
                             d_char *payload, int p_size,
                             char **body, int *b_size, char **ctype)
/* processes an input message */
{
    int status = 202; /* accepted */
    char *i_ctype = xs_dict_get(req, "content-type");
    snac snac;

    if (i_ctype == NULL)
        return 400;

    if (xs_str_in(i_ctype, "application/activity+json") == -1 &&
        xs_str_in(i_ctype, "application/ld+json") == -1)
        return 0;

    /* decode the message */
    xs *msg = xs_json_loads(payload);

    if (msg == NULL) {
        srv_log(xs_fmt("activitypub_post_handler JSON error %s", q_path));
        status = 400;
    }

    /* get the user and path */
    xs *l = xs_split_n(q_path, "/", 2);
    char *uid;

    if (xs_list_len(l) != 3 || strcmp(xs_list_get(l, 2), "inbox") != 0) {
        /* strange q_path */
        srv_debug(1, xs_fmt("activitypub_post_handler unsupported path %s", q_path));
        return 404;
    }

    uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("activitypub_post_handler bad user %s", uid));
        return 404;
    }

    enqueue_input(&snac, msg, req);

    user_free(&snac);

    if (valid_status(status))
        *ctype = "application/activity+json";

    return status;
}
