/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_openssl.h"
#include "xs_glob.h"
#include "xs_set.h"

#include "snac.h"

#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

double db_layout = 2.7;


int db_upgrade(d_char **error);

int srv_open(char *basedir, int auto_upgrade)
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

            host   = xs_dict_get(srv_config, "host");
            prefix = xs_dict_get(srv_config, "prefix");
            dbglvl = xs_dict_get(srv_config, "dbglevel");

            if (host == NULL || prefix == NULL)
                error = xs_str_new("ERROR: cannot get server data");
            else {
                srv_baseurl = xs_fmt("https://%s%s", host, prefix);

                dbglevel = (int) xs_number_get(dbglvl);

                if ((dbglvl = getenv("DEBUG")) != NULL) {
                    dbglevel = atoi(dbglvl);
                    error = xs_fmt("DEBUG level set to %d from environment", dbglevel);
                }

                if (auto_upgrade)
                    ret = db_upgrade(&error);
                else {
                    if (xs_number_get(xs_dict_get(srv_config, "layout")) < db_layout)
                        error = xs_fmt("ERROR: disk layout changed - execute 'snac upgrade' first");
                    else
                        ret = 1;
                }
            }

        }
    }

    if (error != NULL)
        srv_log(error);

/* disabled temporarily; messages can't be sent (libcurl issue?) */
#if 0
#ifdef __OpenBSD__
    srv_debug(2, xs_fmt("Calling unveil()"));
    unveil(basedir,     "rwc");
    unveil("/usr/sbin", "x");
    unveil(NULL,        NULL);
#endif /* __OpenBSD__ */
#endif

    return ret;
}


void srv_free(void)
{
    xs_free(srv_basedir);
    xs_free(srv_config);
    xs_free(srv_baseurl);
}


void user_free(snac *snac)
/* frees a user snac */
{
    xs_free(snac->uid);
    xs_free(snac->basedir);
    xs_free(snac->config);
    xs_free(snac->key);
    xs_free(snac->actor);
    xs_free(snac->md5);
}


int user_open(snac *snac, const char *uid)
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
                        snac->md5   = xs_md5_hex(snac->actor, strlen(snac->actor));
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


double mtime_nl(const char *fn, int *n_link)
/* returns the mtime and number of links of a file or directory, or 0.0 */
{
    struct stat st;
    double r = 0.0;
    int n = 0;

    if (fn && stat(fn, &st) != -1) {
        r = (double) st.st_mtim.tv_sec;
        n = st.st_nlink;
    }

    if (n_link)
        *n_link = n;

    return r;
}


/** database 2.1+ **/

/** indexes **/

int index_add_md5(const char *fn, const char *md5)
/* adds an md5 to an index */
{
    int status = 201; /* Created */
    FILE *f;

    if ((f = fopen(fn, "a")) != NULL) {
        flock(fileno(f), LOCK_EX);

        /* ensure the position is at the end after getting the lock */
        fseek(f, 0, SEEK_END);

        fprintf(f, "%s\n", md5);
        fclose(f);
    }
    else
        status = 500;

    return status;
}


int index_add(const char *fn, const char *id)
/* adds an id to an index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_add_md5(fn, md5);
}


int index_del_md5(const char *fn, const char *md5)
/* deletes an md5 from an index */
{
    int status = 404;
    FILE *i, *o;

    if ((i = fopen(fn, "r")) != NULL) {
        flock(fileno(i), LOCK_EX);

        xs *nfn = xs_fmt("%s.new", fn);
        char line[256];

        if ((o = fopen(nfn, "w")) != NULL) {
            while (fgets(line, sizeof(line), i) != NULL) {
                line[32] = '\0';
                if (memcmp(line, md5, 32) != 0)
                    fprintf(o, "%s\n", line);
            }

            fclose(o);

            xs *ofn = xs_fmt("%s.bak", fn);

            link(fn, ofn);
            rename(nfn, fn);
        }
        else
            status = 500;

        fclose(i);
    }
    else
        status = 500;

    return status;
}


int index_del(const char *fn, const char *id)
/* deletes an id from an index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_del_md5(fn, md5);
}


int index_in_md5(const char *fn, const char *md5)
/* checks if the md5 is already in the index */
{
    FILE *f;
    int ret = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        char line[256];

        while (!ret && fgets(line, sizeof(line), f) != NULL) {
            line[32] = '\0';

            if (strcmp(line, md5) == 0)
                ret = 1;
        }

        fclose(f);
    }

    return ret;
}


