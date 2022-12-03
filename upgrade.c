/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_glob.h"

#include "snac.h"

#include <sys/stat.h>


int db_upgrade(d_char **error)
{
    int ret = 1;
    int changed = 0;
    double f = 0.0;

    for (;;) {
        char *layout = xs_dict_get(srv_config, "layout");
        double nf;

        f = nf = xs_number_get(layout);

        if (!(f < db_layout))
            break;

        srv_log(xs_fmt("db_upgrade %1.1lf < %1.1lf", f, db_layout));

        if (f < 2.0) {
            *error = xs_fmt("ERROR: unsupported old disk layout %1.1lf\n", f);
            ret    = 0;
            break;
        }
        else
        if (f < 2.1) {
            xs *dir = xs_fmt("%s/object", srv_basedir);
            mkdir(dir, 0755);

            nf = 2.1;
        }
        else
        if (f < 2.2) {
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    xs *spec = xs_fmt("%s/actors/" "*.json", snac.basedir);
                    xs *list = xs_glob(spec, 0, 0);
                    char *g, *fn;

                    g = list;
                    while (xs_list_iter(&g, &fn)) {
                        xs *l   = xs_split(fn, "/");
                        char *b = xs_list_get(l, -1);
                        xs *dir = xs_fmt("%s/object/%c%c", srv_basedir, b[0], b[1]);
                        xs *nfn = xs_fmt("%s/%s", dir, b);

                        mkdir(dir, 0755);
                        rename(fn, nfn);
                    }

                    xs *odir = xs_fmt("%s/actors", snac.basedir);
                    rmdir(odir);

                    user_free(&snac);
                }
            }

            nf = 2.2;
        }
        else
        if (f < 2.3) {
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    char *p, *v;
                    xs *dir = xs_fmt("%s/hidden", snac.basedir);

                    /* create the hidden directory */
                    mkdir(dir, 0755);

                    /* rename all muted files incorrectly named .json */
                    xs *spec = xs_fmt("%s/muted/" "*.json", snac.basedir);
                    xs *fns  = xs_glob(spec, 0, 0);

                    p = fns;
                    while (xs_list_iter(&p, &v)) {
                        xs *nfn = xs_replace(v, ".json", "");
                        rename(v, nfn);
                    }

                    user_free(&snac);
                }
            }

            nf = 2.3;
        }
        else
        if (f < 2.4) {
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    xs *dir = xs_fmt("%s/public", snac.basedir);
                    mkdir(dir, 0755);

                    dir = xs_replace_i(dir, "public", "private");
                    mkdir(dir, 0755);

                    user_free(&snac);
                }
            }

            nf = 2.4;
        }
        else
        if (f < 2.5) {
            /* upgrade followers */
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    xs *spec = xs_fmt("%s/followers/" "*.json", snac.basedir);
                    xs *dir  = xs_glob(spec, 0, 0);
                    char *p, *v;

                    p = dir;
                    while (xs_list_iter(&p, &v)) {
                        FILE *f;

                        if ((f = fopen(v, "r")) != NULL) {
                            xs *s = xs_readall(f);
                            xs *o = xs_json_loads(s);
                            fclose(f);

                            char *type = xs_dict_get(o, "type");

                            if (!xs_is_null(type) && strcmp(type, "Follow") == 0) {
                                unlink(v);

                                char *actor = xs_dict_get(o, "actor");

                                if (!xs_is_null(actor))
                                    follower_add(&snac, actor);
                            }
                        }
                    }

                    user_free(&snac);
                }
            }

            nf = 2.5;
        }
        else
        if (f < 2.6) {
            /* upgrade local/ to public/ */
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    xs *spec = xs_fmt("%s/local/" "*.json", snac.basedir);
                    xs *dir  = xs_glob(spec, 0, 0);
                    char *p, *v;

                    p = dir;
                    while (xs_list_iter(&p, &v)) {
                        FILE *f;

                        if ((f = fopen(v, "r")) != NULL) {
                            xs *s = xs_readall(f);
                            xs *o = xs_json_loads(s);
                            fclose(f);

                            xs *meta = xs_dup(xs_dict_get(o, "_snac"));
                            o = xs_dict_del(o, "_snac");

                            char *id = xs_dict_get(o, "id");

                            /* store object */
                            object_add_ow(id, o);

                            /* if it's from us, add to public */
                            if (xs_startswith(id, snac.actor)) {
                                char *p, *v;

                                object_user_cache_add(&snac, id, "public");

                                p = xs_dict_get(meta, "announced_by");
                                while (xs_list_iter(&p, &v))
                                    object_admire(id, v, 0);
                                p = xs_dict_get(meta, "liked_by");
                                while (xs_list_iter(&p, &v))
                                    object_admire(id, v, 1);
                            }

                            unlink(v);
                        }
                    }

                    xs *od = xs_fmt("%s/local", snac.basedir);
                    rmdir(od);

                    user_free(&snac);
                }
            }

            nf = 2.6;
        }
        else
        if (f < 2.7) {
            /* upgrade timeline/ to private/ */
            xs *users = user_list();
            char *p, *v;

            p = users;
            while (xs_list_iter(&p, &v)) {
                snac snac;

                if (user_open(&snac, v)) {
                    xs *spec = xs_fmt("%s/timeline/" "*.json", snac.basedir);
                    xs *dir  = xs_glob(spec, 0, 0);
                    char *p, *v;

                    p = dir;
                    while (xs_list_iter(&p, &v)) {
                        FILE *f;

                        if ((f = fopen(v, "r")) != NULL) {
                            xs *s = xs_readall(f);
                            xs *o = xs_json_loads(s);
                            fclose(f);

                            xs *meta = xs_dup(xs_dict_get(o, "_snac"));
                            o = xs_dict_del(o, "_snac");

                            char *id = xs_dict_get(o, "id");

                            /* store object */
                            object_add_ow(id, o);

                            {
                                char *p, *v;

                                object_user_cache_add(&snac, id, "private");

                                p = xs_dict_get(meta, "announced_by");
                                while (xs_list_iter(&p, &v))
                                    object_admire(id, v, 0);
                                p = xs_dict_get(meta, "liked_by");
                                while (xs_list_iter(&p, &v))
                                    object_admire(id, v, 1);
                            }

                            unlink(v);
                        }
                    }

                    xs *od = xs_fmt("%s/timeline", snac.basedir);
                    rmdir(od);

                    user_free(&snac);
                }
            }

            nf = 2.7;
        }

        if (f < nf) {
            f          = nf;
            xs *nv     = xs_number_new(f);
            srv_config = xs_dict_set(srv_config, "layout", nv);

            srv_log(xs_fmt("db_upgrade converted to version %1.1lf", f));
            changed++;
        }
        else
            break;
    }

    if (f > db_layout) {
        *error = xs_fmt("ERROR: unknown future version %lf\n", f);
        ret    = 0;
    }

    if (changed) {
        /* upgrade the configuration file */
        xs *fn = xs_fmt("%s/server.json", srv_basedir);
        FILE *f;

        if ((f = fopen(fn, "w")) != NULL) {
            xs *j = xs_json_dumps_pp(srv_config, 4);
            fwrite(j, strlen(j), 1, f);
            fclose(f);

            srv_log(xs_fmt("upgraded db %s after %d changes", fn, changed));
        }
        else
            ret = 0;
    }

    return ret;
}
