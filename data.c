/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_openssl.h"
#include "xs_glob.h"

#include "snac.h"

#include <time.h>
#include <glob.h>
#include <sys/stat.h>


int srv_open(char *basedir)
/* opens a server */
{
    int ret = 0;
    xs *cfg_file = NULL;
    FILE *f;
    d_char *error = NULL;

    srv_basedir = xs_str_new(basedir);

    if (xs_endswith(srv_basedir, "/"))
        srv_basedir = xs_crop(srv_basedir, 0, -1);

    cfg_file = xs_fmt("%s/server.json", basedir);

    if ((f = fopen(cfg_file, "r")) == NULL)
        error = xs_fmt("ERROR: cannot opening '%s'", cfg_file);
    else {
        xs *cfg_data;

        /* read full config file */
        cfg_data = xs_readall(f);
        fclose(f);

        /* parse */
        srv_config = xs_json_loads(cfg_data);

        if (srv_config == NULL)
            error = xs_fmt("ERROR: cannot parse '%s'", cfg_file);
        else {
            char *host;
            char *prefix;
            char *dbglvl;
            char *layout;
            double f = 0.0;

            host   = xs_dict_get(srv_config, "host");
            prefix = xs_dict_get(srv_config, "prefix");
            dbglvl = xs_dict_get(srv_config, "dbglevel");
            layout = xs_dict_get(srv_config, "layout");

            if (host == NULL || prefix == NULL)
                error = xs_str_new("ERROR: cannot get server data");
            else {
                srv_baseurl = xs_fmt("https://%s%s", host, prefix);

                dbglevel = (int) xs_number_get(dbglvl);

                if ((dbglvl = getenv("DEBUG")) != NULL) {
                    dbglevel = atoi(dbglvl);
                    error = xs_fmt("DEBUG level set to %d from environment", dbglevel);
                }

                if (!layout || (f = xs_number_get(layout)) != 2.0)
                    error = xs_fmt("ERROR: unsupported old disk layout %f\n", f);
                else
                    ret = 1;
            }

        }
    }

    if (ret == 0 && error != NULL)
        srv_log(error);

    return ret;
}


void user_free(snac *snac)
/* frees a user snac */
{
    free(snac->uid);
    free(snac->basedir);
    free(snac->config);
    free(snac->key);
    free(snac->actor);
}


int user_open(snac *snac, char *uid)
/* opens a user */
{
    int ret = 0;

    memset(snac, '\0', sizeof(struct _snac));

    if (validate_uid(uid)) {
        xs *cfg_file;
        FILE *f;

        snac->uid = xs_str_new(uid);

        snac->basedir = xs_fmt("%s/user/%s", srv_basedir, uid);

        cfg_file = xs_fmt("%s/user.json", snac->basedir);

        if ((f = fopen(cfg_file, "r")) != NULL) {
            xs *cfg_data;

            /* read full config file */
            cfg_data = xs_readall(f);
            fclose(f);

            if ((snac->config = xs_json_loads(cfg_data)) != NULL) {
                xs *key_file = xs_fmt("%s/key.json", snac->basedir);

                if ((f = fopen(key_file, "r")) != NULL) {
                    xs *key_data;

                    key_data = xs_readall(f);
                    fclose(f);

                    if ((snac->key = xs_json_loads(key_data)) != NULL) {
                        snac->actor = xs_fmt("%s/%s", srv_baseurl, uid);
                        ret = 1;
                    }
                    else
                        srv_log(xs_fmt("cannot parse '%s'", key_file));
                }
                else
                    srv_log(xs_fmt("error opening '%s'", key_file));
            }
            else
                srv_log(xs_fmt("cannot parse '%s'", cfg_file));
        }
        else
            srv_debug(2, xs_fmt("error opening '%s'", cfg_file));
    }
    else
        srv_log(xs_fmt("invalid user '%s'", uid));

    if (!ret)
        user_free(snac);

    return ret;
}


d_char *user_list(void)
/* returns the list of user ids */
{
    xs *spec = xs_fmt("%s/user/" "*", srv_basedir);
    return xs_glob(spec, 1, 0);
}


