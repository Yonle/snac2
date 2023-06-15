/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_openssl.h"
#include "xs_glob.h"
#include "xs_set.h"
#include "xs_time.h"

#include "snac.h"

#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>

double disk_layout = 2.7;

/* storage serializer */
pthread_mutex_t data_mutex = {0};

int snac_upgrade(d_char **error);


int srv_open(char *basedir, int auto_upgrade)
/* opens a server */
{
    int ret = 0;
    xs *cfg_file = NULL;
    FILE *f;
    xs_str *error = NULL;

    pthread_mutex_init(&data_mutex, NULL);

    srv_basedir = xs_str_new(basedir);

    if (xs_endswith(srv_basedir, "/"))
        srv_basedir = xs_crop_i(srv_basedir, 0, -1);

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
                    ret = snac_upgrade(&error);
                else {
                    if (xs_number_get(xs_dict_get(srv_config, "layout")) < disk_layout)
                        error = xs_fmt("ERROR: disk layout changed - execute 'snac upgrade' first");
                    else
                        ret = 1;
                }
            }

        }
    }

    if (error != NULL)
        srv_log(error);

    /* create the queue/ subdir, just in case */
    xs *qdir = xs_fmt("%s/queue", srv_basedir);
    mkdirx(qdir);

    xs *ibdir = xs_fmt("%s/inbox", srv_basedir);
    mkdirx(ibdir);

#ifdef __OpenBSD__
    char *v = xs_dict_get(srv_config, "disable_openbsd_security");

    if (v && xs_type(v) == XSTYPE_TRUE) {
        srv_debug(1, xs_dup("OpenBSD security disabled by admin"));
    }
    else {
        srv_debug(1, xs_fmt("Calling unveil()"));
        unveil(basedir,                "rwc");
        unveil("/usr/sbin/sendmail",   "x");
        unveil("/etc/resolv.conf",     "r");
        unveil("/etc/hosts",           "r");
        unveil("/etc/ssl/openssl.cnf", "r");
        unveil("/etc/ssl/cert.pem",    "r");
        unveil("/usr/share/zoneinfo",  "r");
        unveil(NULL,                   NULL);
        srv_debug(1, xs_fmt("Calling pledge()"));
        pledge("stdio rpath wpath cpath flock inet proc exec dns fattr", NULL);
    }
#endif /* __OpenBSD__ */

    return ret;
}


void srv_free(void)
{
    xs_free(srv_basedir);
    xs_free(srv_config);
    xs_free(srv_baseurl);

    pthread_mutex_destroy(&data_mutex);
}


void user_free(snac *snac)
/* frees a user snac */
{
    xs_free(snac->uid);
    xs_free(snac->basedir);
    xs_free(snac->config);
    xs_free(snac->config_o);
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

                        /* everything is ok right now */
                        ret = 1;

                        /* does it have a configuration override? */
                        xs *cfg_file_o = xs_fmt("%s/user_o.json", snac->basedir);
                        if ((f = fopen(cfg_file_o, "r")) != NULL) {
                            xs *j = xs_readall(f);
                            fclose(f);

                            if ((snac->config_o = xs_json_loads(j)) == NULL)
                                srv_log(xs_fmt("error parsing '%s'", cfg_file_o));
                        }

                        if (snac->config_o == NULL)
                            snac->config_o = xs_dict_new();
                    }
                    else
                        srv_log(xs_fmt("error parsing '%s'", key_file));
                }
                else
                    srv_log(xs_fmt("error opening '%s' %d", key_file, errno));
            }
            else
                srv_log(xs_fmt("error parsing '%s'", cfg_file));
        }
        else
            srv_debug(2, xs_fmt("error opening '%s' %d", cfg_file, errno));
    }
    else
        srv_debug(1, xs_fmt("invalid user '%s'", uid));

    if (!ret)
        user_free(snac);

    return ret;
}


xs_list *user_list(void)
/* returns the list of user ids */
{
    xs *spec = xs_fmt("%s/user/" "*", srv_basedir);
    return xs_glob(spec, 1, 0);
}