int index_in(const char *fn, const char *id)
/* checks if the object id is already in the index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_in_md5(fn, md5);
}


int index_first(const char *fn, char *line, int size)
/* reads the first entry of an index */
{
    FILE *f;
    int ret = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        if (fgets(line, size, f) != NULL) {
            line[32] = '\0';
            ret = 1;
        }

        fclose(f);
    }

    return ret;
}


int index_len(const char *fn)
/* returns the number of elements in an index */
{
    struct stat st;
    int len = 0;

    if (stat(fn, &st) != -1)
        len = st.st_size / 33;

    return len;
}


d_char *index_list(const char *fn, int max)
/* returns an index as a list */
{
    d_char *list = NULL;
    FILE *f;
    int n = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        char line[256];
        list = xs_list_new();

        while (n < max && fgets(line, sizeof(line), f) != NULL) {
            line[32] = '\0';
            list = xs_list_append(list, line);
            n++;
        }

        fclose(f);
    }

    return list;
}


d_char *index_list_desc(const char *fn, int skip, int show)
/* returns an index as a list, in reverse order */
{
    d_char *list = NULL;
    FILE *f;
    int n = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        char line[256];
        list = xs_list_new();

        /* move to the end minus one entry (or more, if skipping entries) */
        if (!fseek(f, 0, SEEK_END) && !fseek(f, (skip + 1) * -33, SEEK_CUR)) {
            while (n < show && fgets(line, sizeof(line), f) != NULL) {
                line[32] = '\0';
                list = xs_list_append(list, line);
                n++;

                /* move backwards 2 entries */
                if (fseek(f, -66, SEEK_CUR) == -1)
                    break;
            }
        }

        fclose(f);
    }

    return list;
}


/** objects **/

d_char *_object_fn_by_md5(const char *md5)
{
    xs *bfn = xs_fmt("%s/object/%c%c", srv_basedir, md5[0], md5[1]);

    mkdir(bfn, 0755);

    return xs_fmt("%s/%s.json", bfn, md5);
}


d_char *_object_fn(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return _object_fn_by_md5(md5);
}


int object_here_by_md5(char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn_by_md5(id);
    return mtime(fn) > 0.0;
}


int object_here(char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn(id);
    return mtime(fn) > 0.0;
}


int object_get_by_md5(const char *md5, d_char **obj, const char *type)
/* returns a stored object, optionally of the requested type */
{
    int status = 404;
    xs *fn     = _object_fn_by_md5(md5);
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        xs *j = xs_readall(f);
        fclose(f);

        *obj = xs_json_loads(j);

        if (*obj) {
            status = 200;

            /* specific type requested? */
            if (!xs_is_null(type)) {
                char *v = xs_dict_get(*obj, "type");

                if (xs_is_null(v) || strcmp(v, type) != 0) {
                    status = 404;
                    *obj   = xs_free(*obj);
                }
            }
        }
    }
    else
        *obj = NULL;

    return status;
}


int object_get(const char *id, d_char **obj, const char *type)
/* returns a stored object, optionally of the requested type */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_get_by_md5(md5, obj, type);
}


int _object_add(const char *id, d_char *obj, int ow)
/* stores an object */
{
    int status = 201; /* Created */
    xs *fn     = _object_fn(id);
    FILE *f;

    if (!ow && mtime(fn) > 0.0) {
        /* object already here */
        srv_debug(1, xs_fmt("object_add object already here %s", id));
        return 204; /* No content */
    }

    if ((f = fopen(fn, "w")) != NULL) {
        flock(fileno(f), LOCK_EX);

        xs *j = xs_json_dumps_pp(obj, 4);

        fwrite(j, strlen(j), 1, f);
        fclose(f);

        /* does this object has a parent? */
        char *in_reply_to = xs_dict_get(obj, "inReplyTo");

        if (!xs_is_null(in_reply_to) && *in_reply_to) {
            /* update the children index of the parent */
            xs *c_idx = _object_fn(in_reply_to);

            c_idx = xs_replace_i(c_idx, ".json", "_c.idx");

            if (!index_in(c_idx, id)) {
                index_add(c_idx, id);
                srv_debug(1, xs_fmt("object_add added child %s to %s", id, c_idx));
            }
            else
                srv_debug(1, xs_fmt("object_add %s child already in %s", id, c_idx));

            /* create a one-element index with the parent */
            xs *p_idx = xs_replace(fn, ".json", "_p.idx");

            if (mtime(p_idx) == 0.0) {
                index_add(p_idx, in_reply_to);
                srv_debug(1, xs_fmt("object_add added parent %s to %s", in_reply_to, p_idx));
            }
        }
    }
    else {
        srv_log(xs_fmt("object_add error writing %s (errno: %d)", fn, errno));
        status = 500;
    }

    srv_debug(1, xs_fmt("object_add %s %s %d", id, fn, status));

    return status;
}