double mtime(char *fn)
/* returns the mtime of a file or directory, or 0.0 */
{
    struct stat st;
    double r = 0.0;

    if (fn && stat(fn, &st) != -1)
        r = (double)st.st_mtim.tv_sec;

    return r;
}


d_char *_follower_fn(snac *snac, char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/followers/%s.json", snac->basedir, md5);
}


int follower_add(snac *snac, char *actor, char *msg)
/* adds a follower */
{
    int ret = 201; /* created */
    xs *fn = _follower_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, 1, strlen(j), f);
        fclose(f);
    }
    else
        ret = 500;

    snac_debug(snac, 2, xs_fmt("follower_add %s %s", actor, fn));

    return ret;
}


int follower_del(snac *snac, char *actor)
/* deletes a follower */
{
    int status = 200;
    xs *fn = _follower_fn(snac, actor);

    if (fn != NULL)
        unlink(fn);
    else
        status = 404;

    snac_debug(snac, 2, xs_fmt("follower_del %s %s", actor, fn));

    return status;
}


int follower_check(snac *snac, char *actor)
/* checks if someone is a follower */
{
    xs *fn = _follower_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


d_char *follower_list(snac *snac)
/* returns the list of followers */
{
    xs *spec = xs_fmt("%s/followers/" "*.json", snac->basedir);
    xs *glist = xs_glob(spec, 0, 0);
    char *p, *v;
    d_char *list = xs_list_new();

    /* iterate the list of files */
    p = glist;
    while (xs_list_iter(&p, &v)) {
        FILE *f;

        /* load the follower data */
        if ((f = fopen(v, "r")) != NULL) {
            xs *j = xs_readall(f);
            fclose(f);

            if (j != NULL) {
                xs *o = xs_json_loads(j);

                if (o != NULL)
                    list = xs_list_append(list, o);
            }
        }
    }

    return list;
}


double timeline_mtime(snac *snac)
{
    xs *fn = xs_fmt("%s/timeline", snac->basedir);
    return mtime(fn);
}


d_char *_timeline_find_fn(snac *snac, char *id)
/* returns the file name of a timeline entry by its id */
{
    xs *md5  = xs_md5_hex(id, strlen(id));
    xs *spec = xs_fmt("%s/timeline/" "*-%s.json", snac->basedir, md5);
    xs *list = NULL;
    d_char *fn = NULL;
    int l;

    list = xs_glob(spec, 0, 0);
    l = xs_list_len(list);

    /* if there is something, get the first one */
    if (l > 0) {
        fn = xs_str_new(xs_list_get(list, 0));

        if (l > 1)
            snac_log(snac, xs_fmt("**ALERT** _timeline_find_fn %d > 1", l));
    }

    return fn;
}


int timeline_here(snac *snac, char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _timeline_find_fn(snac, id);

    return fn != NULL;
}


d_char *timeline_find(snac *snac, char *id)
/* gets a message from the timeline by id */
{
    xs *fn      = _timeline_find_fn(snac, id);
    d_char *msg = NULL;

    if (fn != NULL) {
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            xs *j = xs_readall(f);

            msg = xs_json_loads(j);
            fclose(f);
        }
    }

    return msg;
}


void timeline_del(snac *snac, char *id)
/* deletes a message from the timeline */
{
    xs *fn = _timeline_find_fn(snac, id);

    if (fn != NULL) {
        xs *lfn = NULL;

        unlink(fn);
        snac_debug(snac, 1, xs_fmt("timeline_del %s", id));

        /* try to delete also from the local timeline */
        lfn = xs_replace(fn, "/timeline/", "/local/");

        if (unlink(lfn) != -1)
            snac_debug(snac, 1, xs_fmt("timeline_del (local) %s", id));
    }
}


d_char *timeline_get(snac *snac, char *fn)
/* gets a timeline entry by file name */
{
    d_char *d = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);

        d = xs_json_loads(j);
        fclose(f);
    }

    return d;
}


d_char *_timeline_list(snac *snac, char *directory, int max)
/* returns a list of the timeline filenames */
{
    xs *spec = xs_fmt("%s/%s/" "*.json", snac->basedir, directory);
    int c_max;

    /* maximum number of items in the timeline */
    c_max = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));

    /* never more timeline entries than the configured maximum */
    if (max > c_max)
        max = c_max;

    return xs_glob_n(spec, 0, 1, max);
}