int user_open_by_md5(snac *snac, const char *md5)
/* iterates all users searching by md5 */
{
    xs *ulist  = user_list();
    xs_list *p = ulist;
    xs_str *v;

    while (xs_list_iter(&p, &v)) {
        user_open(snac, v);

        if (strcmp(snac->md5, md5) == 0)
            return 1;

        user_free(snac);
    }

    return 0;
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


#define MIN(v1, v2) ((v1) < (v2) ? (v1) : (v2))

double f_ctime(const char *fn)
/* returns the ctime of a file or directory, or 0.0 */
{
    struct stat st;
    double r = 0.0;

    if (fn && stat(fn, &st) != -1) {
        /* return the lowest of ctime and mtime;
           there are operations that change the ctime, like link() */
        r = (double) MIN(st.st_ctim.tv_sec, st.st_mtim.tv_sec);
    }

    return r;
}


/** database 2.1+ **/

/** indexes **/


int index_add_md5(const char *fn, const char *md5)
/* adds an md5 to an index */
{
    int status = 201; /* Created */
    FILE *f;

    pthread_mutex_lock(&data_mutex);

    if ((f = fopen(fn, "a")) != NULL) {
        flock(fileno(f), LOCK_EX);

        /* ensure the position is at the end after getting the lock */
        fseek(f, 0, SEEK_END);

        fprintf(f, "%s\n", md5);
        fclose(f);
    }
    else
        status = 500;

    pthread_mutex_unlock(&data_mutex);

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
    FILE *f;

    pthread_mutex_lock(&data_mutex);

    if ((f = fopen(fn, "r+")) != NULL) {
        char line[256];

        while (fgets(line, sizeof(line), f) != NULL) {
            line[32] = '\0';

            if (strcmp(line, md5) == 0) {
                /* found! just rewind, overwrite it with garbage
                   and an eventual call to index_gc() will clean it
                   [yes: this breaks index_len()] */
                fseek(f, -33, SEEK_CUR);
                fwrite("-", 1, 1, f);
                status = 200;

                break;
            }
        }

        fclose(f);
    }
    else
        status = 500;

    pthread_mutex_unlock(&data_mutex);

    return status;
}


int index_del(const char *fn, const char *id)
/* deletes an id from an index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_del_md5(fn, md5);
}


int index_gc(const char *fn)
/* garbage-collects an index, deleting objects that are not here */
{
    FILE *i, *o;
    int gc = -1;

    pthread_mutex_lock(&data_mutex);

    if ((i = fopen(fn, "r")) != NULL) {
        xs *nfn = xs_fmt("%s.new", fn);
        char line[256];

        if ((o = fopen(nfn, "w")) != NULL) {
            gc = 0;

            while (fgets(line, sizeof(line), i) != NULL) {
                line[32] = '\0';

                if (object_here_by_md5(line))
                    fprintf(o, "%s\n", line);
                else
                    gc++;
            }

            fclose(o);

            xs *ofn = xs_fmt("%s.bak", fn);

            unlink(ofn);
            link(fn, ofn);
            rename(nfn, fn);
        }

        fclose(i);
    }

    pthread_mutex_unlock(&data_mutex);

    return gc;
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


xs_list *index_list(const char *fn, int max)
/* returns an index as a list */
{
    xs_list *list = NULL;
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


xs_list *index_list_desc(const char *fn, int skip, int show)
/* returns an index as a list, in reverse order */
{
    xs_list *list = NULL;
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

xs_str *_object_fn_by_md5(const char *md5)
{
    if (!xs_is_hex(md5))
        srv_log(xs_fmt("_object_fn_by_md5(): '%s' not hex", md5));

    xs *bfn = xs_fmt("%s/object/%c%c", srv_basedir, md5[0], md5[1]);

    mkdirx(bfn);

    return xs_fmt("%s/%s.json", bfn, md5);
}


xs_str *_object_fn(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return _object_fn_by_md5(md5);
}


int object_here_by_md5(const char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn_by_md5(id);
    return mtime(fn) > 0.0;
}


int object_here(const char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn(id);
    return mtime(fn) > 0.0;
}


int object_get_by_md5(const char *md5, xs_dict **obj)
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

        if (*obj)
            status = 200;
    }
    else
        *obj = NULL;

    return status;
}


int object_get(const char *id, xs_dict **obj)
/* returns a stored object, optionally of the requested type */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_get_by_md5(md5, obj);
}


int _object_add(const char *id, const xs_dict *obj, int ow)
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


int object_add(const char *id, const xs_dict *obj)
/* stores an object */
{
    return _object_add(id, obj, 0);
}


int object_add_ow(const char *id, const xs_dict *obj)
/* stores an object (overwriting allowed) */
{
    return _object_add(id, obj, 1);
}


int object_del_by_md5(const char *md5)
/* deletes an object by its md5 */
{
    int status = 404;
    xs *fn     = _object_fn_by_md5(md5);

    if (unlink(fn) != -1) {
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


double object_ctime_by_md5(const char *md5)
{
    xs *fn = _object_fn_by_md5(md5);
    return f_ctime(fn);
}


double object_ctime(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_ctime_by_md5(md5);
}


xs_str *_object_index_fn(const char *id, const char *idxsfx)
/* returns the filename of an object's index */
{
    xs_str *fn = _object_fn(id);
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


xs_list *object_children(const char *id)
/* returns the list of an object's children */
{
    xs *fn = _object_index_fn(id, "_c.idx");
    return index_list(fn, XS_ALL);
}


xs_list *object_likes(const char *id)
{
    xs *fn = _object_index_fn(id, "_l.idx");
    return index_list(fn, XS_ALL);
}


xs_list *object_announces(const char *id)
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


int object_unadmire(const char *id, const char *actor, int like)
/* actor no longer likes or announces this object */
{
    int status;
    xs *fn = _object_fn(id);

    fn = xs_replace_i(fn, ".json", like ? "_l.idx" : "_a.idx");

    status = index_del(fn, actor);

    srv_debug(0,
        xs_fmt("object_unadmire (%s) %s %s %d", like ? "Like" : "Announce", actor, fn, status));

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
        ret = unlink(cfn);
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


xs_list *object_user_cache_list(snac *snac, const char *cachedir, int max)
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


xs_list *follower_list(snac *snac)
/* returns the list of followers */
{
    xs *list       = object_user_cache_list(snac, "followers", XS_ALL);
    xs_list *fwers = xs_list_new();
    char *p, *v;

    /* resolve the list of md5 to be a list of actors */
    p = list;
    while (xs_list_iter(&p, &v)) {
        xs *a_obj = NULL;

        if (valid_status(object_get_by_md5(v, &a_obj))) {
            const char *actor = xs_dict_get(a_obj, "id");

            if (!xs_is_null(actor)) {
                /* check if the actor is still cached */
                xs *fn = xs_fmt("%s/followers/%s.json", snac->basedir, v);

                if (mtime(fn) > 0.0)
                    fwers = xs_list_append(fwers, actor);
            }
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


int timeline_touch(snac *snac)
/* changes the date of the timeline index */
{
    xs *fn = xs_fmt("%s/private.idx", snac->basedir);
    return utimes(fn, NULL);
}


xs_str *timeline_fn_by_md5(snac *snac, const char *md5)
/* get the filename of an entry by md5 from any timeline */
{
    xs_str *fn = xs_fmt("%s/private/%s.json", snac->basedir, md5);

    if (mtime(fn) == 0.0) {
        fn = xs_free(fn);
        fn = xs_fmt("%s/public/%s.json", snac->basedir, md5);

        if (mtime(fn) == 0.0)
            fn = xs_free(fn);
    }

    return fn;
}


int timeline_here(snac *snac, const char *md5)
/* checks if an object is in the user cache */
{
    xs *fn = timeline_fn_by_md5(snac, md5);

    return !(fn == NULL);
}


int timeline_get_by_md5(snac *snac, const char *md5, xs_dict **msg)
/* gets a message from the timeline */
{
    int status = 404;
    FILE *f    = NULL;

    xs *fn = timeline_fn_by_md5(snac, md5);

    if (fn != NULL && (f = fopen(fn, "r")) != NULL) {
        flock(fileno(f), LOCK_SH);

        xs *j = xs_readall(f);
        fclose(f);

        if ((*msg = xs_json_loads(j)) != NULL)
            status = 200;
    }

    return status;
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

        if (valid_status(object_get(id, &msg))) {
            /* if its ours and is public, also store in public */
            if (is_msg_public(snac, msg)) {
                object_user_cache_add(snac, id, "public");

                /* also add it to the instance public timeline */
                xs *ipt = xs_fmt("%s/public.idx", srv_basedir);
                index_add(ipt, id);
            }
        }
    }
}


int timeline_add(snac *snac, char *id, char *o_msg)
/* adds a message to the timeline */
{
    int ret = object_add(id, o_msg);
    timeline_update_indexes(snac, id);

    snac_debug(snac, 1, xs_fmt("timeline_add %s", id));

    return ret;
}


void timeline_admire(snac *snac, char *id, char *admirer, int like)
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


xs_list *timeline_top_level(snac *snac, xs_list *list)
/* returns the top level md5 entries from this index */
{
    xs_set seen;
    xs_list *p;
    xs_str *v;

    xs_set_init(&seen);

    p = list;
    while (xs_list_iter(&p, &v)) {
        char line[256] = "";

        strncpy(line, v, sizeof(line));

        for (;;) {
            char line2[256];

            /* if it doesn't have a parent, use this */
            if (!object_parent(line, line2, sizeof(line2)))
                break;

            /* well, there is a parent... but is it here? */
            if (!timeline_here(snac, line2))
                break;

            /* it's here! try again with its own parent */
            strncpy(line, line2, sizeof(line));
        }

        xs_set_add(&seen, line);
    }

    return xs_set_result(&seen);
}


xs_list *timeline_simple_list(snac *snac, const char *idx_name, int skip, int show)
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


xs_list *timeline_list(snac *snac, const char *idx_name, int skip, int show)
/* returns a timeline (only top level entries) */
{
    xs *list = timeline_simple_list(snac, idx_name, skip, show);

    return timeline_top_level(snac, list);
}


xs_list *timeline_instance_list(int skip, int show)
/* returns the timeline for the full instance */
{
    xs *idx = xs_fmt("%s/public.idx", srv_basedir);

    return index_list_desc(idx, skip, show);
}


/** following **/

/* this needs special treatment and cannot use the object db as is,
   with a link to a cached author, because we need the Follow object
   in case we need to unfollow (Undo + original Follow) */

xs_str *_following_fn(snac *snac, const char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/following/%s.json", snac->basedir, md5);
}


int following_add(snac *snac, const char *actor, const xs_dict *msg)
/* adds to the following list */
{
    int ret = 201; /* created */
    xs *fn = _following_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, 1, strlen(j), f);
        fclose(f);

        /* get the filename of the actor object */
        xs *actor_fn = _object_fn(actor);

        /* increase its reference count */
        fn = xs_replace_i(fn, ".json", "_a.json");
        link(actor_fn, fn);
    }
    else
        ret = 500;

    snac_debug(snac, 2, xs_fmt("following_add %s %s", actor, fn));

    return ret;
}


int following_del(snac *snac, const char *actor)
/* we're not following this actor any longer */
{
    xs *fn = _following_fn(snac, actor);

    snac_debug(snac, 2, xs_fmt("following_del %s %s", actor, fn));

    unlink(fn);

    /* also delete the reference to the author */
    fn = xs_replace_i(fn, ".json", "_a.json");
    unlink(fn);

    return 200;
}


int following_check(snac *snac, const char *actor)
/* checks if we are following this actor */
{
    xs *fn = _following_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


int following_get(snac *snac, const char *actor, xs_dict **data)
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


xs_list *following_list(snac *snac)
/* returns the list of people being followed */
{
    xs *spec = xs_fmt("%s/following/" "*.json", snac->basedir);
    xs *glist = xs_glob(spec, 0, 0);
    xs_list *p;
    xs_str *v;
    xs_list *list = xs_list_new();

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
                    const char *type = xs_dict_get(o, "type");

                    if (!xs_is_null(type) && strcmp(type, "Accept") == 0) {
                        const char *actor = xs_dict_get(o, "actor");

                        if (!xs_is_null(actor)) {
                            list = xs_list_append(list, actor);

                            /* check if there is a link to the actor object */
                            xs *v2 = xs_replace(v, ".json", "_a.json");

                            if (mtime(v2) == 0.0) {
                                /* no; add a link to it */
                                xs *actor_fn = _object_fn(actor);
                                link(actor_fn, v2);
                            }
                        }
                    }
                }
            }
        }
    }

    return list;
}


xs_str *_muted_fn(snac *snac, const char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/muted/%s", snac->basedir, md5);
}


void mute(snac *snac, const char *actor)
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


void unmute(snac *snac, const char *actor)
/* actor is no longer a moron */
{
    xs *fn = _muted_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("unmuted %s %s", actor, fn));
}


int is_muted(snac *snac, const char *actor)
/* check if someone is muted */
{
    xs *fn = _muted_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


xs_str *_hidden_fn(snac *snac, const char *id)
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
            if (valid_status(object_get_by_md5(v, &co))) {
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


int actor_add(const char *actor, xs_dict *msg)
/* adds an actor */
{
    return object_add_ow(actor, msg);
}


int actor_get(snac *snac1, const char *actor, xs_dict **data)
/* returns an already downloaded actor */
{
    int status = 200;
    xs_dict *d = NULL;

    if (strcmp(actor, snac1->actor) == 0) {
        /* this actor */
        if (data)
            *data = msg_actor(snac1);

        return status;
    }

    if (xs_startswith(actor, srv_baseurl)) {
        /* it's a (possible) local user */
        xs *l = xs_split(actor, "/");
        const char *uid = xs_list_get(l, -1);
        snac user;

        if (!xs_is_null(uid) && user_open(&user, uid)) {
            if (data)
                *data = msg_actor(&user);

            user_free(&user);
            return 200;
        }
        else
            return 404;
    }

    /* read the object */
    if (!valid_status(status = object_get(actor, &d))) {
        d = xs_free(d);
        return status;
    }

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

        /* touch the file */
        utimes(fn, NULL);

        status = 205; /* "205: Reset Content" "110: Response Is Stale" */
    }

    return status;
}


/** static data **/

xs_str *_static_fn(snac *snac, const char *id)
/* gets the filename for a static file */
{
    if (strchr(id, '/'))
        return NULL;
    else
        return xs_fmt("%s/static/%s", snac->basedir, id);
}


int static_get(snac *snac, const char *id, xs_val **data, int *size)
/* returns static content */
{
    xs *fn = _static_fn(snac, id);
    FILE *f;
    int status = 404;

    if (fn && (f = fopen(fn, "rb")) != NULL) {
        *size = XS_ALL;
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

    if (fn && (f = fopen(fn, "wb")) != NULL) {
        fwrite(data, size, 1, f);
        fclose(f);
    }
}


void static_put_meta(snac *snac, const char *id, const char *str)
/* puts metadata (i.e. a media description string) to id */
{
    xs *fn = _static_fn(snac, id);

    if (fn) {
        fn = xs_str_cat(fn, ".txt");
        FILE *f;

        if ((f = fopen(fn, "w")) != NULL) {
            fprintf(f, "%s\n", str);
            fclose(f);
        }
    }
}


xs_str *static_get_meta(snac *snac, const char *id)
/* gets metadata from a media */
{
    xs *fn    = _static_fn(snac, id);
    xs_str *r = NULL;

    if (fn) {
        fn = xs_str_cat(fn, ".txt");
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            r = xs_strip_i(xs_readline(f));
            fclose(f);
        }
    }
    else
        r = xs_str_new("");

    return r;
}


/** history **/

xs_str *_history_fn(snac *snac, const char *id)
/* gets the filename for the history */
{
    if (strchr(id, '/'))
        return NULL;
    else
        return xs_fmt("%s/history/%s", snac->basedir, id);
}


double history_mtime(snac *snac, const char *id)
{
    double t = 0.0;
    xs *fn = _history_fn(snac, id);

    if (fn != NULL)
        t = mtime(fn);

    return t;
}


void history_add(snac *snac, const char *id, const char *content, int size)
/* adds something to the history */
{
    xs *fn = _history_fn(snac, id);
    FILE *f;

    if (fn && (f = fopen(fn, "w")) != NULL) {
        fwrite(content, size, 1, f);
        fclose(f);
    }
}


xs_str *history_get(snac *snac, const char *id)
{
    xs_str *content = NULL;
    xs *fn = _history_fn(snac, id);
    FILE *f;

    if (fn && (f = fopen(fn, "r")) != NULL) {
        content = xs_readall(f);
        fclose(f);
    }

    return content;
}


int history_del(snac *snac, const char *id)
{
    xs *fn = _history_fn(snac, id);

    if (fn)
        return unlink(fn);
    else
        return -1;
}


xs_list *history_list(snac *snac)
{
    xs *spec = xs_fmt("%s/history/" "*.html", snac->basedir);

    return xs_glob(spec, 1, 0);
}


void lastlog_write(snac *snac, const char *source)
/* writes the last time the user logged in */
{
    xs *fn = xs_fmt("%s/lastlog.txt", snac->basedir);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%lf %s\n", ftime(), source);
        fclose(f);
    }
}


/** inbox collection **/

void inbox_add(const char *inbox)
/* collects a shared inbox */
{
    xs *md5 = xs_md5_hex(inbox, strlen(inbox));
    xs *fn  = xs_fmt("%s/inbox/%s", srv_basedir, md5);
    FILE *f;

    if (strlen(inbox) < 256 && (f = fopen(fn, "w")) != NULL) {
        pthread_mutex_lock(&data_mutex);

        fprintf(f, "%s\n", inbox);
        fclose(f);

        pthread_mutex_unlock(&data_mutex);
    }
}


void inbox_add_by_actor(const xs_dict *actor)
/* collects an actor's shared inbox, if it has one */
{
    char *v;

    if (!xs_is_null(v = xs_dict_get(actor, "endpoints")) &&
        !xs_is_null(v = xs_dict_get(v, "sharedInbox")))
        inbox_add(v);
}


xs_list *inbox_list(void)
/* returns the collected inboxes as a list */
{
    xs_list *ibl = xs_list_new();
    xs *spec     = xs_fmt("%s/inbox/" "*", srv_basedir);
    xs *files    = xs_glob(spec, 0, 0);
    xs_list *p   = files;
    xs_val *v;

    while (xs_list_iter(&p, &v)) {
        FILE *f;

        if ((f = fopen(v, "r")) != NULL) {
            char line[256];

            if (fgets(line, sizeof(line), f)) {
                fclose(f);

                int i = strlen(line);

                if (i) {
                    line[i - 1] = '\0';
                    ibl = xs_list_append(ibl, line);
                }
            }
        }
    }

    return ibl;
}


/** notifications **/

xs_str *notify_check_time(snac *snac, int reset)
/* gets or resets the latest notification check time */
{
    xs_str *t = NULL;
    xs *fn    = xs_fmt("%s/notifydate.txt", snac->basedir);
    FILE *f;

    if (reset) {
        if ((f = fopen(fn, "w")) != NULL) {
            t = tid(0);
            fprintf(f, "%s\n", t);
            fclose(f);
        }
    }
    else {
        if ((f = fopen(fn, "r")) != NULL) {
            t = xs_readline(f);
            fclose(f);
        }
        else
            /* never set before */
            t = xs_fmt("%16.6f", 0.0);
    }

    return t;
}


void notify_add(snac *snac, const char *type, const char *utype,
                const char *actor, const char *objid)
/* adds a new notification */
{
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/notify/", snac->basedir);
    xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
    FILE *f;

    /* create the directory */
    mkdirx(fn);
    fn = xs_str_cat(fn, ntid);
    fn = xs_str_cat(fn, ".json");

    xs *noti = xs_dict_new();

    noti = xs_dict_append(noti, "id",    ntid);
    noti = xs_dict_append(noti, "type",  type);
    noti = xs_dict_append(noti, "utype", utype);
    noti = xs_dict_append(noti, "actor", actor);
    noti = xs_dict_append(noti, "date",  date);

    if (!xs_is_null(objid))
        noti = xs_dict_append(noti, "objid", objid);

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(noti, 4);

        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }
}


