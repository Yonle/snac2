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

void srv_archive(char *direction, char *req, char *payload, int p_size,
                 int status, char *headers, char *body, int b_size);

float mtime(char *fn);

int follower_add(snac *snac, char *actor, char *msg);
int follower_del(snac *snac, char *actor);
int follower_check(snac *snac, char *actor);
d_char *follower_list(snac *snac);

int timeline_here(snac *snac, char *id);
d_char *timeline_find(snac *snac, char *id);
void timeline_del(snac *snac, char *id);
d_char *timeline_get(snac *snac, char *fn);
d_char *timeline_list(snac *snac);
int timeline_add(snac *snac, char *id, char *o_msg, char *parent, char *referrer);
void timeline_admire(snac *snac, char *id, char *admirer, int like);

int following_add(snac *snac, char *actor, char *msg);
int following_del(snac *snac, char *actor);
int following_check(snac *snac, char *actor);

void mute(snac *snac, char *actor);
void unmute(snac *snac, char *actor);
int is_muted(snac *snac, char *actor);

int actor_add(snac *snac, char *actor, char *msg);
int actor_get(snac *snac, char *actor, d_char **data);

void enqueue_input(snac *snac, char *msg, char *req);
void enqueue_output(snac *snac, char *msg, char *actor, int retries);

d_char *queue(snac *snac);
d_char *dequeue(snac *snac, char *fn);

d_char *http_signed_request(snac *snac, char *method, char *url,
                        d_char *headers,
                        d_char *body, int b_size,
                        int *status, d_char **payload, int *p_size);

void httpd(void);

int webfinger_request(char *qs, char **actor, char **user);
int webfinger_get_handler(d_char *req, char *q_path,
                          char **body, int *b_size, char **ctype);

d_char *msg_admiration(snac *snac, char *object, char *type);
d_char *msg_create(snac *snac, char *object);
d_char *msg_note(snac *snac, char *content, char *rcpts, char *in_reply_to);

int activitypub_request(snac *snac, char *url, d_char **data);
int actor_request(snac *snac, char *actor, d_char **data);
int send_to_inbox(snac *snac, char *inbox, char *msg, d_char **payload, int *p_size);
int send_to_actor(snac *snac, char *actor, char *msg, d_char **payload, int *p_size);
void process_queue(snac *snac);
void post(snac *snac, char *msg);
int activitypub_get_handler(d_char *req, char *q_path,
                            char **body, int *b_size, char **ctype);
int activitypub_post_handler(d_char *req, char *q_path,
                             char *payload, int p_size,
                             char **body, int *b_size, char **ctype);

d_char *not_really_markdown(char *content, d_char **f_content);