d_char *timeline_list(snac *snac, int max)
{
    return _timeline_list(snac, "timeline", max);
}


d_char *local_list(snac *snac, int max)
{
    return _timeline_list(snac, "local", max);
}


d_char *_timeline_new_fn(snac *snac, char *id)
/* creates a new filename */
{
    xs *ntid = tid(0);
    xs *md5  = xs_md5_hex(id, strlen(id));

    return xs_fmt("%s/timeline/%s-%s.json", snac->basedir, ntid, md5);
}


void _timeline_write(snac *snac, char *id, char *msg, char *parent, char *referrer)
/* writes a timeline entry and refreshes the ancestors */
{
    xs *fn = _timeline_new_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, strlen(j), 1, f);
        fclose(f);

        snac_debug(snac, 1, xs_fmt("_timeline_write %s %s", id, fn));
    }

    /* related to this user? link to local timeline */
    if (xs_startswith(id, snac->actor) ||
        (!xs_is_null(parent) && xs_startswith(parent, snac->actor)) ||
        (!xs_is_null(referrer) && xs_startswith(referrer, snac->actor))) {
        xs *lfn = xs_replace(fn, "/timeline/", "/local/");
        link(fn, lfn);

        snac_debug(snac, 1, xs_fmt("_timeline_write (local) %s %s", id, lfn));
    }

    if (!xs_is_null(parent)) {
        /* update the parent, adding this id to its children list */
        xs *pfn   = _timeline_find_fn(snac, parent);
        xs *p_msg = NULL;

        if (pfn != NULL && (f = fopen(pfn, "r")) != NULL) {
            xs *j;

            j = xs_readall(f);
            fclose(f);

            p_msg = xs_json_loads(j);
        }

        if (p_msg == NULL)
            return;

        xs *meta     = xs_dup(xs_dict_get(p_msg, "_snac"));
        xs *children = xs_dup(xs_dict_get(meta,  "children"));

        /* add the child if it's not already there */
        if (xs_list_in(children, id) == -1)
            children = xs_list_append(children, id);

        /* re-store */
        meta  = xs_dict_set(meta,  "children", children);
        p_msg = xs_dict_set(p_msg, "_snac",    meta);

        xs *nfn = _timeline_new_fn(snac, parent);

        if ((f = fopen(nfn, "w")) != NULL) {
            xs *j = xs_json_dumps_pp(p_msg, 4);

            fwrite(j, strlen(j), 1, f);
            fclose(f);

            unlink(pfn);

            snac_debug(snac, 1,
                xs_fmt("_timeline_write updated parent %s %s", parent, nfn));

            /* try to do the same with the local */
            xs *olfn = xs_replace(pfn, "/timeline/", "/local/");

            if (unlink(olfn) != -1 || xs_startswith(id, snac->actor)) {
                xs *nlfn = xs_replace(nfn, "/timeline/", "/local/");

                link(nfn, nlfn);

                snac_debug(snac, 1,
                    xs_fmt("_timeline_write updated parent (local) %s %s", parent, nlfn));
            }
        }
        else
            return;

        /* now iterate all parents up, just renaming the files */
        xs *grampa = xs_dup(xs_dict_get(meta, "parent"));

        while (!xs_is_null(grampa)) {
            xs *gofn = _timeline_find_fn(snac, grampa);

            if (gofn == NULL)
                break;

            /* create the new filename */
            xs *gnfn = _timeline_new_fn(snac, grampa);

            rename(gofn, gnfn);

            snac_debug(snac, 1,
                xs_fmt("_timeline_write updated grampa %s %s", grampa, gnfn));

            /* try to do the same with the local */
            xs *golfn = xs_replace(gofn, "/timeline/", "/local/");

            if (unlink(golfn) != -1) {
                xs *gnlfn = xs_replace(gnfn, "/timeline/", "/local/");

                link(gnfn, gnlfn);

                snac_debug(snac, 1,
                    xs_fmt("_timeline_write updated grampa (local) %s %s", parent, gnlfn));
            }

            /* now open it and get its own parent */
            if ((f = fopen(gnfn, "r")) != NULL) {
                xs *j = xs_readall(f);
                fclose(f);

                xs *g_msg    = xs_json_loads(j);
                d_char *meta = xs_dict_get(g_msg, "_snac");
                d_char *p    = xs_dict_get(meta,  "parent");

                free(grampa);
                grampa = xs_dup(p);
            }
        }
    }
}