int object_add(const char *id, d_char *obj)
/* stores an object */
{
    return _object_add(id, obj, 0);
}


int object_add_ow(const char *id, d_char *obj)
/* stores an object (overwriting allowed) */
{
    return _object_add(id, obj, 1);
}


int object_del_by_md5(const char *md5)
/* deletes an object by its md5 */
{
    int status = 404;
    xs *fn     = _object_fn_by_md5(md5);

    if (fn != NULL && unlink(fn) != -1) {
        status = 200;

        /* also delete associated indexes */
        xs *spec  = xs_dup(fn);
        spec      = xs_replace_i(spec, ".json", "*.idx");
        xs *files = xs_glob(spec, 0, 0);
        char *p, *v;

        p = files;
        while (xs_list_iter(&p, &v)) {
            srv_debug(1, xs_fmt("object_del index %s", v));
            unlink(v);
        }
    }

    srv_debug(1, xs_fmt("object_del %s %d", fn, status));

    return status;
}


int object_del(const char *id)
/* deletes an object */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_del_by_md5(md5);
}


int object_del_if_unref(const char *id)
/* deletes an object if its n_links < 2 */
{
    xs *fn = _object_fn(id);
    int n_links;
    int ret = 0;

    if (mtime_nl(fn, &n_links) > 0.0 && n_links < 2)
        ret = object_del(id);

    return ret;
}


d_char *_object_index_fn(const char *id, const char *idxsfx)
/* returns the filename of an object's index */
{
    d_char *fn = _object_fn(id);
    return xs_replace_i(fn, ".json", idxsfx);
}


int object_likes_len(const char *id)
/* returns the number of likes (without reading the index) */
{
    xs *fn = _object_index_fn(id, "_l.idx");
    return index_len(fn);
}


int object_announces_len(const char *id)
/* returns the number of announces (without reading the index) */
{
    xs *fn = _object_index_fn(id, "_a.idx");
    return index_len(fn);
}


d_char *object_children(const char *id)
/* returns the list of an object's children */
{
    xs *fn = _object_index_fn(id, "_c.idx");
    return index_list(fn, XS_ALL);
}


d_char *object_likes(const char *id)
{
    xs *fn = _object_index_fn(id, "_l.idx");
    return index_list(fn, XS_ALL);
}


d_char *object_announces(const char *id)
{
    xs *fn = _object_index_fn(id, "_a.idx");
    return index_list(fn, XS_ALL);
}


int object_parent(const char *id, char *buf, int size)
/* returns the object parent, if any */
{
    xs *fn = _object_fn_by_md5(id);
    fn = xs_replace_i(fn, ".json", "_p.idx");
    return index_first(fn, buf, size);
}


int object_admire(const char *id, const char *actor, int like)
/* actor likes or announces this object */
{
    int status = 200;
    xs *fn     = _object_fn(id);

    fn = xs_replace_i(fn, ".json", like ? "_l.idx" : "_a.idx");

    if (!index_in(fn, actor)) {
        status = index_add(fn, actor);

        srv_debug(1, xs_fmt("object_admire (%s) %s %s", like ? "Like" : "Announce", actor, fn));
    }

    return status;
}


int _object_user_cache(snac *snac, const char *id, const char *cachedir, int del)
/* adds or deletes from a user cache */
{
    xs *ofn = _object_fn(id);
    xs *l   = xs_split(ofn, "/");
    xs *cfn = xs_fmt("%s/%s/%s", snac->basedir, cachedir, xs_list_get(l, -1));
    xs *idx = xs_fmt("%s/%s.idx", snac->basedir, cachedir);
    int ret;

    if (del) {
        if ((ret = unlink(cfn)) != -1)
            index_del(idx, id);
    }
    else {
        if ((ret = link(ofn, cfn)) != -1)
            index_add(idx, id);
    }

    return ret;
}


int object_user_cache_add(snac *snac, const char *id, const char *cachedir)
/* caches an object into a user cache */
{
    return _object_user_cache(snac, id, cachedir, 0);
}


int object_user_cache_del(snac *snac, const char *id, const char *cachedir)
/* deletes an object from a user cache */
{
    return _object_user_cache(snac, id, cachedir, 1);
}


