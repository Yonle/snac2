/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"

#include "snac.h"

int usage(void)
{
    printf("snac - A simple, minimalistic ActivityPub instance\n");
    printf("Copyright (c) 2022 grunfink - MIT license\n");
    printf("\n");
    printf("Commands:\n");
    printf("\n");
    printf("init [{basedir}]                 Initializes the database\n");
    printf("httpd {basedir}                  Starts the HTTPD daemon\n");
    printf("webfinger {basedir} {user}       Queries about a @user@host or actor\n");
    printf("queue {basedir} {uid}            Processes a user queue\n");
//    printf("check {basedir} [{uid}]          Checks the database\n");
//    printf("purge {basedir} [{uid}]          Purges old data\n");
//    printf("adduser {basedir} [{uid}]        Adds a new user\n");

//    printf("update {basedir} {uid}           Sends a user update to followers\n");
//    printf("passwd {basedir} {uid}           Sets the password for {uid}\n");
//    printf("follow {basedir} {uid} {actor}   Follows an actor\n");
//    printf("unfollow {basedir} {uid} {actor} Unfollows an actor\n");
//    printf("mute {basedir} {uid} {actor}     Mutes an actor\n");
//    printf("unmute {basedir} {uid} {actor}   Unmutes an actor\n");
//    printf("like {basedir} {uid} {url}       Likes an url\n");
//    printf("announce {basedir} {uid} {url}   Announces (boosts) an url\n");
//    printf("note {basedir} {uid} {'text'}    Sends a note to followers\n");

    printf("request {basedir} {uid} {url}    Requests an object\n");
    printf("actor {basedir} {uid} {url}      Requests an actor\n");

    return 1;
}


char *get_argv(int *argi, int argc, char *argv[])
{
    if (*argi < argc)
        return argv[(*argi)++];
    else
        return NULL;
}


#define GET_ARGV() get_argv(&argi, argc, argv)

int main(int argc, char *argv[])
{
    char *cmd;
    char *basedir;
    char *user;
    char *url;
    int argi = 1;
    snac snac;

    if ((cmd = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "init") == 0) {
        /* initialize the database */
        /* ... */
        basedir = GET_ARGV();

        return 0;
    }

    if ((basedir = GET_ARGV()) == NULL)
        return usage();

    if (!srv_open(basedir)) {
        srv_log(xs_fmt("error opening database at %s", basedir));
        return 1;
    }

    if (strcmp(cmd, "httpd") == 0) {
        httpd();
        return 0;
    }

    if ((user = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "webfinger") == 0) {
        xs *actor = NULL;
        xs *uid = NULL;
        int status;

        status = webfinger_request(user, &actor, &uid);

        printf("status: %d\n", status);
        if (actor != NULL)
            printf("actor: %s\n", actor);
        if (uid != NULL)
            printf("uid: %s\n", uid);

        return 0;
    }

    if (!user_open(&snac, user)) {
        printf("error in user '%s'\n", user);
        return 1;
    }

    if (strcmp(cmd, "queue") == 0) {
        process_queue(&snac);
        return 0;
    }

    if ((url = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "request") == 0) {
        int status;
        xs *data = NULL;

        status = activitypub_request(&snac, url, &data);

        printf("status: %d\n", status);
        if (valid_status(status)) {

            xs *j = xs_json_dumps_pp(data, 4);
            printf("%s\n", j);
        }

        return 0;
    }

    if (strcmp(cmd, "actor") == 0) {
        int status;
        xs *data = NULL;

        status = actor_request(&snac, url, &data);

        printf("status: %d\n", status);

        return 0;
    }

    return 0;
}


#if 0
{
    snac snac;

    printf("%s\n", tid(0));

    srv_open("/home/angel/lib/snac/comam.es/");

    user_open(&snac, "mike");

    xs *headers = xs_dict_new();
    int status;
    d_char *payload;
    int p_size;
    xs *response;

    response = http_signed_request(&snac, "GET", "https://mastodon.social/users/VictorMoral",
        headers, NULL, 0, &status, &payload, &p_size);

    {
        xs *j1 = xs_json_dumps_pp(response, 4);
        printf("response:\n%s\n", j1);
        printf("payload:\n%s\n", payload);
    }

    {
        xs *list = queue(&snac);
        char *p, *fn;

        p = list;
        while (xs_list_iter(&p, &fn)) {
            xs *obj;

            obj = dequeue(&snac, fn);
            printf("%s\n", xs_dict_get(obj, "actor"));
        }
    }

#if 0
    {
        xs *list = follower_list(&snac);
        char *p, *obj;

        p = list;
        while (xs_list_iter(&p, &obj)) {
            char *actor = xs_dict_get(obj, "actor");
            printf("%s\n", actor);
        }
    }

    {
        xs *list = timeline_list(&snac);
        char *p, *fn;

        p = list;
        while (xs_list_iter(&p, &fn)) {
            xs *tle = timeline_get(&snac, fn);

            printf("%s\n", xs_dict_get(tle, "id"));
        }
    }

    {
        xs *list = user_list();
        char *p, *uid;

        p = list;
        while (xs_list_iter(&p, &uid)) {
            if (user_open(&snac, uid)) {
                printf("%s (%s)\n", uid, xs_dict_get(snac.config, "name"));
                user_free(&snac);
            }
        }
    }
#endif

    return 0;
}
#endif
