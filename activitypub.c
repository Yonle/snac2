/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_mime.h"
#include "xs_openssl.h"
#include "xs_regex.h"
#include "xs_time.h"
#include "xs_set.h"

#include "snac.h"

#include <sys/wait.h>

const char *public_address = "https:/" "/www.w3.org/ns/activitystreams#Public";

/* susie.png */

const char *susie =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAUUlEQVQoz43R0QkAMQwCUDdw/y3dwE"
    "vsvzlL4X1IoQkAisKmwfAFT3RgJHbQezpSRoXEq"
    "eqCL9BJBf7h3QbOCCxV5EVWMEMwG7K1/WODtlvx"
    "AYTtEsDU9F34AAAAAElFTkSuQmCC";

const char *susie_cool =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAV0lEQVQoz43RwQ3AMAwCQDZg/y3ZgN"
    "qo3+JaedwDOUQBQFHYaTB8wTM6sGl2cMPu+DFzn"
    "+ZcgN7wF7ZVihXkfSlWIVzIA6dbQzaygllpNuTX"
    "ZmmFNlvxADX1+o0cUPMbAAAAAElFTkSuQmCC";


const char *default_avatar_base64(void)
/* returns the default avatar in base64 */
{
    time_t t = time(NULL);
    struct tm tm;

    gmtime_r(&t, &tm);

    return tm.tm_wday == 0 || tm.tm_wday == 6 ? susie_cool : susie;
}


