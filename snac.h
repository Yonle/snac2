/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#define VERSION "2.28-dev"

#define USER_AGENT "snac/" VERSION

#define DIR_PERM 02770

extern double disk_layout;
extern d_char *srv_basedir;
extern d_char *srv_config;
extern d_char *srv_baseurl;
extern int     srv_running;

extern int dbglevel;

#define L(s) (s)

int mkdirx(const char *pathname);

int valid_status(int status);
d_char *tid(int offset);
double ftime(void);

void srv_debug(int level, d_char *str);
#define srv_log(str) srv_debug(0, str)

int srv_open(char *basedir, int auto_upgrade);
void srv_free(void);

typedef struct _snac {
    xs_str *uid;        /* uid */
    xs_str *basedir;    /* user base directory */
    xs_dict *config;    /* user configuration */
    xs_dict *config_o;  /* user configuration admin override */
    xs_dict *key;       /* keypair */
    xs_str *actor;      /* actor url */
    xs_str *md5;        /* actor url md5 */
} snac;

int user_open(snac *snac, const char *uid);
void user_free(snac *snac);
d_char *user_list(void);

void snac_debug(snac *snac, int level, d_char *str);
#define snac_log(snac, str) snac_debug(snac, 0, str)

int validate_uid(const char *uid);

d_char *hash_password(const char *uid, const char *passwd, const char *nonce);
int check_password(const char *uid, const char *passwd, const char *hash);

void srv_archive(const char *direction, const char *url, xs_dict *req,
                 const char *payload, int p_size,
                 int status, xs_dict *headers,
                 const char *body, int b_size);
void srv_archive_error(const char *prefix, const xs_str *err,
                       const xs_dict *req, const xs_val *data);

double mtime_nl(const char *fn, int *n_link);
#define mtime(fn) mtime_nl(fn, NULL)
double f_ctime(const char *fn);

int index_add(const char *fn, const char *md5);
int index_gc(const char *fn);
int index_first(const char *fn, char *buf, int size);
int index_len(const char *fn);
d_char *index_list(const char *fn, int max);
d_char *index_list_desc(const char *fn, int skip, int show);

int object_add(const char *id, d_char *obj);
int object_add_ow(const char *id, d_char *obj);
int object_here_by_md5(char *id);
int object_here(char *id);
int object_get_by_md5(const char *md5, xs_dict **obj);
int object_get(const char *id, xs_dict **obj);
int object_del(const char *id);
int object_del_if_unref(const char *id);
double object_ctime_by_md5(const char *md5);
double object_ctime(const char *id);
int object_admire(const char *id, const char *actor, int like);

int object_likes_len(const char *id);
int object_announces_len(const char *id);

d_char *object_children(const char *id);
d_char *object_likes(const char *id);
d_char *object_announces(const char *id);
int object_parent(const char *id, char *buf, int size);

int object_user_cache_add(snac *snac, const char *id, const char *cachedir);
int object_user_cache_del(snac *snac, const char *id, const char *cachedir);

int follower_add(snac *snac, const char *actor);
int follower_del(snac *snac, const char *actor);
int follower_check(snac *snac, const char *actor);
d_char *follower_list(snac *snac);

double timeline_mtime(snac *snac);
int timeline_here(snac *snac, const char *md5);
int timeline_get_by_md5(snac *snac, const char *md5, xs_dict **msg);
int timeline_del(snac *snac, char *id);
d_char *timeline_simple_list(snac *snac, const char *idx_name, int skip, int show);
d_char *timeline_list(snac *snac, const char *idx_name, int skip, int show);
int timeline_add(snac *snac, char *id, char *o_msg);
void timeline_admire(snac *snac, char *id, char *admirer, int like);

xs_list *timeline_top_level(snac *snac, xs_list *list);

d_char *local_list(snac *snac, int max);

int following_add(snac *snac, char *actor, char *msg);
int following_del(snac *snac, char *actor);
int following_check(snac *snac, char *actor);
int following_get(snac *snac, char *actor, d_char **data);
d_char *following_list(snac *snac);

void mute(snac *snac, char *actor);
void unmute(snac *snac, char *actor);
int is_muted(snac *snac, char *actor);

void hide(snac *snac, const char *id);
int is_hidden(snac *snac, const char *id);

