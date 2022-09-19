/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

extern d_char *srv_basedir;
extern d_char *srv_config;
extern d_char *srv_baseurl;

extern int dbglevel;

d_char *xs_time(char *fmt, int local);
#define xs_local_time(fmt) xs_time(fmt, 1)
#define xs_utc_time(fmt)   xs_time(fmt, 0)

d_char *tid(void);

void srv_debug(int level, d_char *str);
#define srv_log(str) srv_debug(0, str)

int srv_open(char *basedir);

typedef struct _snac {
    d_char *uid;        /* uid */
    d_char *basedir;    /* user base directory */
    d_char *config;     /* user configuration */
    d_char *key;        /* keypair */
    d_char *actor;      /* actor url */
} snac;

int snac_open(snac *snac, char *uid);
void snac_free(snac *snac);

void snac_debug(snac *snac, int level, d_char *str);
#define snac_log(snac, str) snac_debug(snac, 0, str)

int validate_uid(char *uid);
