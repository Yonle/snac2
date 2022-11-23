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