xs_dict *notify_get(snac *snac, const char *id)
/* gets a notification */
{
    xs *fn = xs_fmt("%s/notify/%s.json", snac->basedir, id);
    FILE *f;
    xs_dict *out = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);
        fclose(f);

        out = xs_json_loads(j);
    }

    return out;
}


xs_list *notify_list(snac *snac, int new_only)
/* returns a list of notification ids, optionally only the new ones */
{
    xs *t = NULL;

    /* if only new ones are requested, get the last time */
    if (new_only)
        t = notify_check_time(snac, 0);

    xs *spec     = xs_fmt("%s/notify/" "*.json", snac->basedir);
    xs *lst      = xs_glob(spec, 1, 1);
    xs_list *out = xs_list_new();
    xs_list *p   = lst;
    xs_str *v;

    while (xs_list_iter(&p, &v)) {
        xs *id = xs_replace(v, ".json", "");

        /* old? */
        if (t != NULL && strcmp(id, t) < 0)
            continue;

        out = xs_list_append(out, id);
    }

    return out;
}


void notify_clear(snac *snac)
/* clears all notifications */
{
    xs *spec   = xs_fmt("%s/notify/" "*", snac->basedir);
    xs *lst    = xs_glob(spec, 0, 0);
    xs_list *p = lst;
    xs_str *v;

    while (xs_list_iter(&p, &v))
        unlink(v);
}


