/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#define XS_IMPLEMENTATION

#include "xs.h"
#include "xs_io.h"
#include "xs_unicode.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_openssl.h"
#include "xs_socket.h"
#include "xs_httpd.h"
#include "xs_mime.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_time.h"
#include "xs_glob.h"
#include "xs_random.h"

#include "snac.h"

#include <sys/time.h>
#include <sys/stat.h>

xs_str *srv_basedir = NULL;
xs_dict *srv_config = NULL;
xs_str *srv_baseurl = NULL;

int dbglevel = 0;


int mkdirx(const char *pathname)
/* creates a directory with special permissions */
{
    int ret;

    if ((ret = mkdir(pathname, DIR_PERM)) != -1)
        ret = chmod(pathname, DIR_PERM);

    return ret;
}


int valid_status(int status)
/* is this HTTP status valid? */
{
    return status >= 200 && status <= 299;
}


xs_str *tid(int offset)
/* returns a time-based Id */
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return xs_fmt("%10d.%06d", tv.tv_sec + offset, tv.tv_usec);
}


double ftime(void)
/* returns the UNIX time as a float */
{
    xs *ntid = tid(0);

    return atof(ntid);
}


int validate_uid(const char *uid)
/* returns if uid is a valid identifier */
{
    while (*uid) {
        if (!(isalnum(*uid) || *uid == '_'))
            return 0;

        uid++;
    }

    return 1;
}


void srv_debug(int level, xs_str *str)
/* logs a debug message */
{
    if (xs_str_in(str, srv_basedir) != -1) {
        /* replace basedir with ~ */
        str = xs_replace_i(str, srv_basedir, "~");
    }

    if (dbglevel >= level) {
        xs *tm = xs_str_localtime(0, "%H:%M:%S");
        fprintf(stderr, "%s %s\n", tm, str);

        /* if the ~/error/ folder exists, also write to a file there */
        xs *lf = xs_fmt("%s/error/debug.log", srv_basedir);
        FILE *f;
        if ((f = fopen(lf, "a")) != NULL) {
            fprintf(f, "%s %s\n", tm, str);
            fclose(f);
        }
    }

    xs_free(str);
}


void snac_debug(snac *snac, int level, xs_str *str)
/* prints a user debugging information */
{
    xs *o_str = str;
    xs_str *msg = xs_fmt("[%s] %s", snac->uid, o_str);

    if (xs_str_in(msg, snac->basedir) != -1) {
        /* replace long basedir references with ~ */
        msg = xs_replace_i(msg, snac->basedir, "~");
    }

    srv_debug(level, msg);
}


xs_str *hash_password(const char *uid, const char *passwd, const char *nonce)
/* hashes a password */
{
    xs *d_nonce = NULL;
    xs *combi;
    xs *hash;

    if (nonce == NULL) {
        unsigned int r;
        xs_rnd_buf(&r, sizeof(r));
        d_nonce = xs_fmt("%08x", r);
        nonce = d_nonce;
    }

    combi = xs_fmt("%s:%s:%s", nonce, uid, passwd);
    hash  = xs_sha1_hex(combi, strlen(combi));

    return xs_fmt("%s:%s", nonce, hash);
}


int check_password(const char *uid, const char *passwd, const char *hash)
/* checks a password */
{
    int ret = 0;
    xs *spl = xs_split_n(hash, ":", 1);

    if (xs_list_len(spl) == 2) {
        xs *n_hash = hash_password(uid, passwd, xs_list_get(spl, 0));

        ret = (strcmp(hash, n_hash) == 0);
    }

    return ret;
}