int object_user_cache_in(snac *snac, const char *id, const char *cachedir)
/* checks if an object is stored in a cache */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    xs *cfn = xs_fmt("%s/%s/%s.json", snac->basedir, cachedir, md5);

    return !!(mtime(cfn) != 0.0);
}


d_char *object_user_cache_list(snac *snac, const char *cachedir, int max)
/* returns the objects in a cache as a list */
{
    xs *idx = xs_fmt("%s/%s.idx", snac->basedir, cachedir);
    return index_list(idx, max);
}


/** specialized functions **/

/** followers **/

int follower_add(snac *snac, const char *actor)
/* adds a follower */
{
    int ret = object_user_cache_add(snac, actor, "followers");

    snac_debug(snac, 2, xs_fmt("follower_add %s", actor));

    return ret == -1 ? 500 : 200;
}


int follower_del(snac *snac, const char *actor)
/* deletes a follower */
{
    int ret = object_user_cache_del(snac, actor, "followers");

    snac_debug(snac, 2, xs_fmt("follower_del %s", actor));

    return ret == -1 ? 404 : 200;
}


int follower_check(snac *snac, const char *actor)
/* checks if someone is a follower */
{
    return object_user_cache_in(snac, actor, "followers");
}


d_char *follower_list(snac *snac)
/* returns the list of followers */
{
    xs *list      = object_user_cache_list(snac, "followers", XS_ALL);
    d_char *fwers = xs_list_new();
    char *p, *v;

    /* resolve the list of md5 to be a list of actors */
    p = list;
    while (xs_list_iter(&p, &v)) {
        xs *a_obj = NULL;

        if (valid_status(object_get_by_md5(v, &a_obj, NULL))) {
            char *actor = xs_dict_get(a_obj, "id");

            if (!xs_is_null(actor))
                fwers = xs_list_append(fwers, actor);
        }
    }

    return fwers;
}


/** timeline **/

double timeline_mtime(snac *snac)
{
    xs *fn = xs_fmt("%s/private.idx", snac->basedir);
    return mtime(fn);
}


int timeline_del(snac *snac, char *id)
/* deletes a message from the timeline */
{
    /* delete from the user's caches */
    object_user_cache_del(snac, id, "public");
    object_user_cache_del(snac, id, "private");

    /* try to delete the object if it's not used elsewhere */
    return object_del_if_unref(id);
}


void timeline_update_indexes(snac *snac, const char *id)
/* updates the indexes */
{
    object_user_cache_add(snac, id, "private");

    if (xs_startswith(id, snac->actor)) {
        xs *msg = NULL;

        if (valid_status(object_get(id, &msg, NULL))) {
            /* if its ours and is public, also store in public */
            if (is_msg_public(snac, msg))
                object_user_cache_add(snac, id, "public");
        }
    }
}


int timeline_add(snac *snac, char *id, char *o_msg, char *parent, char *referrer)
/* adds a message to the timeline */
{
    int ret = object_add(id, o_msg);
    timeline_update_indexes(snac, id);

    snac_debug(snac, 1, xs_fmt("timeline_add %s", id));

    return ret;
}


void timeline_admire(snac *snac, char *o_msg, char *id, char *admirer, int like)
/* updates a timeline entry with a new admiration */
{
    /* if we are admiring this, add to both timelines */
    if (!like && strcmp(admirer, snac->actor) == 0) {
        object_user_cache_add(snac, id, "public");
        object_user_cache_add(snac, id, "private");
    }

    object_admire(id, admirer, like);

    snac_debug(snac, 1, xs_fmt("timeline_admire (%s) %s %s",
            like ? "Like" : "Announce", id, admirer));
}


d_char *timeline_top_level(d_char *list)
/* returns the top level md5 entries from this index */
{
    xs_set seen;
    char *p, *v;

    xs_set_init(&seen);

    p = list;
    while (xs_list_iter(&p, &v)) {
        char line[256] = "";

        strcpy(line, v);

        for (;;) {
            char line2[256];

            /* if it doesn't have a parent, use this */
            if (!object_parent(line, line2, sizeof(line2)))
                break;

            /* well, there is a parent... but if it's not there, use this */
            if (!object_here_by_md5(line2))
                break;

            /* it's here! try again with its own parent */
            strcpy(line, line2);
        }

        xs_set_add(&seen, line);
    }

    return xs_set_result(&seen);
}


