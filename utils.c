/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_time.h"
#include "xs_openssl.h"
#include "xs_random.h"

#include "snac.h"

#include <sys/stat.h>
#include <stdlib.h>

const char *default_srv_config = "{"
    "\"host\":                 \"\","
    "\"prefix\":               \"\","
    "\"address\":              \"127.0.0.1\","
    "\"port\":                 8001,"
    "\"layout\":               0.0,"
    "\"dbglevel\":             0,"
    "\"queue_retry_minutes\":  2,"
    "\"queue_retry_max\":      10,"
    "\"cssurls\":              [\"\"],"
    "\"max_timeline_entries\": 128,"
    "\"timeline_purge_days\":  120,"
    "\"local_purge_days\":     0,"
    "\"admin_email\":          \"\","
    "\"admin_account\":        \"\""
    "}";

const char *default_css =
    "body { max-width: 48em; margin: auto; line-height: 1.5; padding: 0.8em }\n"
    "img { max-width: 100% }\n"
    ".snac-origin { font-size: 85% }\n"
    ".snac-score { float: right; font-size: 85% }\n"
    ".snac-top-user { text-align: center; padding-bottom: 2em }\n"
    ".snac-top-user-name { font-size: 200% }\n"
    ".snac-top-user-id { font-size: 150% }\n"
    ".snac-avatar { float: left; height: 2.5em; padding: 0.25em }\n"
    ".snac-author { font-size: 90%; text-decoration: none }\n"
    ".snac-author-tag { font-size: 80% }\n"
    ".snac-pubdate { color: #a0a0a0; font-size: 90% }\n"
    ".snac-top-controls { padding-bottom: 1.5em }\n"
    ".snac-post { border-top: 1px solid #a0a0a0; }\n"
    ".snac-children { padding-left: 2em; border-left: 1px solid #a0a0a0; }\n"
    ".snac-textarea { font-family: inherit; width: 100% }\n"
    ".snac-history { border: 1px solid #606060; border-radius: 3px; margin: 2.5em 0; padding: 0 2em }\n"
    ".snac-btn-mute { float: right; margin-left: 0.5em }\n"
    ".snac-btn-unmute { float: right; margin-left: 0.5em }\n"
    ".snac-btn-follow { float: right; margin-left: 0.5em }\n"
    ".snac-btn-unfollow { float: right; margin-left: 0.5em }\n"
    ".snac-btn-hide { float: right; margin-left: 0.5em }\n"
    ".snac-btn-delete { float: right; margin-left: 0.5em }\n"
    ".snac-footer { margin-top: 2em; font-size: 75% }\n"
    ".snac-poll-result { margin-left: auto; margin-right: auto; }\n";

const char *greeting_html =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
    "<title>Welcome to %host%</title>\n"
    "<body style=\"margin: auto; max-width: 50em\">\n"
    "<h1>Welcome to %host%</h1>\n"
    "<p>This is a <a href=\"https://en.wikipedia.org/wiki/Fediverse\">Fediverse</a> instance\n"
    "that uses the <a href=\"https://en.wikipedia.org/wiki/ActivityPub\">ActivityPub</a> protocol.\n"
    "In other words, users at this host can communicate with people that use software like\n"
    "Mastodon, Pleroma, Friendica, etc. all around the world.</p>\n"
    "\n"
    "<p>There is no automatic sign up process for this server. If you want to be a part of\n"
    "this community, please write an email to %admin_email%\n"
    "and ask politely indicating what is your preferred user id (alphanumeric characters\n"
    "only).</p>\n"
    "\n"
    "<p>The following users are already part of this community:</p>\n"
    "\n"
    "%userlist%\n"
    "\n"
    "<p>This site is powered by <abbr title=\"Social Networks Are Crap\">snac</abbr>.</p>\n"
    "</body></html>\n";

