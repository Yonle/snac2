/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#define XS_IMPLEMENTATION

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_openssl.h"
#include "xs_socket.h"
#include "xs_httpd.h"

#include "snac.h"

d_char *srv_basedir = NULL;
d_char *srv_config  = NULL;
d_char *srv_baseurl = NULL;

int dbglevel = 0;


void srv_log(d_char *str)
/* logs a message */
{
    char tm[16] = "00:00:00";
    xs *msg = str;

    fprintf(stderr, "%s %s\n", tm, msg);
}


int srv_open(char *basedir)
/* opens a server */
{
    int ret = 0;
    xs *cfg_file = NULL;
    FILE *f;

    srv_basedir = xs_str_new(basedir);

    cfg_file = xs_fmt("%s/server.json", basedir);

    if ((f = fopen(cfg_file, "r")) == NULL)
        srv_log(xs_fmt("cannot open %s", cfg_file));
    else {
        xs *cfg_data;

        /* read full config file */
        cfg_data = xs_readall(f);

        /* parse */
        srv_config = xs_json_loads(cfg_data);

        if (srv_config == NULL)
            srv_log(xs_fmt("cannot parse %s", cfg_file));
        else {
            char *host;
            char *prefix;
            char *dbglvl;

            host   = xs_dict_get(srv_config, "host");
            prefix = xs_dict_get(srv_config, "prefix");
            dbglvl = xs_dict_get(srv_config, "dbglevel");

            if (host == NULL || prefix == NULL)
                srv_log(xs_str_new("cannot get server data"));
            else {
                srv_baseurl = xs_fmt("https://%s%s", host, prefix);

                dbglevel = (int) xs_number_get(dbglvl);

                if ((dbglvl = getenv("DEBUG")) != NULL) {
                    dbglevel = atoi(dbglvl);
                    srv_log(xs_fmt("DEBUG level set to %d from environment", dbglevel));
                }

                ret = 1;
            }
        }
    }

    return ret;
}


int main(int argc, char *argv[])
{
    srv_open("/home/angel/lib/snac/comam.es");

    return 0;
}