d_char *timeline_simple_list(snac *snac, const char *idx_name, int skip, int show)
/* returns a timeline (with all entries) */
{
    int c_max;

    /* maximum number of items in the timeline */
    c_max = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));

    /* never more timeline entries than the configured maximum */
    if (show > c_max)
        show = c_max;

    xs *idx = xs_fmt("%s/%s.idx", snac->basedir, idx_name);

    return index_list_desc(idx, skip, show);
}


d_char *timeline_list(snac *snac, const char *idx_name, int skip, int show)
/* returns a timeline (only top level entries) */
{
    xs *list = timeline_simple_list(snac, idx_name, skip, show);

    return timeline_top_level(list);
}


/** following **/

/* this needs special treatment and cannot use the object db as is,
   with a link to a cached author, because we need the Follow object
   in case we need to unfollow (Undo + original Follow) */

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
/* we're not following this actor any longer */
{
    xs *fn = _following_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("following_del %s %s", actor, fn));

    return 200;
}


int following_check(snac *snac, char *actor)
/* checks if we are following this actor */
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


d_char *following_list(snac *snac)
/* returns the list of people being followed */
{
    xs *spec = xs_fmt("%s/following/" "*.json", snac->basedir);
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

                if (o != NULL) {
                    char *type = xs_dict_get(o, "type");

                    if (!xs_is_null(type) && strcmp(type, "Accept") == 0) {
                        char *actor = xs_dict_get(o, "actor");

                        if (!xs_is_null(actor))
                            list = xs_list_append(list, actor);
                    }
                }
            }
        }
    }

    return list;
}


d_char *_muted_fn(snac *snac, char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/muted/%s", snac->basedir, md5);
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


d_char *_hidden_fn(snac *snac, const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return xs_fmt("%s/hidden/%s", snac->basedir, md5);
}


void hide(snac *snac, const char *id)
/* hides a message tree */
{
    xs *fn = _hidden_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%s\n", id);
        fclose(f);

        snac_debug(snac, 2, xs_fmt("hidden %s %s", id, fn));

        /* hide all the children */
        xs *chld = object_children(id);
        char *p, *v;

        p = chld;
        while (xs_list_iter(&p, &v)) {
            xs *co = NULL;

            /* resolve to get the id */
            if (valid_status(object_get_by_md5(v, &co, NULL))) {
                if ((v = xs_dict_get(co, "id")) != NULL)
                    hide(snac, v);
            }
        }
    }
}


int is_hidden(snac *snac, const char *id)
/* check is id is hidden */
{
    xs *fn = _hidden_fn(snac, id);

    return !!(mtime(fn) != 0.0);
}


int actor_add(snac *snac, const char *actor, d_char *msg)
/* adds an actor */
{
    return object_add_ow(actor, msg);
}


int actor_get(snac *snac, const char *actor, d_char **data)
/* returns an already downloaded actor */
{
    int status = 200;
    d_char *d;

    if (strcmp(actor, snac->actor) == 0) {
        /* this actor */
        if (data)
            *data = msg_actor(snac);

        return status;
    }

    /* read the object */
    if (!valid_status(status = object_get(actor, &d, NULL)))
        return status;

    if (data)
        *data = d;
    else
        d = xs_free(d);

    xs *fn = _object_fn(actor);
    double max_time;

    /* maximum time for the actor data to be considered stale */
    max_time = 3600.0 * 36.0;

    if (mtime(fn) + max_time < (double) time(NULL)) {
        /* actor data exists but also stinks */
        FILE *f;

        if ((f = fopen(fn, "a")) != NULL) {
            /* write a blank at the end to 'touch' the file */
            fwrite(" ", 1, 1, f);
            fclose(f);
        }

        status = 205; /* "205: Reset Content" "110: Response Is Stale" */
    }

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

    *size = XS_ALL;

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


/** the queue **/

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


void enqueue_output(snac *snac, char *msg, char *inbox, int retries)
/* enqueues an output message to an inbox */
{
    if (xs_startswith(inbox, snac->actor)) {
        snac_debug(snac, 1, xs_str_new("refusing enqueue to myself"));
        return;
    }

    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *fn   = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);
    xs *qmsg = xs_dict_new();
    xs *rn   = xs_number_new(retries);

    qmsg = xs_dict_append(qmsg, "type",    "output");
    qmsg = xs_dict_append(qmsg, "inbox",   inbox);
    qmsg = xs_dict_append(qmsg, "object",  msg);
    qmsg = xs_dict_append(qmsg, "retries", rn);

    _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_output %s %s %d", inbox, fn, retries));
}