int activitypub_request(snac *snac, char *url, d_char **data)
/* request an object */
{
    int status;
    xs *response = NULL;
    xs *payload = NULL;
    int p_size;
    char *ctype;

    /* get from the net */
    response = http_signed_request(snac, "GET", url,
        NULL, NULL, 0, &status, &payload, &p_size, 0);

    if (status == 0 || (status >= 500 && status <= 599)) {
        /* I found an instance running Misskey that returned
           500 on signed messages but returned the object
           perfectly without signing (?), so why not try */
        xs_free(response);

        xs *hdrs = xs_dict_new();
        hdrs = xs_dict_append(hdrs, "accept", "application/activity+json");

        response = xs_http_request("GET", url, hdrs,
            NULL, 0, &status, &payload, &p_size, 0);
    }

    if (valid_status(status)) {
        /* ensure it's ActivityPub data */
        ctype = xs_dict_get(response, "content-type");

        if (xs_str_in(ctype, "application/activity+json") != -1 ||
            xs_str_in(ctype, "application/ld+json") != -1)
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

    if (data)
        *data = NULL;

    /* get from disk first */
    status = actor_get(snac, actor, data);

    if (status != 200) {
        /* actor data non-existent or stale: get from the net */
        status2 = activitypub_request(snac, actor, &payload);

        if (valid_status(status2)) {
            /* renew data */
            status = actor_add(snac, actor, payload);

            if (data != NULL) {
                *data   = payload;
                payload = NULL;
            }
        }
    }

    if (valid_status(status) && data && *data)
        inbox_add_by_actor(*data);

    return status;
}


int timeline_request(snac *snac, char **id, d_char **wrk)
/* ensures that an entry and its ancestors are in the timeline */
{
    int status = 0;

    if (!xs_is_null(*id)) {
        /* is the admired object already there? */
        if (!object_here(*id)) {
            xs *object = NULL;

            /* no; download it */
            status = activitypub_request(snac, *id, &object);

            if (valid_status(status)) {
                char *type = xs_dict_get(object, "type");

                /* get the id again from the object, as it may be different */
                char *nid = xs_dict_get(object, "id");

                if (wrk && strcmp(nid, *id) != 0) {
                    snac_debug(snac, 1,
                        xs_fmt("timeline_request canonical id for %s is %s", *id, nid));

                    *wrk = xs_dup(nid);
                    *id  = *wrk;
                }

                if (!xs_is_null(type) && strcmp(type, "Note") == 0) {
                    char *actor = xs_dict_get(object, "attributedTo");

                    /* request (and drop) the actor for this entry */
                    if (!xs_is_null(actor))
                        actor_request(snac, actor, NULL);

                    /* does it have an ancestor? */
                    char *in_reply_to = xs_dict_get(object, "inReplyTo");

                    /* recurse! */
                    timeline_request(snac, &in_reply_to, NULL);

                    /* finally store */
                    timeline_add(snac, *id, object);
                }
            }
        }
    }

    return status;
}


int send_to_inbox_raw(const char *keyid, const char *seckey,
                  const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout)
/* sends a message to an Inbox */
{
    int status;
    xs_dict *response;
    xs *j_msg = xs_json_dumps_pp((xs_dict *)msg, 4);

    response = http_signed_request_raw(keyid, seckey, "POST", inbox,
        NULL, j_msg, strlen(j_msg), &status, payload, p_size, timeout);

    xs_free(response);

    return status;
}


int send_to_inbox(snac *snac, const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout)
/* sends a message to an Inbox */
{
    char *seckey = xs_dict_get(snac->key, "secret");

    return send_to_inbox_raw(snac->actor, seckey, inbox, msg, payload, p_size, timeout);
}


d_char *get_actor_inbox(snac *snac, char *actor)
/* gets an actor's inbox */
{
    xs *data = NULL;
    char *v = NULL;

    if (valid_status(actor_request(snac, actor, &data))) {
        /* try first endpoints/sharedInbox */
        if ((v = xs_dict_get(data, "endpoints")))
            v = xs_dict_get(v, "sharedInbox");

        /* try then the regular inbox */
        if (xs_is_null(v))
            v = xs_dict_get(data, "inbox");
    }

    return xs_is_null(v) ? NULL : xs_dup(v);
}


int send_to_actor(snac *snac, char *actor, char *msg, d_char **payload, int *p_size, int timeout)
/* sends a message to an actor */
{
    int status = 400;
    xs *inbox = get_actor_inbox(snac, actor);

    if (!xs_is_null(inbox))
        status = send_to_inbox(snac, inbox, msg, payload, p_size, timeout);

    return status;
}


d_char *recipient_list(snac *snac, char *msg, int expand_public)
/* returns the list of recipients for a message */
{
    char *to = xs_dict_get(msg, "to");
    char *cc = xs_dict_get(msg, "cc");
    xs_set rcpts;
    int n;

    xs_set_init(&rcpts);

    char *lists[] = { to, cc, NULL };
    for (n = 0; lists[n]; n++) {
        char *l = lists[n];
        char *v;
        xs *tl = NULL;

        /* if it's a string, create a list with only one element */
        if (xs_type(l) == XSTYPE_STRING) {
            tl = xs_list_new();
            tl = xs_list_append(tl, l);

            l = tl;
        }

        while (xs_list_iter(&l, &v)) {
            if (expand_public && strcmp(v, public_address) == 0) {
                /* iterate the followers and add them */
                xs *fwers = follower_list(snac);
                char *actor;

                char *p = fwers;
                while (xs_list_iter(&p, &actor))
                    xs_set_add(&rcpts, actor);
            }
            else
                xs_set_add(&rcpts, v);
        }
    }

    return xs_set_result(&rcpts);
}


int is_msg_public(snac *snac, xs_dict *msg)
/* checks if a message is public */
{
    xs *rcpts = recipient_list(snac, msg, 0);

    return xs_list_in(rcpts, public_address) != -1;
}


void process_tags(snac *snac, const char *content, d_char **n_content, d_char **tag)
/* parses mentions and tags from content */
{
    d_char *nc = xs_str_new(NULL);
    d_char *tl = xs_list_new();
    xs *split;
    char *p, *v;
    int n = 0;

    split = xs_regex_split(content, "(@[A-Za-z0-9_]+@[A-Za-z0-9\\.-]+|&#[0-9]+;|#[^ ,\\.:;<]+)");

    p = split;
    while (xs_list_iter(&p, &v)) {
        if ((n & 0x1)) {
            if (*v == '@') {
                /* query the webfinger about this fellow */
                xs *v2    = xs_strip_chars_i(xs_dup(v), "@.");
                xs *actor = NULL;
                xs *uid   = NULL;
                int status;

                status = webfinger_request(v2, &actor, &uid);

                if (valid_status(status)) {
                    xs *d = xs_dict_new();
                    xs *n = xs_fmt("@%s", uid);
                    xs *l = xs_fmt("<a href=\"%s\" class=\"u-url mention\">%s</a>", actor, n);

                    d = xs_dict_append(d, "type",   "Mention");
                    d = xs_dict_append(d, "href",   actor);
                    d = xs_dict_append(d, "name",   n);

                    tl = xs_list_append(tl, d);

                    /* add the code */
                    nc = xs_str_cat(nc, l);
                }
                else
                    /* store as is */
                    nc = xs_str_cat(nc, v);
            }
            else
            if (*v == '#') {
                /* hashtag */
                xs *d = xs_dict_new();
                xs *n = xs_tolower_i(xs_dup(v));
                xs *h = xs_fmt("%s%s", snac->actor, n);
                xs *l = xs_fmt("<a href=\"%s\" class=\"mention hashtag\" rel=\"tag\">%s</a>", h, v);

                d = xs_dict_append(d, "type",   "Hashtag");
                d = xs_dict_append(d, "href",   h);
                d = xs_dict_append(d, "name",   n);

                tl = xs_list_append(tl, d);

                /* add the code */
                nc = xs_str_cat(nc, l);
            }
            else
            if (*v == '&') {
                /* HTML Unicode entity, probably part of an emoji */

                /* write as is */
                nc = xs_str_cat(nc, v);
            }
        }
        else
            nc = xs_str_cat(nc, v);

        n++;
    }

    *n_content = nc;
    *tag       = tl;
}


/** messages **/

d_char *msg_base(snac *snac, char *type, char *id, char *actor, char *date, char *object)
/* creates a base ActivityPub message */
{
    xs *did       = NULL;
    xs *published = NULL;
    xs *ntid      = tid(0);

    /* generated values */
    if (date && strcmp(date, "@now") == 0) {
        published = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");
        date = published;
    }

    if (id != NULL) {
        if (strcmp(id, "@dummy") == 0) {
            did = xs_fmt("%s/d/%s/%s", snac->actor, ntid, type);

            id = did;
        }
        else
        if (strcmp(id, "@object") == 0) {
            if (object != NULL) {
                did = xs_fmt("%s/%s_%s", xs_dict_get(object, "id"), type, ntid);
                id = did;
            }
            else
                id = NULL;
        }
    }

    d_char *msg = xs_dict_new();

    msg = xs_dict_append(msg, "@context", "https:/" "/www.w3.org/ns/activitystreams");
    msg = xs_dict_append(msg, "type",     type);

    if (id != NULL)
        msg = xs_dict_append(msg, "id", id);

    if (actor != NULL)
        msg = xs_dict_append(msg, "actor", actor);

    if (date != NULL)
        msg = xs_dict_append(msg, "published", date);

    if (object != NULL)
        msg = xs_dict_append(msg, "object", object);

    return msg;
}


d_char *msg_collection(snac *snac, char *id)
/* creates an empty OrderedCollection message */
{
    d_char *msg = msg_base(snac, "OrderedCollection", id, NULL, NULL, NULL);
    xs *ol = xs_list_new();
    xs *nz = xs_number_new(0);

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "orderedItems", ol);
    msg = xs_dict_append(msg, "totalItems",   nz);

    return msg;
}


