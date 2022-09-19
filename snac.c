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


void snac_debug(snac *snac, int level, d_char *str)
/* prints a user debugging information */
{
    xs *msg = str;

    if (dbglevel >= level) {
        xs *tm = xs_local_time("%H:%M:%S");
        fprintf(stderr, "%s [%s] %s\n", tm, snac->uid, msg);
    }
}