/** the queue **/

static xs_dict *_enqueue_put(const char *fn, xs_dict *msg)
/* writes safely to the queue */
{
    xs *tfn = xs_fmt("%s.tmp", fn);
    FILE *f;

    if ((f = fopen(tfn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(msg, 4);

        fwrite(j, strlen(j), 1, f);
        fclose(f);

        rename(tfn, fn);
    }

    return msg;
}


static xs_dict *_new_qmsg(const char *type, const xs_val *msg, int retries)
/* creates a queue message */
{
    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *rn   = xs_number_new(retries);

    xs_dict *qmsg = xs_dict_new();

    qmsg = xs_dict_append(qmsg, "type",    type);
    qmsg = xs_dict_append(qmsg, "message", msg);
    qmsg = xs_dict_append(qmsg, "retries", rn);
    qmsg = xs_dict_append(qmsg, "ntid",    ntid);

    return qmsg;
}


void enqueue_input(snac *snac, const xs_dict *msg, const xs_dict *req, int retries)
/* enqueues an input message */
{
    xs *qmsg   = _new_qmsg("input", msg, retries);
    char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);

    qmsg = xs_dict_append(qmsg, "req", req);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_input %s", fn));
}


void enqueue_output_raw(const char *keyid, const char *seckey,
                        xs_dict *msg, xs_str *inbox, int retries)
/* enqueues an output message to an inbox */
{
    xs *qmsg   = _new_qmsg("output", msg, retries);
    char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = xs_dict_append(qmsg, "inbox",  inbox);
    qmsg = xs_dict_append(qmsg, "keyid",  keyid);
    qmsg = xs_dict_append(qmsg, "seckey", seckey);

    /* if it's to be sent right now, bypass the disk queue and post the job */
    if (retries == 0 && job_fifo_ready())
        job_post(qmsg, 0);
    else {
        qmsg = _enqueue_put(fn, qmsg);
        srv_debug(1, xs_fmt("enqueue_output %s %s %d", inbox, fn, retries));
    }
}


void enqueue_output(snac *snac, xs_dict *msg, xs_str *inbox, int retries)
/* enqueues an output message to an inbox */
{
    if (xs_startswith(inbox, snac->actor)) {
        snac_debug(snac, 1, xs_str_new("refusing enqueue to myself"));
        return;
    }

    char *seckey = xs_dict_get(snac->key, "secret");

    enqueue_output_raw(snac->actor, seckey, msg, inbox, retries);
}


void enqueue_output_by_actor(snac *snac, xs_dict *msg, const xs_str *actor, int retries)
/* enqueues an output message for an actor */
{
    xs *inbox = get_actor_inbox(snac, actor);

    if (!xs_is_null(inbox))
        enqueue_output(snac, msg, inbox, retries);
    else
        snac_log(snac, xs_fmt("enqueue_output_by_actor cannot get inbox %s", actor));
}


void enqueue_email(xs_str *msg, int retries)
/* enqueues an email message to be sent */
{
    xs *qmsg   = _new_qmsg("email", msg, retries);
    char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_email %d", retries));
}


void enqueue_telegram(const xs_str *msg, const char *bot, const char *chat_id)
/* enqueues a message to be sent via Telegram */
{
    xs *qmsg   = _new_qmsg("telegram", msg, 0);
    char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = xs_dict_append(qmsg, "bot",      bot);
    qmsg = xs_dict_append(qmsg, "chat_id",  chat_id);

    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_email %s %s", bot, chat_id));
}


void enqueue_message(snac *snac, xs_dict *msg)
/* enqueues an output message */
{
    xs *qmsg   = _new_qmsg("message", msg, 0);
    char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(snac, 0, xs_fmt("enqueue_message %s", xs_dict_get(msg, "id")));
}


void enqueue_close_question(snac *user, const char *id, int end_secs)
/* enqueues the closing of a question */
{
    xs *qmsg = _new_qmsg("close_question", id, 0);
    xs *ntid = tid(end_secs);
    xs *fn   = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = xs_dict_set(qmsg, "ntid", ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 0, xs_fmt("enqueue_close_question %s", id));
}


void enqueue_request_replies(snac *user, const char *id)
/* enqueues a request for the replies of a message */
{
    /* test first if this precise request is already in the queue */
    xs *queue = user_queue(user);
    xs_list *p = queue;
    xs_str *v;

    while (xs_list_iter(&p, &v)) {
        xs *q_item = queue_get(v);

        if (q_item != NULL) {
            const char *type = xs_dict_get(q_item, "type");
            const char *msg  = xs_dict_get(q_item, "message");

            if (type && msg && strcmp(type, "request_replies") == 0 && strcmp(msg, id) == 0) {
                /* don't requeue */
                snac_debug(user, 0, xs_fmt("enqueue_request_replies already here %s", id));
                return;
            }
        }
    }

    /* not there; enqueue the request with a small delay */
    xs *qmsg   = _new_qmsg("request_replies", id, 0);
    xs *ntid = tid(10);
    xs *fn     = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = xs_dict_set(qmsg, "ntid", ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 0, xs_fmt("enqueue_request_replies %s", id));
}


int was_question_voted(snac *user, const char *id)
/* returns true if the user voted in this poll */
{
    xs *children = object_children(id);
    int voted = 0;
    xs_list *p;
    xs_str *md5;

    p = children;
    while (xs_list_iter(&p, &md5)) {
        xs *obj = NULL;

        if (valid_status(object_get_by_md5(md5, &obj))) {
            if (strcmp(xs_dict_get(obj, "attributedTo"), user->actor) == 0) {
                voted = 1;
                break;
            }
        }
    }

    return voted;
}


xs_list *user_queue(snac *snac)
/* returns a list with filenames that can be dequeued */
{
    xs *spec      = xs_fmt("%s/queue/" "*.json", snac->basedir);
    xs_list *list = xs_list_new();
    time_t t      = time(NULL);
    xs_list *p;
    xs_val *v;

    xs *fns = xs_glob(spec, 0, 0);

    p = fns;
    while (xs_list_iter(&p, &v)) {
        /* get the retry time from the basename */
        char *bn  = strrchr(v, '/');
        time_t t2 = atol(bn + 1);

        if (t2 > t)
            snac_debug(snac, 2, xs_fmt("user_queue not yet time for %s [%ld]", v, t));
        else {
            list = xs_list_append(list, v);
            snac_debug(snac, 2, xs_fmt("user_queue ready for %s", v));
        }
    }

    return list;
}


xs_list *queue(void)
/* returns a list with filenames that can be dequeued */
{
    xs *spec      = xs_fmt("%s/queue/" "*.json", srv_basedir);
    xs_list *list = xs_list_new();
    time_t t      = time(NULL);
    xs_list *p;
    xs_val *v;

    xs *fns = xs_glob(spec, 0, 0);

    p = fns;
    while (xs_list_iter(&p, &v)) {
        /* get the retry time from the basename */
        char *bn  = strrchr(v, '/');
        time_t t2 = atol(bn + 1);

        if (t2 > t)
            srv_debug(2, xs_fmt("queue not yet time for %s [%ld]", v, t));
        else {
            list = xs_list_append(list, v);
            srv_debug(2, xs_fmt("queue ready for %s", v));
        }
    }

    return list;
}


xs_dict *queue_get(const char *fn)
/* gets a file from a queue */
{
    FILE *f;
    xs_dict *obj = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);
        obj = xs_json_loads(j);

        fclose(f);
    }

    return obj;
}