d_char *msg_accept(snac *snac, char *object, char *to)
/* creates an Accept message (as a response to a Follow) */
{
    d_char *msg = msg_base(snac, "Accept", "@dummy", snac->actor, NULL, object);

    msg = xs_dict_append(msg, "to", to);

    return msg;
}


d_char *msg_update(snac *snac, char *object)
/* creates an Update message */
{
    d_char *msg = msg_base(snac, "Update", "@object", snac->actor, "@now", object);

    msg = xs_dict_append(msg, "to", public_address);

    return msg;
}


d_char *msg_admiration(snac *snac, char *object, char *type)
/* creates a Like or Announce message */
{
    xs *a_msg   = NULL;
    d_char *msg = NULL;
    xs *wrk     = NULL;

    /* call the object */
    timeline_request(snac, &object, &wrk);

    if (valid_status(object_get(object, &a_msg))) {
        xs *rcpts = xs_list_new();

        msg = msg_base(snac, type, "@dummy", snac->actor, "@now", object);

        rcpts = xs_list_append(rcpts, public_address);
        rcpts = xs_list_append(rcpts, xs_dict_get(a_msg, "attributedTo"));

        msg = xs_dict_append(msg, "to", rcpts);
    }
    else
        snac_log(snac, xs_fmt("msg_admiration cannot retrieve object %s", object));

    return msg;
}


d_char *msg_actor(snac *snac)
/* create a Person message for this actor */
{
    xs *ctxt    = xs_list_new();
    xs *icon    = xs_dict_new();
    xs *keys    = xs_dict_new();
    xs *avtr    = NULL;
    xs *kid     = NULL;
    xs *f_bio   = NULL;
    d_char *msg = msg_base(snac, "Person", snac->actor, NULL, NULL, NULL);
    char *p;
    int n;

    /* change the @context (is this really necessary?) */
    ctxt = xs_list_append(ctxt, "https:/" "/www.w3.org/ns/activitystreams");
    ctxt = xs_list_append(ctxt, "https:/" "/w3id.org/security/v1");
    msg = xs_dict_set(msg, "@context",          ctxt);

    msg = xs_dict_set(msg, "url",               snac->actor);
    msg = xs_dict_set(msg, "name",              xs_dict_get(snac->config, "name"));
    msg = xs_dict_set(msg, "preferredUsername", snac->uid);
    msg = xs_dict_set(msg, "published",         xs_dict_get(snac->config, "published"));

    f_bio = not_really_markdown(xs_dict_get(snac->config, "bio"));
    msg = xs_dict_set(msg, "summary", f_bio);

    char *folders[] = { "inbox", "outbox", "followers", "following", NULL };
    for (n = 0; folders[n]; n++) {
        xs *f = xs_fmt("%s/%s", snac->actor, folders[n]);
        msg = xs_dict_set(msg, folders[n], f);
    }

    p = xs_dict_get(snac->config, "avatar");

    if (*p == '\0')
        avtr = xs_fmt("%s/susie.png", srv_baseurl);
    else
        avtr = xs_dup(p);

    icon = xs_dict_append(icon, "type",         "Image");
    icon = xs_dict_append(icon, "mediaType",    xs_mime_by_ext(avtr));
    icon = xs_dict_append(icon, "url",          avtr);
    msg = xs_dict_set(msg, "icon", icon);

    kid = xs_fmt("%s#main-key", snac->actor);

    keys = xs_dict_append(keys, "id",           kid);
    keys = xs_dict_append(keys, "owner",        snac->actor);
    keys = xs_dict_append(keys, "publicKeyPem", xs_dict_get(snac->key, "public"));
    msg = xs_dict_set(msg, "publicKey", keys);

    return msg;
}


d_char *msg_create(snac *snac, char *object)
/* creates a 'Create' message */
{
    d_char *msg = msg_base(snac, "Create", "@object", snac->actor, "@now", object);

    msg = xs_dict_append(msg, "attributedTo", xs_dict_get(object, "attributedTo"));
    msg = xs_dict_append(msg, "to",           xs_dict_get(object, "to"));
    msg = xs_dict_append(msg, "cc",           xs_dict_get(object, "cc"));

    return msg;
}


d_char *msg_undo(snac *snac, char *object)
/* creates an 'Undo' message */
{
    d_char *msg = msg_base(snac, "Undo", "@object", snac->actor, "@now", object);

    msg = xs_dict_append(msg, "to", xs_dict_get(object, "object"));

    return msg;
}