int timeline_add(snac *snac, char *id, char *o_msg, char *parent, char *referrer)
/* adds a message to the timeline */
{
    xs *pfn = _timeline_find_fn(snac, id);

    if (pfn != NULL) {
        snac_log(snac, xs_fmt("timeline_add refusing rewrite %s %s", id, pfn));
        return 0;
    }

    xs *msg = xs_dup(o_msg);
    xs *md;

    /* add new metadata */
    md = xs_json_loads("{"
        "\"children\":     [],"
        "\"liked_by\":     [],"
        "\"announced_by\": [],"
        "\"version\":      \"" USER_AGENT "\","
        "\"referrer\":     null,"
        "\"parent\":       null"
    "}");

    if (!xs_is_null(parent))
        md = xs_dict_set(md, "parent", parent);

    if (!xs_is_null(referrer))
        md = xs_dict_set(md, "referrer", referrer);

    msg = xs_dict_set(msg, "_snac", md);

    _timeline_write(snac, id, msg, parent, referrer);

    snac_debug(snac, 1, xs_fmt("timeline_add %s", id));

    return 1;
}



void timeline_admire(snac *snac, char *id, char *admirer, int like)
/* updates a timeline entry with a new admiration */
{
    xs *ofn = _timeline_find_fn(snac, id);
    FILE *f;

    if (ofn != NULL && (f = fopen(ofn, "r")) != NULL) {
        xs *j1 = xs_readall(f);
        fclose(f);

        xs *msg  = xs_json_loads(j1);
        xs *meta = xs_dup(xs_dict_get(msg, "_snac"));
        xs *list;

        if (like)
            list = xs_dup(xs_dict_get(meta, "liked_by"));
        else
            list = xs_dup(xs_dict_get(meta, "announced_by"));

        /* add the admirer if it's not already there */
        if (xs_list_in(list, admirer) == -1)
            list = xs_list_append(list, admirer);

        /* set the admirer as the referrer (if not already set) */
        if (!like && xs_is_null(xs_dict_get(meta, "referrer")))
            meta = xs_dict_set(meta, "referrer", admirer);

        /* re-store */
        if (like)
            meta = xs_dict_set(meta, "liked_by", list);
        else
            meta = xs_dict_set(meta, "announced_by", list);

        msg = xs_dict_set(msg, "_snac", meta);

        unlink(ofn);
        ofn = xs_replace_i(ofn, "/timeline/", "/local/");
        unlink(ofn);

        _timeline_write(snac, id, msg, xs_dict_get(meta, "parent"), like ? NULL : admirer);

        snac_debug(snac, 1, xs_fmt("timeline_admire (%s) %s %s",
            like ? "Like" : "Announce", id, admirer));
    }
    else
        snac_log(snac, xs_fmt("timeline_admire ignored for unknown object %s", id));
}


d_char *_following_fn(snac *snac, char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/following/%s.json", snac->basedir, md5);
}


int following_add(snac *snac, char *actor, char *msg)
/* adds to the following list */
{
    int ret = 201; /* created */
    xs *fn = _following_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, 1, strlen(j), f);
        fclose(f);
    }
    else
        ret = 500;

    snac_debug(snac, 2, xs_fmt("following_add %s %s", actor, fn));

    return ret;
}


int following_del(snac *snac, char *actor)
/* someone is no longer following us */
{
    xs *fn = _following_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("following_del %s %s", actor, fn));

    return 200;
}


int following_check(snac *snac, char *actor)
/* checks if someone is following us */
{
    xs *fn = _following_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


int following_get(snac *snac, char *actor, d_char **data)
/* returns the 'Follow' object */
{
    xs *fn = _following_fn(snac, actor);
    FILE *f;
    int status = 200;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);

        fclose(f);

        *data = xs_json_loads(j);
    }
    else
        status = 404;

    return status;
}