xs_dict *dequeue(const char *fn)
/* dequeues a message */
{
    xs_dict *obj = queue_get(fn);

    if (obj != NULL)
        unlink(fn);

    return obj;
}


/** the purge **/

static int _purge_file(const char *fn, time_t mt)
/* purge fn if it's older than days */
{
    int ret = 0;

    if (mtime(fn) < mt) {
        /* older than the minimum time: delete it */
        unlink(fn);
        srv_debug(2, xs_fmt("purged %s", fn));
        ret = 1;
    }

    return ret;
}


static void _purge_dir(const char *dir, int days)
/* purges all files in a directory older than days */
{
    int cnt = 0;

    if (days) {
        time_t mt = time(NULL) - days * 24 * 3600;
        xs *spec  = xs_fmt("%s/" "*", dir);
        xs *list  = xs_glob(spec, 0, 0);
        xs_list *p;
        xs_str *v;

        p = list;
        while (xs_list_iter(&p, &v))
            cnt += _purge_file(v, mt);

        srv_debug(1, xs_fmt("purge: %s %d", dir, cnt));
    }
}


static void _purge_user_subdir(snac *snac, const char *subdir, int days)
/* purges all files in a user subdir older than days */
{
    xs *u_subdir = xs_fmt("%s/%s", snac->basedir, subdir);

    _purge_dir(u_subdir, days);
}


