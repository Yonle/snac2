/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

extern d_char *srv_basedir;
extern d_char *srv_config;
extern d_char *srv_baseurl;

extern int dbglevel;

d_char *xs_time(char *fmt, int local);
#define xs_local_time(fmt) xs_time(fmt, 1)
#define xs_utc_time(fmt)   xs_time(fmt, 0)

void srv_debug(int level, d_char *str);
#define srv_log(str) srv_debug(0, str)

int srv_open(char *basedir);