d_char *_muted_fn(snac *snac, char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/muted/%s.json", snac->basedir, md5);
}


void mute(snac *snac, char *actor)
/* mutes a moron */
{
    xs *fn = _muted_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%s\n", actor);
        fclose(f);

        snac_debug(snac, 2, xs_fmt("muted %s %s", actor, fn));
    }
}


void unmute(snac *snac, char *actor)
/* actor is no longer a moron */
{
    xs *fn = _muted_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("unmuted %s %s", actor, fn));
}


int is_muted(snac *snac, char *actor)
/* check if someone is muted */
{
    xs *fn = _muted_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


d_char *_actor_fn(snac *snac, char *actor)
/* returns the file name for an actor */
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/actors/%s.json", snac->basedir, md5);
}


int actor_add(snac *snac, char *actor, char *msg)
/* adds an actor */
{
    int ret = 201; /* created */
    xs *fn = _actor_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, 1, strlen(j), f);
        fclose(f);
    }
    else
        ret = 500;

    snac_debug(snac, 2, xs_fmt("actor_add %s %s", actor, fn));

    return ret;
}


int actor_get(snac *snac, char *actor, d_char **data)
/* returns an already downloaded actor */
{
    xs *fn = _actor_fn(snac, actor);
    double t;
    double max_time;
    int status;
    FILE *f;

    t = mtime(fn);

    /* no mtime? there is nothing here */
    if (t == 0.0)
        return 404;

    /* maximum time for the actor data to be considered stale */
    max_time = 3600.0 * 36.0;

    if (t + max_time < (double) time(NULL)) {
        /* actor data exists but also stinks */

        if ((f = fopen(fn, "a")) != NULL) {
            /* write a blank at the end to 'touch' the file */
            fwrite(" ", 1, 1, f);
            fclose(f);
        }

        status = 205; /* "205: Reset Content" "110: Response Is Stale" */
    }
    else {
        /* it's still valid */
        status = 200;
    }

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);

        fclose(f);

        if (data)
            *data = xs_json_loads(j);
    }
    else
        status = 500;

    return status;
}


d_char *_static_fn(snac *snac, const char *id)
/* gets the filename for a static file */
{
    return xs_fmt("%s/static/%s", snac->basedir, id);
}


int static_get(snac *snac, const char *id, d_char **data, int *size)
/* returns static content */
{
    xs *fn = _static_fn(snac, id);
    FILE *f;
    int status = 404;

    *size = 0xfffffff;

    if ((f = fopen(fn, "rb")) != NULL) {
        *data = xs_read(f, size);
        fclose(f);

        status = 200;
    }

    return status;
}


void static_put(snac *snac, const char *id, const char *data, int size)
/* writes status content */
{
    xs *fn = _static_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "wb")) != NULL) {
        fwrite(data, size, 1, f);
        fclose(f);
    }
}


d_char *_history_fn(snac *snac, char *id)
/* gets the filename for the history */
{
    return xs_fmt("%s/history/%s", snac->basedir, id);
}


double history_mtime(snac *snac, char * id)
{
    double t = 0.0;
    xs *fn = _history_fn(snac, id);

    if (fn != NULL)
        t = mtime(fn);

    return t;
}


void history_add(snac *snac, char *id, char *content, int size)
/* adds something to the history */
{
    xs *fn = _history_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fwrite(content, size, 1, f);
        fclose(f);
    }
}


d_char *history_get(snac *snac, char *id)
{
    d_char *content = NULL;
    xs *fn = _history_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        content = xs_readall(f);
        fclose(f);
    }

    return content;
}


int history_del(snac *snac, char *id)
{
    xs *fn = _history_fn(snac, id);
    return unlink(fn);
}


d_char *history_list(snac *snac)
{
    xs *spec = xs_fmt("%s/history/" "*.html", snac->basedir);

    return xs_glob(spec, 1, 0);
}