void purge_server(void)
/* purge global server data */
{
    xs *spec = xs_fmt("%s/object/??", srv_basedir);
    xs *dirs = xs_glob(spec, 0, 0);
    xs_list *p;
    xs_str *v;
    int cnt = 0;
    int icnt = 0;

    time_t mt = time(NULL) - 7 * 24 * 3600;

    p = dirs;
    while (xs_list_iter(&p, &v)) {
        xs_list *p2;
        xs_str *v2;

        {
            xs *spec2 = xs_fmt("%s/" "*.json", v);
            xs *files = xs_glob(spec2, 0, 0);

            p2 = files;
            while (xs_list_iter(&p2, &v2)) {
                int n_link;

                /* old and with no hard links? */
                if (mtime_nl(v2, &n_link) < mt && n_link < 2) {
                    xs *s1    = xs_replace(v2, ".json", "");
                    xs *l     = xs_split(s1, "/");
                    char *md5 = xs_list_get(l, -1);

                    object_del_by_md5(md5);
                    cnt++;
                }
            }
        }

        {
            /* look for stray indexes */
            xs *speci = xs_fmt("%s/" "*_?.idx", v);
            xs *idxfs = xs_glob(speci, 0, 0);

            p2 = idxfs;
            while (xs_list_iter(&p2, &v2)) {
                /* old enough to consider? */
                if (mtime(v2) < mt) {
                    /* check if the indexed object is here */
                    xs *o = xs_dup(v2);
                    char *ext = strchr(o, '_');

                    if (ext) {
                        *ext = '\0';
                        o = xs_str_cat(o, ".json");

                        if (mtime(o) == 0.0) {
                            /* delete */
                            unlink(v2);
                            srv_debug(1, xs_fmt("purged %s", v2));
                            icnt++;
                        }
                    }
                }
            }
        }
    }

    /* purge collected inboxes */
    xs *ib_dir = xs_fmt("%s/inbox", srv_basedir);
    _purge_dir(ib_dir, 7);

    /* purge the instance timeline */
    xs *itl_fn = xs_fmt("%s/public.idx", srv_basedir);
    int itl_gc = index_gc(itl_fn);

    srv_debug(1, xs_fmt("purge: global (obj: %d, idx: %d, itl: %d)", cnt, icnt, itl_gc));
}


