/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

extern d_char *srv_basedir;
extern d_char *srv_config;
extern d_char *srv_baseurl;

extern int dbglevel;

void srv_log(d_char *str);
int srv_open(char *basedir);