d_char *msg_delete(snac *snac, char *id)
/* creates a 'Delete' + 'Tombstone' for a local entry */
{
    xs *tomb = xs_dict_new();
    d_char *msg = NULL;

    /* sculpt the tombstone */
    tomb = xs_dict_append(tomb, "type", "Tombstone");
    tomb = xs_dict_append(tomb, "id",   id);

    /* now create the Delete */
    msg = msg_base(snac, "Delete", "@object", snac->actor, "@now", tomb);

    msg = xs_dict_append(msg, "to", public_address);

    return msg;
}


d_char *msg_follow(snac *snac, char *url_or_uid)
/* creates a 'Follow' message */
{
    xs *actor_o = NULL;
    xs *actor   = NULL;
    d_char *msg = NULL;
    int status;

    if (xs_startswith(url_or_uid, "https:/"))
        actor = xs_dup(url_or_uid);
    else
    if (!valid_status(webfinger_request(url_or_uid, &actor, NULL))) {
        snac_log(snac, xs_fmt("cannot resolve user %s to follow", url_or_uid));
        return NULL;
    }

    /* request the actor */
    status = actor_request(snac, actor, &actor_o);

    if (valid_status(status)) {
        /* check if the actor is an alias */
        char *r_actor = xs_dict_get(actor_o, "id");

        if (r_actor && strcmp(actor, r_actor) != 0) {
            snac_log(snac, xs_fmt("actor to follow is an alias %s -> %s", actor, r_actor));
        }

        msg = msg_base(snac, "Follow", "@dummy", snac->actor, NULL, r_actor);
    }
    else
        snac_log(snac, xs_fmt("cannot get actor to follow %s %d", actor, status));

    return msg;
}


xs_dict *msg_note(snac *snac, xs_str *content, xs_val *rcpts,
                  xs_str *in_reply_to, xs_list *attach, int priv)
/* creates a 'Note' message */
{
    xs *ntid = tid(0);
    xs *id   = xs_fmt("%s/p/%s", snac->actor, ntid);
    xs *ctxt = NULL;
    xs *fc2  = NULL;
    xs *fc1  = NULL;
    xs *to   = NULL;
    xs *cc   = xs_list_new();
    xs *irt  = NULL;
    xs *tag  = NULL;
    xs *atls = NULL;
    xs_dict *msg = msg_base(snac, "Note", id, NULL, "@now", NULL);
    xs_list *p;
    xs_val *v;

    if (rcpts == NULL)
        to = xs_list_new();
    else {
        if (xs_type(rcpts) == XSTYPE_STRING) {
            to = xs_list_new();
            to = xs_list_append(to, rcpts);
        }
        else
            to = xs_dup(rcpts);
    }

    /* format the content */
    fc2 = not_really_markdown(content);

    /* extract the tags */
    process_tags(snac, fc2, &fc1, &tag);

    if (tag == NULL)
        tag = xs_list_new();

    if (in_reply_to != NULL && *in_reply_to) {
        xs *p_msg = NULL;
        xs *wrk   = NULL;

        /* demand this thing */
        timeline_request(snac, &in_reply_to, &wrk);

        if (valid_status(object_get(in_reply_to, &p_msg))) {
            /* add this author as recipient */
            char *a, *v;

            if ((a = xs_dict_get(p_msg, "attributedTo")) && xs_list_in(to, a) == -1)
                to = xs_list_append(to, a);

            /* add this author to the tag list as a mention */
            xs *t_href = NULL;
            xs *t_name = NULL;

            if (!xs_is_null(a) && valid_status(webfinger_request(a, &t_href, &t_name))) {
                xs *t = xs_dict_new();

                t = xs_dict_append(t, "type", "Mention");
                t = xs_dict_append(t, "href", t_href);
                t = xs_dict_append(t, "name", t_name);

                tag = xs_list_append(tag, t);
            }

            /* get the context, if there is one */
            if ((v = xs_dict_get(p_msg, "context")))
                ctxt = xs_dup(v);

            /* if this message is public, ours will also be */
            if (!priv && is_msg_public(snac, p_msg) && xs_list_in(to, public_address) == -1)
                to = xs_list_append(to, public_address);
        }

        irt = xs_dup(in_reply_to);
    }
    else
        irt = xs_val_new(XSTYPE_NULL);

    /* create the attachment list, if there are any */
    if (!xs_is_null(attach)) {
        atls = xs_list_new();

        while (xs_list_iter(&attach, &v)) {
            xs *d = xs_dict_new();
            char *url  = xs_list_get(v, 0);
            char *alt  = xs_list_get(v, 1);
            char *mime = xs_mime_by_ext(url);

            d = xs_dict_append(d, "mediaType", mime);
            d = xs_dict_append(d, "url",       url);
            d = xs_dict_append(d, "name",      alt);
            d = xs_dict_append(d, "type",
                xs_startswith(mime, "image/") ? "Image" : "Document");

            atls = xs_list_append(atls, d);
        }
    }

    if (ctxt == NULL)
        ctxt = xs_fmt("%s#ctxt", id);

    /* add all mentions to the cc */
    p = tag;
    while (xs_list_iter(&p, &v)) {
        if (xs_type(v) == XSTYPE_DICT) {
            char *t;

            if ((t = xs_dict_get(v, "type")) != NULL && strcmp(t, "Mention") == 0) {
                if ((t = xs_dict_get(v, "href")) != NULL)
                    cc = xs_list_append(cc, t);
            }
        }
    }

    /* no recipients? must be for everybody */
    if (!priv && xs_list_len(to) == 0)
        to = xs_list_append(to, public_address);

    /* delete all cc recipients that also are in the to */
    p = to;
    while (xs_list_iter(&p, &v)) {
        int i;

        if ((i = xs_list_in(cc, v)) != -1)
            cc = xs_list_del(cc, i);
    }

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "summary",      "");
    msg = xs_dict_append(msg, "content",      fc1);
    msg = xs_dict_append(msg, "context",      ctxt);
    msg = xs_dict_append(msg, "url",          id);
    msg = xs_dict_append(msg, "to",           to);
    msg = xs_dict_append(msg, "cc",           cc);
    msg = xs_dict_append(msg, "inReplyTo",    irt);
    msg = xs_dict_append(msg, "tag",          tag);

    msg = xs_dict_append(msg, "sourceContent", content);

    if (atls != NULL)
        msg = xs_dict_append(msg, "attachment", atls);

    return msg;
}