void enqueue_output_by_actor(snac *snac, char *msg, char *actor, int retries)
/* enqueues an output message for an actor */
{
    xs *inbox = get_actor_inbox(snac, actor);

    if (!xs_is_null(inbox))
        enqueue_output(snac, msg, inbox, retries);
    else
        snac_log(snac, xs_fmt("enqueue_output_by_actor cannot get inbox %s", actor));
}


void enqueue_email(snac *snac, char *msg, int retries)
/* enqueues an email message to be sent */
{
    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *fn   = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);
    xs *qmsg = xs_dict_new();
    xs *rn   = xs_number_new(retries);

    qmsg = xs_dict_append(qmsg, "type",    "email");
    qmsg = xs_dict_append(qmsg, "message", msg);
    qmsg = xs_dict_append(qmsg, "retries", rn);

    _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_email %d", retries));
}


void enqueue_message(snac *snac, char *msg)
/* enqueues an output message */
{
    char *id = xs_dict_get(msg, "id");
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);
    xs *qmsg = xs_dict_new();

    qmsg = xs_dict_append(qmsg, "type",    "message");
    qmsg = xs_dict_append(qmsg, "message", msg);

    _enqueue_put(fn, qmsg);

    snac_debug(snac, 0, xs_fmt("enqueue_message %s", id));
}


d_char *queue(snac *snac)
/* returns a list with filenames that can be dequeued */
{
    xs *spec     = xs_fmt("%s/queue/" "*.json", snac->basedir);
    d_char *list = xs_list_new();
    time_t t     = time(NULL);
    char *p, *v;

    xs *fns = xs_glob(spec, 0, 0);

    p = fns;
    while (xs_list_iter(&p, &v)) {
        /* get the retry time from the basename */
        char *bn  = strrchr(v, '/');
        time_t t2 = atol(bn + 1);

        if (t2 > t)
            snac_debug(snac, 2, xs_fmt("queue not yet time for %s [%ld]", v, t));
        else {
            list = xs_list_append(list, v);
            snac_debug(snac, 2, xs_fmt("queue ready for %s", v));
        }
    }

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


/** the purge **/

static void _purge_file(const char *fn, time_t mt)
/* purge fn if it's older than days */
{
    if (mtime(fn) < mt) {
        /* older than the minimum time: delete it */
        unlink(fn);
        srv_debug(1, xs_fmt("purged %s", fn));
    }
}


static void _purge_subdir(snac *snac, const char *subdir, int days)
/* purges all files in subdir older than days */
{
    if (days) {
        time_t mt = time(NULL) - days * 24 * 3600;
        xs *spec  = xs_fmt("%s/%s/" "*", snac->basedir, subdir);
        xs *list  = xs_glob(spec, 0, 0);
        char *p, *v;

        p = list;
        while (xs_list_iter(&p, &v))
            _purge_file(v, mt);
    }
}


void purge_server(void)
/* purge global server data */
{
    int tpd = xs_number_get(xs_dict_get(srv_config, "timeline_purge_days"));
    xs *spec = xs_fmt("%s/object/??", srv_basedir);
    xs *dirs = xs_glob(spec, 0, 0);
    char *p, *v;

    time_t mt = time(NULL) - tpd * 24 * 3600;

    p = dirs;
    while (xs_list_iter(&p, &v)) {
        xs *spec2 = xs_fmt("%s/" "*.json", v);
        xs *files = xs_glob(spec2, 0, 0);
        char *p2, *v2;

        p2 = files;
        while (xs_list_iter(&p2, &v2)) {
            int n_link;

            /* old and with no hard links? */
            if (mtime_nl(v2, &n_link) < mt && n_link < 2) {
                xs *s1    = xs_replace(v2, ".json", "");
                xs *l     = xs_split(s1, "/");
                char *md5 = xs_list_get(l, -1);

                object_del_by_md5(md5);
            }
        }
    }
}


void purge_user(snac *snac)
/* do the purge for this user */
{
    int days;

    days = xs_number_get(xs_dict_get(srv_config, "timeline_purge_days"));
    _purge_subdir(snac, "timeline", days);
    _purge_subdir(snac, "hidden", days);
    _purge_subdir(snac, "private", days);

    days = xs_number_get(xs_dict_get(srv_config, "local_purge_days"));
    _purge_subdir(snac, "local", days);
    _purge_subdir(snac, "public", days);
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
            purge_user(&snac);
            user_free(&snac);
        }
    }

    purge_server();
}