int snac_init(const char *basedir)
{
    FILE *f;

    if (basedir == NULL) {
        printf("Base directory:\n");
        srv_basedir = xs_strip_i(xs_readline(stdin));
    }
    else
        srv_basedir = xs_str_new(basedir);

    if (srv_basedir == NULL || *srv_basedir == '\0')
        return 1;

    if (xs_endswith(srv_basedir, "/"))
        srv_basedir = xs_crop_i(srv_basedir, 0, -1);

    if (mtime(srv_basedir) != 0.0) {
        printf("ERROR: directory '%s' must not exist\n", srv_basedir);
        return 1;
    }

    srv_config = xs_json_loads(default_srv_config);

    xs *layout = xs_number_new(disk_layout);
    srv_config = xs_dict_set(srv_config, "layout", layout);

    printf("Network address [%s]:\n", xs_dict_get(srv_config, "address"));
    {
        xs *i = xs_strip_i(xs_readline(stdin));
        if (*i)
            srv_config = xs_dict_set(srv_config, "address", i);
    }

    printf("Network port [%d]:\n", (int)xs_number_get(xs_dict_get(srv_config, "port")));
    {
        xs *i = xs_strip_i(xs_readline(stdin));
        if (*i) {
            xs *n = xs_number_new(atoi(i));
            srv_config = xs_dict_set(srv_config, "port", n);
        }
    }

    printf("Host name:\n");
    {
        xs *i = xs_strip_i(xs_readline(stdin));
        if (*i == '\0')
            return 1;

        srv_config = xs_dict_set(srv_config, "host", i);
    }

    printf("URL prefix:\n");
    {
        xs *i = xs_strip_i(xs_readline(stdin));

        if (*i) {
            if (xs_endswith(i, "/"))
                i = xs_crop_i(i, 0, -1);

            srv_config = xs_dict_set(srv_config, "prefix", i);
        }
    }

    printf("Admin email address (optional):\n");
    {
        xs *i = xs_strip_i(xs_readline(stdin));

        srv_config = xs_dict_set(srv_config, "admin_email", i);
    }

    if (mkdirx(srv_basedir) == -1) {
        printf("ERROR: cannot create directory '%s'\n", srv_basedir);
        return 1;
    }

    xs *udir = xs_fmt("%s/user", srv_basedir);
    mkdirx(udir);

    xs *odir = xs_fmt("%s/object", srv_basedir);
    mkdirx(odir);

    xs *qdir = xs_fmt("%s/queue", srv_basedir);
    mkdirx(qdir);

    xs *ibdir = xs_fmt("%s/inbox", srv_basedir);
    mkdirx(ibdir);

    xs *gfn = xs_fmt("%s/greeting.html", srv_basedir);
    if ((f = fopen(gfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", gfn);
        return 1;
    }

    fwrite(greeting_html, strlen(greeting_html), 1, f);
    fclose(f);

    xs *sfn = xs_fmt("%s/style.css", srv_basedir);
    if ((f = fopen(sfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", sfn);
        return 1;
    }

    fwrite(default_css, strlen(default_css), 1, f);
    fclose(f);

    xs *cfn = xs_fmt("%s/server.json", srv_basedir);
    if ((f = fopen(cfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", cfn);
        return 1;
    }

    xs *j = xs_json_dumps_pp(srv_config, 4);
    fwrite(j, strlen(j), 1, f);
    fclose(f);

    printf("Done.\n");
    return 0;
}


void new_password(const char *uid, d_char **clear_pwd, d_char **hashed_pwd)
/* creates a random password */
{
    int rndbuf[3];

    xs_rnd_buf(rndbuf, sizeof(rndbuf));

    *clear_pwd  = xs_base64_enc((char *)rndbuf, sizeof(rndbuf));
    *hashed_pwd = hash_password(uid, *clear_pwd, NULL);
}


int adduser(const char *uid)
/* creates a new user */
{
    snac snac;
    xs *config = xs_dict_new();
    xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
    xs *pwd = NULL;
    xs *pwd_f = NULL;
    xs *key = NULL;
    FILE *f;

    if (uid == NULL) {
        printf("User id:\n");
        uid = xs_strip_i(xs_readline(stdin));
    }

    if (!validate_uid(uid)) {
        printf("ERROR: only alphanumeric characters and _ are allowed in user ids.\n");
        return 1;
    }

    if (user_open(&snac, uid)) {
        printf("ERROR: user '%s' already exists\n", uid);
        return 1;
    }

    new_password(uid, &pwd, &pwd_f);

    config = xs_dict_append(config, "uid",       uid);
    config = xs_dict_append(config, "name",      uid);
    config = xs_dict_append(config, "avatar",    "");
    config = xs_dict_append(config, "bio",       "");
    config = xs_dict_append(config, "cw",        "");
    config = xs_dict_append(config, "published", date);
    config = xs_dict_append(config, "passwd",    pwd_f);

    xs *basedir = xs_fmt("%s/user/%s", srv_basedir, uid);

    if (mkdirx(basedir) == -1) {
        printf("ERROR: cannot create directory '%s'\n", basedir);
        return 0;
    }

    const char *dirs[] = {
        "followers", "following", "muted", "hidden",
        "public", "private", "queue", "history",
        "static", NULL };
    int n;

    for (n = 0; dirs[n]; n++) {
        xs *d = xs_fmt("%s/%s", basedir, dirs[n]);
        mkdirx(d);
    }

    xs *cfn = xs_fmt("%s/user.json", basedir);

    if ((f = fopen(cfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", cfn);
        return 1;
    }
    else {
        xs *j = xs_json_dumps_pp(config, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }

    printf("\nCreating RSA key...\n");
    key = xs_evp_genkey(4096);
    printf("Done.\n");

    xs *kfn = xs_fmt("%s/key.json", basedir);

    if ((f = fopen(kfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", kfn);
        return 1;
    }
    else {
        xs *j = xs_json_dumps_pp(key, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }

    printf("\nUser password is %s\n", pwd);

    printf("\nGo to %s/%s and continue configuring your user there.\n", srv_baseurl, uid);

    return 0;
}


int resetpwd(snac *snac)
/* creates a new password for the user */
{
    xs *clear_pwd  = NULL;
    xs *hashed_pwd = NULL;
    xs *fn         = xs_fmt("%s/user.json", snac->basedir);
    FILE *f;
    int ret = 0;

    new_password(snac->uid, &clear_pwd, &hashed_pwd);

    snac->config = xs_dict_set(snac->config, "passwd", hashed_pwd);

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(snac->config, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);

        printf("New password for user %s is %s\n", snac->uid, clear_pwd);
    }
    else {
        printf("ERROR: cannot write to %s\n", fn);
        ret = 1;
    }

    return ret;
}
