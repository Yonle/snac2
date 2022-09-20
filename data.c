/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_openssl.h"

#include "snac.h"

#include <glob.h>


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
        error = xs_fmt("error opening '%s'", cfg_file);
    else {
        xs *cfg_data;

        /* read full config file */
        cfg_data = xs_readall(f);

        /* parse */
        srv_config = xs_json_loads(cfg_data);

        if (srv_config == NULL)
            error = xs_fmt("cannot parse '%s'", cfg_file);
        else {
            char *host;
            char *prefix;
            char *dbglvl;

            host   = xs_dict_get(srv_config, "host");
            prefix = xs_dict_get(srv_config, "prefix");
            dbglvl = xs_dict_get(srv_config, "dbglevel");

            if (host == NULL || prefix == NULL)
                error = xs_str_new("cannot get server data");
            else {
                srv_baseurl = xs_fmt("https://%s%s", host, prefix);

                dbglevel = (int) xs_number_get(dbglvl);

                if ((dbglvl = getenv("DEBUG")) != NULL) {
                    dbglevel = atoi(dbglvl);
                    error = xs_fmt("DEBUG level set to %d from environment", dbglevel);
                }

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
            srv_log(xs_fmt("error opening '%s'", cfg_file));
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
    d_char *list;
    xs *spec;
    glob_t globbuf;

    globbuf.gl_offs = 1;

    list = xs_list_new();
    spec = xs_fmt("%s/user/" "*", srv_basedir);

    if (glob(spec, 0, NULL, &globbuf) == 0) {
        int n;
        char *p;

        for (n = 0; (p = globbuf.gl_pathv[n]) != NULL; n++) {
            if ((p = strrchr(p, '/')) != NULL)
                list = xs_list_append(list, p + 1);
        }
    }

    globfree(&globbuf);

    return list;
}


float mtime(char *fn)
/* returns the mtime of a file or directory, or 0.0 */
{
    struct stat st;
    float r = 0.0;

    if (stat(fn, &st) != -1)
        r = (float)st.st_mtim.tv_sec;

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
    xs *fn = _follower_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("follower_del %s %s", actor, fn));

    return 200;
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
    d_char *list;
    xs *spec;
    glob_t globbuf;

    list = xs_list_new();
    spec = xs_fmt("%s/followers/" "*.json", snac->basedir);

    if (glob(spec, 0, NULL, &globbuf) == 0) {
        int n;
        char *p;

        for (n = 0; (p = globbuf.gl_pathv[n]) != NULL; n++) {
            FILE *f;

            if ((f = fopen(p, "r")) != NULL) {
                xs *j = xs_readall(f);
                xs *o = xs_json_loads(j);

                if (o != NULL)
                    list = xs_list_append(list, o);

                fclose(f);
            }
        }
    }

    globfree(&globbuf);

    return list;
}