static int _enqueue_put(char *fn, char *msg)
/* writes safely to the queue */
{
    int ret = 1;
    xs *tfn = xs_fmt("%s.tmp", fn);
    FILE *f;

    if ((f = fopen(tfn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, strlen(j), 1, f);
        fclose(f);

        rename(tfn, fn);
    }
    else
        ret = 0;

    return ret;
}


void enqueue_input(snac *snac, char *msg, char *req, int retries)
/* enqueues an input message */
{
    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *fn   = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);
    xs *qmsg = xs_dict_new();
    xs *rn   = xs_number_new(retries);

    qmsg = xs_dict_append(qmsg, "type",    "input");
    qmsg = xs_dict_append(qmsg, "object",  msg);
    qmsg = xs_dict_append(qmsg, "req",     req);
    qmsg = xs_dict_append(qmsg, "retries", rn);

    _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_input %s", fn));
}


void enqueue_output(snac *snac, char *msg, char *actor, int retries)
/* enqueues an output message for an actor */
{
    if (strcmp(actor, snac->actor) == 0) {
        snac_debug(snac, 1, xs_str_new("enqueue refused to myself"));
        return;
    }

    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *fn   = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);
    xs *qmsg = xs_dict_new();
    xs *rn   = xs_number_new(retries);

    qmsg = xs_dict_append(qmsg, "type",    "output");
    qmsg = xs_dict_append(qmsg, "actor",   actor);
    qmsg = xs_dict_append(qmsg, "object",  msg);
    qmsg = xs_dict_append(qmsg, "retries", rn);

    _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_output %s %s %d", actor, fn, retries));
}


d_char *queue(snac *snac)
/* returns a list with filenames that can be dequeued */
{
    xs *spec = xs_fmt("%s/queue/" "*.json", snac->basedir);
    d_char *list = xs_list_new();
    glob_t globbuf;
    time_t t = time(NULL);

    if (glob(spec, 0, NULL, &globbuf) == 0) {
        int n;
        char *p;

        for (n = 0; (p = globbuf.gl_pathv[n]) != NULL; n++) {
            /* get the retry time from the basename */
            char *bn = strrchr(p, '/');
            time_t t2 = atol(bn + 1);

            if (t2 > t)
                snac_debug(snac, 2, xs_fmt("queue not yet time for %s", p));
            else {
                list = xs_list_append(list, p);
                snac_debug(snac, 2, xs_fmt("queue ready for %s", p));
            }
        }
    }

    globfree(&globbuf);

    return list;
}


d_char *dequeue(snac *snac, char *fn)
/* dequeues a message */
{
    FILE *f;
    d_char *obj = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        /* delete right now */
        unlink(fn);

        xs *j = xs_readall(f);
        obj = xs_json_loads(j);

        fclose(f);
    }

    return obj;
}


void purge(snac *snac)
/* do the purge */
{
    int tpd = xs_number_get(xs_dict_get(srv_config, "timeline_purge_days"));

    /* purge days set to 0? disable purging */
    if (tpd == 0) {
        /* well, enjoy your data drive exploding */
        return;
    }

    time_t mt  = time(NULL) - tpd * 24 * 3600;
    xs *t_spec = xs_fmt("%s/timeline/" "*.json", snac->basedir);
    xs *t_list = xs_glob(t_spec, 0, 0);
    char *p, *v;

    p = t_list;
    while (xs_list_iter(&p, &v)) {
        if (mtime(v) < mt) {
            /* older than the minimum time: delete it */
            unlink(v);
            snac_debug(snac, 1, xs_fmt("purged %s", v));
        }
    }

    xs *a_spec = xs_fmt("%s/actors/" "*.json", snac->basedir);
    xs *a_list = xs_glob(a_spec, 0, 0);

    p = a_list;
    while (xs_list_iter(&p, &v)) {
        if (mtime(v) < mt) {
            /* older than the minimum time: delete it */
            unlink(v);
            snac_debug(snac, 1, xs_fmt("purged %s", v));
        }
    }
}


void purge_all(void)
/* purge all users */
{
    snac snac;
    xs *list = user_list();
    char *p, *uid;

    p = list;
    while (xs_list_iter(&p, &uid)) {
        if (user_open(&snac, uid)) {
            purge(&snac);
            user_free(&snac);
        }
    }
}
