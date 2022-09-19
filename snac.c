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

#include <sys/time.h>


d_char *srv_basedir = NULL;
d_char *srv_config  = NULL;
d_char *srv_baseurl = NULL;

int dbglevel = 0;


d_char *xs_time(char *fmt, int local)
/* returns a d_char with a formated time */
{
    time_t t = time(NULL);
    struct tm tm;
    char tmp[64];

    if (local)
        localtime_r(&t, &tm);
    else
        gmtime_r(&t, &tm);

    strftime(tmp, sizeof(tmp), fmt, &tm);

    return xs_str_new(tmp);
}


d_char *tid(void)
/* returns a time-based Id */
{
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);

    return xs_fmt("%10d.%06d", tv.tv_sec, tv.tv_usec);
}


void srv_debug(int level, d_char *str)
/* logs a debug message */
{
    xs *msg = str;

    if (dbglevel >= level) {
        xs *tm = xs_local_time("%H:%M:%S");
        fprintf(stderr, "%s %s\n", tm, msg);
    }
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
        srv_log(xs_fmt("error opening '%s'", cfg_file));
    else {
        xs *cfg_data;

        /* read full config file */
        cfg_data = xs_readall(f);

        /* parse */
        srv_config = xs_json_loads(cfg_data);

        if (srv_config == NULL)
            srv_log(xs_fmt("cannot parse '%s'", cfg_file));
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


int validate_uid(char *uid)
/* returns if uid is a valid identifier */
{
    while (*uid) {
        if (!(isalnum(*uid) || *uid == '_'))
            return 0;

        uid++;
    }

    return 1;
}


void snac_free(snac *snac)
/* frees a user snac */
{
    free(snac->uid);
    free(snac->basedir);
    free(snac->config);
    free(snac->key);
    free(snac->actor);
}


int snac_open(snac *snac, char *uid)
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
        snac_free(snac);

    return ret;
}


void snac_debug(snac *snac, int level, d_char *str)
/* prints a user debugging information */
{
    xs *msg = str;

    if (dbglevel >= level) {
        xs *tm = xs_local_time("%H:%M:%S");
        fprintf(stderr, "%s [%s] %s\n", tm, snac->uid, msg);
    }
}
