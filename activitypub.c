/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_mime.h"
#include "xs_openssl.h"
#include "xs_regex.h"
#include "xs_time.h"

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

    /* get from disk first */
    status = actor_get(snac, actor, data);

    if (status == 200)
        return status;

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

    return status;
}


int timeline_request(snac *snac, char *id, char *referrer)
/* ensures that an entry and its ancestors are in the timeline */
{
    int status = 0;

    if (!xs_is_null(id)) {
        /* is the admired object already there? */
        if (!timeline_here(snac, id)) {
            xs *object = NULL;

            /* no; download it */
            status = activitypub_request(snac, id, &object);

            if (valid_status(status)) {
                char *type = xs_dict_get(object, "type");

                if (!xs_is_null(type) && strcmp(type, "Note") == 0) {
                    char *actor = xs_dict_get(object, "attributedTo");

                    /* request (and drop) the actor for this entry */
                    if (!xs_is_null(actor))
                        actor_request(snac, actor, NULL);

                    /* does it have an ancestor? */
                    char *in_reply_to = xs_dict_get(object, "inReplyTo");

                    /* recurse! */
                    timeline_request(snac, in_reply_to, referrer);

                    /* finally store */
                    timeline_add(snac, id, object, in_reply_to, referrer);
                }
            }
        }
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

    xs_free(response);

    return status;
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


int send_to_actor(snac *snac, char *actor, char *msg, d_char **payload, int *p_size)
/* sends a message to an actor */
{
    int status = 400;
    xs *inbox = get_actor_inbox(snac, actor);

    if (!xs_is_null(inbox))
        status = send_to_inbox(snac, inbox, msg, payload, p_size);

    return status;
}


d_char *recipient_list(snac *snac, char *msg, int expand_public)
/* returns the list of recipients for a message */
{
    d_char *list = xs_list_new();
    char *to = xs_dict_get(msg, "to");
    char *cc = xs_dict_get(msg, "cc");
    int n;

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
                char *fw;

                char *p = fwers;
                while (xs_list_iter(&p, &fw)) {
                    char *actor = xs_dict_get(fw, "actor");

                    if (xs_list_in(list, actor) == -1)
                        list = xs_list_append(list, actor);
                }
            }
            else
            if (xs_list_in(list, v) == -1)
                list = xs_list_append(list, v);
        }
    }

    return list;
}


int is_msg_public(snac *snac, char *msg)
/* checks if a message is public */
{
    int ret = 0;
    xs *rcpts = recipient_list(snac, msg, 0);
    char *p, *v;

    p = rcpts;
    while (!ret && xs_list_iter(&p, &v)) {
        if (strcmp(v, public_address) == 0)
            ret = 1;
    }

    return ret;
}


