/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

extern d_char *srv_basedir;
extern d_char *srv_config;
extern d_char *srv_baseurl;
extern int     srv_running;

extern int dbglevel;

#define valid_status(status) ((status) >= 200 && (status) <= 299)

d_char *xs_time(char *fmt, int local);
#define xs_local_time(fmt) xs_time(fmt, 1)
#define xs_utc_time(fmt)   xs_time(fmt, 0)

d_char *tid(int offset);

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

int user_open(snac *snac, char *uid);
void user_free(snac *snac);
d_char *user_list(void);

void snac_debug(snac *snac, int level, d_char *str);
#define snac_log(snac, str) snac_debug(snac, 0, str)

int validate_uid(char *uid);

d_char *hash_password(char *uid, char *passwd, char *nonce);
int check_password(char *uid, char *passwd, char *hash);

float mtime(char *fn);

int follower_add(snac *snac, char *actor, char *msg);
int follower_del(snac *snac, char *actor);
int follower_check(snac *snac, char *actor);
d_char *follower_list(snac *snac);

d_char *timeline_find(snac *snac, char *id);
void timeline_del(snac *snac, char *id);
d_char *timeline_get(snac *snac, char *fn);
d_char *timeline_list(snac *snac);

int following_add(snac *snac, char *actor, char *msg);
int following_del(snac *snac, char *actor);
int following_check(snac *snac, char *actor);

void mute(snac *snac, char *actor);
void unmute(snac *snac, char *actor);
int is_muted(snac *snac, char *actor);

void enqueue(snac *snac, char *actor, char *msg, int retries);
d_char *queue(snac *snac);
d_char *dequeue(snac *snac, char *fn);

d_char *http_signed_request(snac *snac, char *method, char *url,
                        d_char *headers,
                        d_char *body, int b_size,
                        int *status, d_char **payload, int *p_size);

void httpd(void);

void webfinger_request(char *qs, int *status, char **actor, char **user);
void webfinger_get_handler(d_char *req, char *q_path, int *status,
                        char **body, int *b_size, char **ctype);