void notify(snac *snac, xs_str *type, xs_str *utype, xs_str *actor, xs_dict *msg)
/* notifies the user of relevant events */
{
    xs_val *object = NULL;

    if (strcmp(type, "Create") == 0) {
        /* only notify of notes specifically for us */
        xs *rcpts = recipient_list(snac, msg, 0);

        if (xs_list_in(rcpts, snac->actor) == -1)
            return;
    }

    if (strcmp(type, "Undo") == 0 && strcmp(utype, "Follow") != 0)
        return;

    if (strcmp(type, "Like") == 0 || strcmp(type, "Announce") == 0) {
        object = xs_dict_get(msg, "object");

        if (xs_is_null(object))
            return;
        else {
            if (xs_type(object) == XSTYPE_DICT)
                object = xs_dict_get(object, "id");

            /* if it's not an admiration about something by us, done */
            if (xs_is_null(object) || !xs_startswith(object, snac->actor))
                return;
        }
    }

    /* prepare message body */
    xs *body = xs_fmt("User  : @%s@%s\n",
        xs_dict_get(snac->config, "uid"),
        xs_dict_get(srv_config,   "host")
    );

    if (strcmp(utype, "(null)") != 0) {
        xs *s1 = xs_fmt("Type  : %s + %s\n", type, utype);
        body = xs_str_cat(body, s1);
    }
    else {
        xs *s1 = xs_fmt("Type  : %s\n", type);
        body = xs_str_cat(body, s1);
    }

    {
        xs *s1 = xs_fmt("Actor : %s\n", actor);
        body = xs_str_cat(body, s1);
    }

    if (object != NULL) {
        xs *s1 = xs_fmt("Object: %s\n", object);
        body = xs_str_cat(body, s1);
    }

    /* email */

    const char *email = "[disabled by admin]";

    if (xs_type(xs_dict_get(srv_config, "disable_email_notifications")) != XSTYPE_TRUE) {
        email = xs_dict_get(snac->config_o, "email");
        if (xs_is_null(email)) {
            email = xs_dict_get(snac->config, "email");

            if (xs_is_null(email))
                email = "[empty]";
        }
    }

    if (*email != '\0' && *email != '[') {
        snac_debug(snac, 1, xs_fmt("email notify %s %s %s", type, utype, actor));

        xs *subject = xs_fmt("snac notify for @%s@%s",
                    xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"));
        xs *from    = xs_fmt("snac-daemon <snac-daemon@%s>", xs_dict_get(srv_config, "host"));
        xs *header  = xs_fmt(
                    "From: %s\n"
                    "To: %s\n"
                    "Subject: %s\n"
                    "\n",
                    from, email, subject);

        xs *email_body = xs_fmt("%s%s", header, body);

        enqueue_email(email_body, 0);
    }

    /* telegram */

    char *bot     = xs_dict_get(snac->config, "telegram_bot");
    char *chat_id = xs_dict_get(snac->config, "telegram_chat_id");

    if (!xs_is_null(bot) && !xs_is_null(chat_id) && *bot && *chat_id)
        enqueue_telegram(body, bot, chat_id);
}


/** queues **/

int process_input_message(snac *snac, char *msg, char *req)
/* processes an ActivityPub message from the input queue */
{
    /* actor and type exist, were checked previously */
    char *actor  = xs_dict_get(msg, "actor");
    char *type   = xs_dict_get(msg, "type");
    xs *actor_o = NULL;
    int a_status;
    int do_notify = 0;

    char *object, *utype;

    object = xs_dict_get(msg, "object");
    if (object != NULL && xs_type(object) == XSTYPE_DICT)
        utype = xs_dict_get(object, "type");
    else
        utype = "(null)";

    /* bring the actor */
    a_status = actor_request(snac, actor, &actor_o);

    /* if the actor does not explicitly exist, discard */
    if (a_status == 404 || a_status == 410) {
        snac_debug(snac, 1,
            xs_fmt("dropping message due to actor error %s %d", actor, a_status));

        return 1;
    }

    if (!valid_status(a_status)) {
        /* other actor download errors may need a retry */
        snac_debug(snac, 1,
            xs_fmt("error requesting actor %s %d -- retry later", actor, a_status));

        return 0;
    }

    /* check the signature */
    xs *sig_err = NULL;

    if (!check_signature(snac, req, &sig_err)) {
        snac_log(snac, xs_fmt("bad signature %s (%s)", actor, sig_err));

        srv_archive_error("check_signature", sig_err, req, msg);

        return 1;
    }

    if (strcmp(type, "Follow") == 0) {
        if (!follower_check(snac, actor)) {
            xs *f_msg = xs_dup(msg);
            xs *reply = msg_accept(snac, f_msg, actor);

            enqueue_message(snac, reply);

            if (xs_is_null(xs_dict_get(f_msg, "published"))) {
                /* add a date if it doesn't include one (Mastodon) */
                xs *date = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");
                f_msg = xs_dict_set(f_msg, "published", date);
            }

            timeline_add(snac, xs_dict_get(f_msg, "id"), f_msg);

            follower_add(snac, actor);

            snac_log(snac, xs_fmt("new follower %s", actor));
            do_notify = 1;
        }
        else
            snac_log(snac, xs_fmt("repeated 'Follow' from %s", actor));
    }
    else
    if (strcmp(type, "Undo") == 0) {
        if (strcmp(utype, "Follow") == 0) {
            if (valid_status(follower_del(snac, actor))) {
                snac_log(snac, xs_fmt("no longer following us %s", actor));
                do_notify = 1;
            }
            else
                snac_log(snac, xs_fmt("error deleting follower %s", actor));
        }
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Undo' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Create") == 0) {
        if (strcmp(utype, "Note") == 0) {
            if (is_muted(snac, actor))
                snac_log(snac, xs_fmt("ignored 'Note' from muted actor %s", actor));
            else {
                char *id          = xs_dict_get(object, "id");
                char *in_reply_to = xs_dict_get(object, "inReplyTo");
                xs *wrk           = NULL;

                timeline_request(snac, &in_reply_to, &wrk);

                if (timeline_add(snac, id, object)) {
                    snac_log(snac, xs_fmt("new 'Note' %s %s", actor, id));
                    do_notify = 1;
                }
            }
        }
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Create' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Accept") == 0) {
        if (strcmp(utype, "Follow") == 0) {
            if (following_check(snac, actor)) {
                following_add(snac, actor, msg);
                snac_log(snac, xs_fmt("confirmed follow from %s", actor));
            }
            else
                snac_log(snac, xs_fmt("spurious follow accept from %s", actor));
        }
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Accept' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Like") == 0) {
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        timeline_admire(snac, object, actor, 1);
        snac_log(snac, xs_fmt("new 'Like' %s %s", actor, object));
        do_notify = 1;
    }
    else
    if (strcmp(type, "Announce") == 0) {
        xs *a_msg = NULL;
        xs *wrk   = NULL;

        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        timeline_request(snac, &object, &wrk);

        if (valid_status(object_get(object, &a_msg))) {
            char *who = xs_dict_get(a_msg, "attributedTo");

            if (who && !is_muted(snac, who)) {
                /* bring the actor */
                xs *who_o = NULL;

                if (valid_status(actor_request(snac, who, &who_o))) {
                    timeline_admire(snac, object, actor, 0);
                    snac_log(snac, xs_fmt("new 'Announce' %s %s", actor, object));
                    do_notify = 1;
                }
                else
                    snac_log(snac, xs_fmt("dropped 'Announce' on actor request error %s", who));
            }
            else
                snac_log(snac, xs_fmt("ignored 'Announce' about muted actor %s", who));
        }
        else
            snac_log(snac, xs_fmt("error requesting 'Announce' object %s", object));
    }
    else
    if (strcmp(type, "Update") == 0) {
        if (strcmp(utype, "Person") == 0) {
            actor_add(snac, actor, xs_dict_get(msg, "object"));

            snac_log(snac, xs_fmt("updated actor %s", actor));
        }
        else
        if (strcmp(utype, "Note") == 0) {
            char *id = xs_dict_get(object, "id");

            object_add_ow(id, object);

            snac_log(snac, xs_fmt("updated post %s", id));
        }
        else
            snac_log(snac, xs_fmt("ignored 'Update' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Delete") == 0) {
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        if (valid_status(timeline_del(snac, object)))
            snac_debug(snac, 1, xs_fmt("new 'Delete' %s %s", actor, object));
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Delete' for unknown object %s", object));
    }
    else
        snac_debug(snac, 1, xs_fmt("process_message type '%s' ignored", type));

    if (do_notify)
        notify(snac, type, utype, actor, msg);

    return 1;
}


int send_email(char *msg)
/* invoke sendmail with email headers and body in msg */
{
    FILE *f;
    int status;
    int fds[2];
    pid_t pid;
    if (pipe(fds) == -1) return -1;
    pid = vfork();
    if (pid == -1) return -1;
    else if (pid == 0) {
        dup2(fds[0], 0);
        close(fds[0]);
        close(fds[1]);
        execl("/usr/sbin/sendmail", "sendmail", "-t", (char *) NULL);
        _exit(1);
    }
    close(fds[0]);
    if ((f = fdopen(fds[1], "w")) == NULL) {
        close(fds[1]);
        return -1;
    }
    fprintf(f, "%s\n", msg);
    fclose(f);
    if (waitpid(pid, &status, 0) == -1) return -1;
    return status;
}


void process_user_queue_item(snac *snac, xs_dict *q_item)
/* processes an item from the user queue */
{
    char *type;
    int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

    if ((type = xs_dict_get(q_item, "type")) == NULL)
        type = "output";

    if (strcmp(type, "message") == 0) {
        xs_dict *msg = xs_dict_get(q_item, "message");
        xs *rcpts    = recipient_list(snac, msg, 1);
        xs_set inboxes;
        xs_list *p;
        xs_str *actor;

        xs_set_init(&inboxes);

        /* if it's public, send first to the collected inboxes */
        if (is_msg_public(snac, msg)) {
            xs *shibx = inbox_list();
            xs_str *v;

            p = shibx;
            while (xs_list_iter(&p, &v)) {
                if (xs_set_add(&inboxes, v) == 1)
                    enqueue_output(snac, msg, v, 0);
            }
        }

        /* iterate now the recipients */
        p = rcpts;
        while (xs_list_iter(&p, &actor)) {
            xs *inbox = get_actor_inbox(snac, actor);

            if (inbox != NULL) {
                /* add to the set and, if it's not there, send message */
                if (xs_set_add(&inboxes, inbox) == 1)
                    enqueue_output(snac, msg, inbox, 0);
            }
            else
                snac_log(snac, xs_fmt("cannot find inbox for %s", actor));
        }

        xs_set_free(&inboxes);
    }
    else
    if (strcmp(type, "input") == 0) {
        /* process the message */
        xs_dict *msg = xs_dict_get(q_item, "message");
        xs_dict *req = xs_dict_get(q_item, "req");
        int retries  = xs_number_get(xs_dict_get(q_item, "retries"));

        if (xs_is_null(msg))
            return;

        if (!process_input_message(snac, msg, req)) {
            if (retries > queue_retry_max)
                snac_log(snac, xs_fmt("input giving up"));
            else {
                /* reenqueue */
                enqueue_input(snac, msg, req, retries + 1);
                snac_log(snac, xs_fmt("input requeue #%d", retries + 1));
            }
        }
    }
    else
        snac_log(snac, xs_fmt("unexpected q_item type '%s'", type));
}


int process_user_queue(snac *snac)
/* processes a user's queue */
{
    int cnt = 0;
    xs *list = user_queue(snac);

    xs_list *p = list;
    xs_str *fn;

    while (xs_list_iter(&p, &fn)) {
        xs *q_item = dequeue(fn);

        if (q_item == NULL) {
            snac_log(snac, xs_fmt("process_user_queue q_item error"));
            continue;
        }

        process_user_queue_item(snac, q_item);
        cnt++;
    }

    return cnt;
}


void process_queue_item(xs_dict *q_item)
/* processes an item from the global queue */
{
    char *type = xs_dict_get(q_item, "type");
    int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

    if (strcmp(type, "output") == 0) {
        int status;
        xs_str *inbox  = xs_dict_get(q_item, "inbox");
        xs_str *keyid  = xs_dict_get(q_item, "keyid");
        xs_str *seckey = xs_dict_get(q_item, "seckey");
        xs_dict *msg   = xs_dict_get(q_item, "message");
        int retries    = xs_number_get(xs_dict_get(q_item, "retries"));
        xs *payload    = NULL;
        int p_size     = 0;

        if (xs_is_null(inbox) || xs_is_null(msg) || xs_is_null(keyid) || xs_is_null(seckey)) {
            srv_log(xs_fmt("output message error: missing fields"));
            return;
        }

        /* deliver */
        status = send_to_inbox_raw(keyid, seckey, inbox, msg, &payload, &p_size, retries == 0 ? 3 : 8);

        if (payload) {
            if (p_size > 24) {
                /* trim the message */
                payload[24] = '\0';
                payload = xs_str_cat(payload, "...");
            }

            /* strip ugly control characters */
            payload = xs_replace_i(payload, "\n", "");
            payload = xs_replace_i(payload, "\r", "");

            if (*payload)
                payload = xs_str_wrap_i(" [", payload, "]");
        }
        else
            payload = xs_str_new(NULL);

        srv_log(xs_fmt("output message: sent to inbox %s %d%s", inbox, status, payload));

        if (!valid_status(status)) {
            retries++;

            /* error sending; requeue? */
            if (status == 404 || status == 410)
                /* explicit error: discard */
                srv_log(xs_fmt("output message: fatal error %s %d", inbox, status));
            else
            if (retries > queue_retry_max)
                srv_log(xs_fmt("output message: giving up %s %d", inbox, status));
            else {
                /* requeue */
                enqueue_output_raw(keyid, seckey, msg, inbox, retries);
                srv_log(xs_fmt("output message: requeue %s #%d", inbox, retries));
            }
        }
    }
    else
    if (strcmp(type, "email") == 0) {
        /* send this email */
        xs_str *msg = xs_dict_get(q_item, "message");
        int retries = xs_number_get(xs_dict_get(q_item, "retries"));

        if (!send_email(msg))
            srv_debug(1, xs_fmt("email message sent"));
        else {
            retries++;

            if (retries > queue_retry_max)
                srv_log(xs_fmt("email giving up (errno: %d)", errno));
            else {
                /* requeue */
                srv_log(xs_fmt(
                    "email requeue #%d (errno: %d)", retries, errno));

                enqueue_email(msg, retries);
            }
        }
    }
    else
    if (strcmp(type, "telegram") == 0) {
        /* send this via telegram */
        char *bot   = xs_dict_get(q_item, "bot");
        char *msg   = xs_dict_get(q_item, "message");
        xs *chat_id = xs_dup(xs_dict_get(q_item, "chat_id"));
        int status  = 0;

        /* chat_id must start with a - */
        if (!xs_startswith(chat_id, "-"))
            chat_id = xs_str_wrap_i("-", chat_id, NULL);

        xs *url  = xs_fmt("https:/" "/api.telegram.org/bot%s/sendMessage", bot);
        xs *body = xs_fmt("{\"chat_id\":%s,\"text\":\"%s\"}", chat_id, msg);

        xs *headers = xs_dict_new();
        headers = xs_dict_append(headers, "content-type", "application/json");

        xs *rsp  = xs_http_request("POST", url, headers,
                                   body, strlen(body), &status, NULL, NULL, 0);

        srv_debug(0, xs_fmt("telegram post %d", status));
    }
    else
    if (strcmp(type, "purge") == 0) {
        srv_log(xs_dup("purge start"));

        purge_all();

        srv_log(xs_dup("purge end"));
    }
    else
        srv_log(xs_fmt("unexpected q_item type '%s'", type));
}


int process_queue(void)
/* processes the global queue */
{
    int cnt = 0;
    xs *list = queue();

    xs_list *p = list;
    xs_str *fn;

    while (xs_list_iter(&p, &fn)) {
        xs *q_item = dequeue(fn);

        if (q_item != NULL) {
            job_post(q_item);
            cnt++;
        }
    }

    return cnt;
}


/** HTTP handlers */

int activitypub_get_handler(d_char *req, char *q_path,
                            char **body, int *b_size, char **ctype)
{
    int status = 200;
    char *accept = xs_dict_get(req, "accept");
    snac snac;
    xs *msg = NULL;

    if (accept == NULL)
        return 0;

    if (xs_str_in(accept, "application/activity+json") == -1 &&
        xs_str_in(accept, "application/ld+json") == -1)
        return 0;

    xs *l = xs_split_n(q_path, "/", 2);
    char *uid, *p_path;

    uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("activitypub_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    *ctype  = "application/activity+json";

    if (p_path == NULL) {
        /* if there was no component after the user, it's an actor request */
        msg = msg_actor(&snac);
        *ctype = "application/ld+json; profile=\"https://www.w3.org/ns/activitystreams\"";

        snac_debug(&snac, 1, xs_fmt("serving actor"));
    }
    else
    if (strcmp(p_path, "outbox") == 0) {
        xs *id = xs_fmt("%s/outbox", snac.actor);
        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *list = xs_list_new();
        msg = msg_collection(&snac, id);
        char *p, *v;

        p = elems;
        while (xs_list_iter(&p, &v)) {
            xs *i = NULL;

            if (valid_status(object_get_by_md5(v, &i))) {
                char *type = xs_dict_get(i, "type");
                char *id   = xs_dict_get(i, "id");

                if (type && id && strcmp(type, "Note") == 0 && xs_startswith(id, snac.actor)) {
                    i = xs_dict_del(i, "_snac");
                    list = xs_list_append(list, i);
                }
            }
        }

        /* replace the 'orderedItems' with the latest posts */
        xs *items = xs_number_new(xs_list_len(list));
        msg = xs_dict_set(msg, "orderedItems", list);
        msg = xs_dict_set(msg, "totalItems",   items);
    }
    else
    if (strcmp(p_path, "followers") == 0 || strcmp(p_path, "following") == 0) {
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);
        msg = msg_collection(&snac, id);
    }
    else
    if (xs_startswith(p_path, "p/")) {
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);

        status = object_get(id, &msg);
    }
    else
        status = 404;

    if (status == 200 && msg != NULL) {
        *body   = xs_json_dumps_pp(msg, 4);
        *b_size = strlen(*body);
    }

    snac_debug(&snac, 1, xs_fmt("activitypub_get_handler serving %s %d", q_path, status));

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
    char *v;

    if (i_ctype == NULL) {
        *body  = xs_str_new("no content-type");
        *ctype = "text/plain";
        return 400;
    }

    if (xs_str_in(i_ctype, "application/activity+json") == -1 &&
        xs_str_in(i_ctype, "application/ld+json") == -1)
        return 0;

    /* decode the message */
    xs *msg = xs_json_loads(payload);

    if (msg == NULL) {
        srv_log(xs_fmt("activitypub_post_handler JSON error %s", q_path));

        *body  = xs_str_new("JSON error");
        *ctype = "text/plain";
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

    /* if it has a digest, check it now, because
       later the payload won't be exactly the same */
    if ((v = xs_dict_get(req, "digest")) != NULL) {
        xs *s1 = xs_sha256_base64(payload, p_size);
        xs *s2 = xs_fmt("SHA-256=%s", s1);

        if (strcmp(s2, v) != 0) {
            srv_log(xs_fmt("digest check FAILED"));

            *body  = xs_str_new("bad digest");
            *ctype = "text/plain";
            status = 400;
        }
    }

    if (valid_status(status)) {
        enqueue_input(&snac, msg, req, 0);
        *ctype = "application/activity+json";
    }

    user_free(&snac);

    return status;
}