void process_tags(const char *content, d_char **n_content, d_char **tag)
/* parses mentions and tags from content */
{
    d_char *nc = xs_str_new(NULL);
    d_char *tl = xs_list_new();
    xs *split;
    char *p, *v;
    int n = 0;

    p = split = xs_regex_split(content, "(@[A-Za-z0-9_]+@[A-Za-z0-9\\.-]+|#[^ ,\\.:;]+)");
    while (xs_list_iter(&p, &v)) {
        if ((n & 0x1)) {
            if (*v == '@') {
                /* query the webfinger about this fellow */
                xs *actor = NULL;
                xs *uid   = NULL;
                int status;

                status = webfinger_request(v + 1, &actor, &uid);

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
                /* store as is by now */
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

    /* generated values */
    if (date && strcmp(date, "@now") == 0)
        date = published = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");

    if (id != NULL) {
        if (strcmp(id, "@dummy") == 0) {
            xs *ntid = tid(0);
            id = did = xs_fmt("%s/d/%s/%s", snac->actor, ntid, type);
        }
        else
        if (strcmp(id, "@object") == 0) {
            if (object != NULL)
                id = did = xs_fmt("%s/%s", xs_dict_get(object, "id"), type);
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

    /* call the object */
    timeline_request(snac, object, snac->actor);

    if ((a_msg = timeline_find(snac, object)) != NULL) {
        xs *rcpts = xs_list_new();

        msg = msg_base(snac, type, "@dummy", snac->actor, "@now", object);

        rcpts = xs_list_append(rcpts, public_address);
        rcpts = xs_list_append(rcpts, xs_dict_get(a_msg, "attributedTo"));

        msg = xs_dict_append(msg, "to",     rcpts);
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


d_char *msg_note(snac *snac, char *content, char *rcpts, char *in_reply_to, char *attach)
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
    d_char *msg = msg_base(snac, "Note", id, NULL, "@now", NULL);
    char *p, *v;

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
    process_tags(fc2, &fc1, &tag);

    if (in_reply_to != NULL) {
        xs *p_msg = NULL;

        /* demand this thing */
        timeline_request(snac, in_reply_to, NULL);

        if ((p_msg = timeline_find(snac, in_reply_to)) != NULL) {
            /* add this author as recipient */
            char *v;

            if ((v = xs_dict_get(p_msg, "attributedTo")) && xs_list_in(to, v) == -1)
                to = xs_list_append(to, v);

            if ((v = xs_dict_get(p_msg, "context")))
                ctxt = xs_dup(v);

            /* if this message is public, ours will also be */
            if (is_msg_public(snac, p_msg) &&
                xs_list_in(to, public_address) == -1)
                to = xs_list_append(to, public_address);
        }

        irt = xs_dup(in_reply_to);
    }
    else
        irt = xs_val_new(XSTYPE_NULL);

    /* create the attachment list, if there are any */
    if (!xs_is_null(attach) && *attach != '\0') {
        xs *lsof1 = NULL;

        if (xs_type(attach) == XSTYPE_STRING) {
            lsof1 = xs_list_append(xs_list_new(), attach);
            attach = lsof1;
        }

        atls = xs_list_new();
        while (xs_list_iter(&attach, &v)) {
            xs *d = xs_dict_new();
            char *mime = xs_mime_by_ext(v);

            d = xs_dict_append(d, "mediaType", mime);
            d = xs_dict_append(d, "url",       v);
            d = xs_dict_append(d, "name",      "");
            d = xs_dict_append(d, "type",
                xs_startswith(mime, "image/") ? "Image" : "Document");

            atls = xs_list_append(atls, d);
        }
    }

    if (tag == NULL)
        tag = xs_list_new();

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
    if (xs_list_len(to) == 0)
        to = xs_list_append(to, public_address);

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "summary",      "");
    msg = xs_dict_append(msg, "content",      fc1);
    msg = xs_dict_append(msg, "context",      ctxt);
    msg = xs_dict_append(msg, "url",          id);
    msg = xs_dict_append(msg, "to",           to);
    msg = xs_dict_append(msg, "cc",           cc);
    msg = xs_dict_append(msg, "inReplyTo",    irt);
    msg = xs_dict_append(msg, "tag",          tag);

    if (atls != NULL)
        msg = xs_dict_append(msg, "attachment", atls);

    return msg;
}


void notify(snac *snac, char *type, char *utype, char *actor, char *msg)
/* notifies the user of relevant events */
{
    char *email  = xs_dict_get(snac->config, "email");
    char *object = NULL;

    /* no email address? done */
    if (xs_is_null(email) || *email == '\0')
        return;

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

    snac_debug(snac, 1, xs_fmt("notify(%s, %s, %s)", type, utype, actor));

    /* prepare message */

    xs *subject = xs_fmt("snac notify for @%s@%s",
                    xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"));
    xs *from    = xs_fmt("snac-daemon <snac-daemon@%s>", xs_dict_get(srv_config, "host"));
    xs *header  = xs_fmt(
                    "From: %s\n"
                    "To: %s\n"
                    "Subject: %s\n"
                    "\n",
                    from, email, subject);

    xs *body = xs_str_new(header);

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

    enqueue_email(snac, body, 0);
}


/** queues **/

int process_message(snac *snac, char *msg, char *req)
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
    if (!check_signature(snac, req)) {
        snac_log(snac, xs_fmt("bad signature"));
        return 1;
    }

    if (strcmp(type, "Follow") == 0) {
        xs *f_msg = xs_dup(msg);
        xs *reply = msg_accept(snac, f_msg, actor);

        post(snac, reply);

        if (xs_is_null(xs_dict_get(f_msg, "published"))) {
            /* add a date if it doesn't include one (Mastodon) */
            xs *date = xs_str_utctime(0, "%Y-%m-%dT%H:%M:%SZ");
            f_msg = xs_dict_set(f_msg, "published", date);
        }

        timeline_add(snac, xs_dict_get(f_msg, "id"), f_msg, NULL, NULL);

        follower_add(snac, actor, f_msg);

        snac_log(snac, xs_fmt("New follower %s", actor));
        do_notify = 1;
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

                timeline_request(snac, in_reply_to, NULL);

                if (timeline_add(snac, id, object, in_reply_to, NULL)) {
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

        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        timeline_request(snac, object, actor);

        if ((a_msg = timeline_find(snac, object)) != NULL) {
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
            snac_log(snac, xs_fmt("ignored 'Update' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Delete") == 0) {
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        if (valid_status(timeline_del(snac, object)))
            snac_log(snac, xs_fmt("New 'Delete' %s %s", actor, object));
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Delete' for unknown object %s", object));
    }
    else
        snac_debug(snac, 1, xs_fmt("process_message type '%s' ignored", type));

    if (do_notify)
        notify(snac, type, utype, actor, msg);

    return 1;
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

        if (q_item == NULL) {
            snac_log(snac, xs_fmt("process_queue q_item error"));
            continue;
        }

        if ((type = xs_dict_get(q_item, "type")) == NULL)
            type = "output";

        if (strcmp(type, "output") == 0) {
            int status;
            char *actor = xs_dict_get(q_item, "actor");
            char *msg   = xs_dict_get(q_item, "object");
            int retries = xs_number_get(xs_dict_get(q_item, "retries"));
            xs *payload = NULL;
            int p_size = 0;

            /* deliver */
            status = send_to_actor(snac, actor, msg, &payload, &p_size);

            if (!valid_status(status)) {
                /* error sending; requeue? */
                if (retries > queue_retry_max)
                    snac_log(snac, xs_fmt("process_queue giving up %s %d", actor, status));
                else {
                    /* requeue */
                    enqueue_output(snac, msg, actor, retries + 1);
                    snac_log(snac, xs_fmt("process_queue requeue %s %d", actor, retries + 1));
                }
            }
            else
                snac_log(snac, xs_fmt("process_queue sent to actor %s %d", actor, status));
        }
        else
        if (strcmp(type, "input") == 0) {
            /* process the message */
            char *msg = xs_dict_get(q_item, "object");
            char *req = xs_dict_get(q_item, "req");
            int retries = xs_number_get(xs_dict_get(q_item, "retries"));

            if (!process_message(snac, msg, req)) {
                if (retries > queue_retry_max)
                    snac_log(snac, xs_fmt("process_queue input giving up"));
                else {
                    /* reenqueue */
                    enqueue_input(snac, msg, req, retries + 1);
                    snac_log(snac, xs_fmt("process_queue input requeue %d", retries + 1));
                }
            }
        }
        else
        if (strcmp(type, "email") == 0) {
            /* send this email */
            char *msg   = xs_dict_get(q_item, "message");
            int retries = xs_number_get(xs_dict_get(q_item, "retries"));
            FILE *f;
            int ok = 0;

            if ((f = popen("/usr/sbin/sendmail -t", "w")) != NULL) {
                fprintf(f, "%s\n", msg);

                if (fclose(f) != EOF)
                    ok = 1;
            }

            if (ok)
                snac_debug(snac, 1, xs_fmt("email message sent"));
            else {
                if (retries > queue_retry_max)
                    snac_log(snac, xs_fmt("process_queue email giving up (errno: %d)", errno));
                else {
                    /* requeue */
                    snac_log(snac, xs_fmt(
                        "process_queue email requeue %d (errno: %d)", retries + 1, errno));

                    enqueue_email(snac, msg, retries + 1);
                }
            }
        }
    }
}


void post(snac *snac, char *msg)
/* enqueues a message to all its recipients */
{
    xs *rcpts = recipient_list(snac, msg, 1);
    char *p, *v;

    p = rcpts;
    while (xs_list_iter(&p, &v)) {
        enqueue_output(snac, msg, v, 0);
    }
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
        srv_log(xs_fmt("activitypub_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    *ctype  = "application/activity+json";

    if (p_path == NULL) {
        /* if there was no component after the user, it's an actor request */
        msg = msg_actor(&snac);
        *ctype = "application/ld+json";
    }
    else
    if (strcmp(p_path, "outbox") == 0) {
        xs *id = xs_fmt("%s/outbox", snac.actor);
        xs *elems = local_list(&snac, 20);
        xs *list = xs_list_new();
        msg = msg_collection(&snac, id);
        char *p, *v;

        p = elems;
        while (xs_list_iter(&p, &v)) {
            xs *i = timeline_get(&snac, v);
            char *type = xs_dict_get(i, "type");
            char *id   = xs_dict_get(i, "id");

            if (type && id && strcmp(type, "Note") == 0 && xs_startswith(id, snac.actor)) {
                i = xs_dict_del(i, "_snac");
                list = xs_list_append(list, i);
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

        if ((msg = timeline_find(&snac, id)) != NULL)
            msg = xs_dict_del(msg, "_snac");
        else
            status = 404;
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

    /* if it has a digest, check it now, because
       later the payload won't be exactly the same */
    if ((v = xs_dict_get(req, "digest")) != NULL) {
        xs *s1 = xs_sha256_base64(payload, p_size);
        xs *s2 = xs_fmt("SHA-256=%s", s1);

        if (strcmp(s2, v) != 0) {
            srv_log(xs_fmt("digest check FAILED"));
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