int actor_add(snac *snac, const char *actor, d_char *msg);
int actor_get(snac *snac, const char *actor, d_char **data);

int static_get(snac *snac, const char *id, d_char **data, int *size);
void static_put(snac *snac, const char *id, const char *data, int size);

double history_mtime(snac *snac, char *id);
void history_add(snac *snac, char *id, char *content, int size);
d_char *history_get(snac *snac, char *id);
int history_del(snac *snac, char *id);
d_char *history_list(snac *snac);

void lastlog_write(snac *snac);

xs_str *notify_check_time(snac *snac, int reset);
void notify_add(snac *snac, const char *type, const char *utype,
                const char *actor, const char *objid);

void inbox_add(const char *inbox);
void inbox_add_by_actor(const xs_dict *actor);
xs_list *inbox_list(void);

void enqueue_input(snac *snac, xs_dict *msg, xs_dict *req, int retries);
void enqueue_output_raw(const char *keyid, const char *seckey,
                        xs_dict *msg, xs_str *inbox, int retries);
void enqueue_output(snac *snac, xs_dict *msg, xs_str *inbox, int retries);
void enqueue_output_by_actor(snac *snac, xs_dict *msg, xs_str *actor, int retries);
void enqueue_email(xs_str *msg, int retries);
void enqueue_telegram(const xs_str *msg, const char *bot, const char *chat_id);
void enqueue_message(snac *snac, char *msg);

xs_list *user_queue(snac *snac);
xs_list *queue(void);
xs_dict *dequeue(const char *fn);

void purge(snac *snac);
void purge_all(void);

xs_dict *http_signed_request_raw(const char *keyid, const char *seckey,
                            const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout);
xs_dict *http_signed_request(snac *snac, const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout);
int check_signature(snac *snac, xs_dict *req, xs_str **err);

void httpd(void);

int webfinger_request(char *qs, char **actor, char **user);
int webfinger_get_handler(d_char *req, char *q_path,
                          char **body, int *b_size, char **ctype);

const char *default_avatar_base64(void);

d_char *msg_admiration(snac *snac, char *object, char *type);
d_char *msg_create(snac *snac, char *object);
d_char *msg_follow(snac *snac, char *actor);

xs_dict *msg_note(snac *snac, xs_str *content, xs_val *rcpts,
                  xs_str *in_reply_to, xs_list *attach, int priv);

d_char *msg_undo(snac *snac, char *object);
d_char *msg_delete(snac *snac, char *id);
d_char *msg_actor(snac *snac);
xs_dict *msg_update(snac *snac, xs_dict *object);

int activitypub_request(snac *snac, char *url, d_char **data);
int actor_request(snac *snac, char *actor, d_char **data);
int send_to_inbox_raw(const char *keyid, const char *seckey,
                  const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout);
int send_to_inbox(snac *snac, const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout);
d_char *get_actor_inbox(snac *snac, char *actor);
int send_to_actor(snac *snac, char *actor, char *msg, d_char **payload, int *p_size, int timeout);
int is_msg_public(snac *snac, const xs_dict *msg);
int is_msg_for_me(snac *snac, const xs_dict *msg);

int process_user_queue(snac *snac);
void process_queue_item(xs_dict *q_item);
int process_queue(void);

int activitypub_get_handler(d_char *req, char *q_path,
                            char **body, int *b_size, char **ctype);
int activitypub_post_handler(d_char *req, char *q_path,
                             char *payload, int p_size,
                             char **body, int *b_size, char **ctype);

d_char *not_really_markdown(const char *content);
d_char *sanitize(const char *str);

int html_get_handler(d_char *req, char *q_path, char **body, int *b_size, char **ctype);
int html_post_handler(d_char *req, char *q_path, d_char *payload, int p_size,
                      char **body, int *b_size, char **ctype);

int snac_init(const char *_basedir);
int adduser(const char *uid);
int resetpwd(snac *snac);

int job_fifo_ready(void);
void job_post(const xs_val *job, int urgent);
void job_wait(xs_val **job);

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype);
int oauth_post_handler(const xs_dict *req, const char *q_path,
                       const char *payload, int p_size,
                       char **body, int *b_size, char **ctype);
int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype);
int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype);
