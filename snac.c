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
#include "xs_mime.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_time.h"
#include "xs_glob.h"

#include "snac.h"

#include <sys/time.h>
#include <sys/stat.h>

d_char *srv_basedir = NULL;
d_char *srv_config  = NULL;
d_char *srv_baseurl = NULL;
int     srv_running = 0;

int dbglevel = 0;


int valid_status(int status)
/* is this HTTP status valid? */
{
    return status >= 200 && status <= 299;
}


d_char *tid(int offset)
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


void srv_debug(int level, d_char *str)
/* logs a debug message */
{
    xs *msg = str;

    if (xs_str_in(msg, srv_basedir) != -1) {
        /* replace basedir with ~ */
        msg = xs_replace_i(msg, srv_basedir, "~");
    }

    if (dbglevel >= level) {
        xs *tm = xs_str_localtime(0, "%H:%M:%S");
        fprintf(stderr, "%s %s\n", tm, msg);
    }
}


void snac_debug(snac *snac, int level, d_char *str)
/* prints a user debugging information */
{
    xs *o_str = str;
    d_char *msg = xs_fmt("[%s] %s", snac->uid, o_str);

    if (xs_str_in(msg, snac->basedir) != -1) {
        /* replace long basedir references with ~ */
        msg = xs_replace_i(msg, snac->basedir, "~");
    }

    srv_debug(level, msg);
}


d_char *hash_password(const char *uid, const char *passwd, const char *nonce)
/* hashes a password */
{
    xs *d_nonce = NULL;
    xs *combi;
    xs *hash;

    if (nonce == NULL)
        nonce = d_nonce = xs_fmt("%08x", random());

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


void srv_archive(char *direction, char *req, char *payload, int p_size,
                 int status, char *headers, char *body, int b_size)
/* archives a connection */
{
    /* obsessive archiving */
    xs *date = tid(0);
    xs *dir  = xs_fmt("%s/archive/%s_%s", srv_basedir, date, direction);
    FILE *f;

    if (mkdir(dir, 0755) != -1) {
        xs *meta_fn = xs_fmt("%s/_META", dir);

        if ((f = fopen(meta_fn, "w")) != NULL) {
            xs *j1 = xs_json_dumps_pp(req, 4);
            xs *j2 = xs_json_dumps_pp(headers, 4);

            fprintf(f, "dir: %s\n", direction);
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