void purge_user(snac *snac)
/* do the purge for this user */
{
    int priv_days, pub_days, user_days = 0;
    char *v;
    int n;

    priv_days = xs_number_get(xs_dict_get(srv_config, "timeline_purge_days"));
    pub_days  = xs_number_get(xs_dict_get(srv_config, "local_purge_days"));

    if ((v = xs_dict_get(snac->config_o, "purge_days")) != NULL ||
        (v = xs_dict_get(snac->config, "purge_days")) != NULL)
        user_days = xs_number_get(v);

    if (user_days) {
        /* override admin settings only if they are lesser */
        if (priv_days == 0 || user_days < priv_days)
            priv_days = user_days;

        if (pub_days == 0 || user_days < pub_days)
            pub_days = user_days;
    }

    _purge_user_subdir(snac, "hidden",  priv_days);
    _purge_user_subdir(snac, "private", priv_days);

    _purge_user_subdir(snac, "public",  pub_days);

    const char *idxs[] = { "followers.idx", "private.idx", "public.idx", NULL };

    for (n = 0; idxs[n]; n++) {
        xs *idx = xs_fmt("%s/%s", snac->basedir, idxs[n]);
        int gc = index_gc(idx);
        snac_debug(snac, 1, xs_fmt("purge: %s %d", idx, gc));
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
            purge_user(&snac);
            user_free(&snac);
        }
    }

    purge_server();

#ifndef NO_MASTODON_API
    mastoapi_purge();
#endif
}


/** archive **/

void srv_archive(const char *direction, const char *url, xs_dict *req,
                 const char *payload, int p_size,
                 int status, xs_dict *headers,
                 const char *body, int b_size)
/* archives a connection */
{
    /* obsessive archiving */
    xs *date = tid(0);
    xs *dir  = xs_fmt("%s/archive/%s_%s", srv_basedir, date, direction);
    FILE *f;

    if (mkdirx(dir) != -1) {
        xs *meta_fn = xs_fmt("%s/_META", dir);

        if ((f = fopen(meta_fn, "w")) != NULL) {
            xs *j1 = xs_json_dumps_pp(req, 4);
            xs *j2 = xs_json_dumps_pp(headers, 4);

            fprintf(f, "dir: %s\n", direction);

            if (url)
                fprintf(f, "url: %s\n", url);

            fprintf(f, "req: %s\n", j1);
            fprintf(f, "p_size: %d\n", p_size);
            fprintf(f, "status: %d\n", status);
            fprintf(f, "response: %s\n", j2);
            fprintf(f, "b_size: %d\n", b_size);
            fclose(f);
        }

        if (p_size && payload) {
            xs *payload_fn = NULL;
            xs *payload_fn_raw = NULL;
            char *v = xs_dict_get(req, "content-type");

            if (v && xs_str_in(v, "json") != -1) {
                payload_fn = xs_fmt("%s/payload.json", dir);

                if ((f = fopen(payload_fn, "w")) != NULL) {
                    xs *v1 = xs_json_loads(payload);
                    xs *j1 = NULL;

                    if (v1 != NULL)
                        j1 = xs_json_dumps_pp(v1, 4);

                    if (j1 != NULL)
                        fwrite(j1, strlen(j1), 1, f);
                    else
                        fwrite(payload, p_size, 1, f);

                    fclose(f);
                }
            }

            payload_fn_raw = xs_fmt("%s/payload", dir);

            if ((f = fopen(payload_fn_raw, "w")) != NULL) {
                fwrite(payload, p_size, 1, f);
                fclose(f);
            }
        }

        if (b_size && body) {
            xs *body_fn = NULL;
            char *v = xs_dict_get(headers, "content-type");

            if (v && xs_str_in(v, "json") != -1) {
                body_fn = xs_fmt("%s/body.json", dir);

                if ((f = fopen(body_fn, "w")) != NULL) {
                    xs *v1 = xs_json_loads(body);
                    xs *j1 = NULL;

                    if (v1 != NULL)
                        j1 = xs_json_dumps_pp(v1, 4);

                    if (j1 != NULL)
                        fwrite(j1, strlen(j1), 1, f);
                    else
                        fwrite(body, b_size, 1, f);

                    fclose(f);
                }
            }
            else {
                body_fn = xs_fmt("%s/body", dir);

                if ((f = fopen(body_fn, "w")) != NULL) {
                    fwrite(body, b_size, 1, f);
                    fclose(f);
                }
            }
        }
    }
}


void srv_archive_error(const char *prefix, const xs_str *err,
                       const xs_dict *req, const xs_val *data)
/* archives an error */
{
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/error/%s_%s", srv_basedir, prefix, ntid);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "Error: %s\n", err);

        if (req) {
            fprintf(f, "Request headers:\n");

            xs *j = xs_json_dumps_pp(req, 4);
            fwrite(j, strlen(j), 1, f);

            fprintf(f, "\n");
        }

        if (data) {
            fprintf(f, "Data:\n");

            if (xs_type(data) == XSTYPE_LIST || xs_type(data) == XSTYPE_DICT) {
                xs *j = xs_json_dumps_pp(data, 4);
                fwrite(j, strlen(j), 1, f);
            }
            else
                fprintf(f, "%s", data);

            fprintf(f, "\n");
        }

        fclose(f);
    }
}
