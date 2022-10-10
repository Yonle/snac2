/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"

#include "snac.h"

int usage(void)
{
    printf("snac " VERSION " - A simple, minimalistic ActivityPub instance\n");
    printf("Copyright (c) 2022 grunfink - MIT license\n");
    printf("\n");
    printf("Commands:\n");
    printf("\n");
    printf("init [{basedir}]                 Initializes the database\n");
    printf("adduser {basedir} [{uid}]        Adds a new user\n");
    printf("httpd {basedir}                  Starts the HTTPD daemon\n");
    printf("purge {basedir}                  Purges old data\n");
    printf("webfinger {basedir} {user}       Queries about a @user@host or actor\n");
    printf("queue {basedir} {uid}            Processes a user queue\n");
    printf("follow {basedir} {uid} {actor}   Follows an actor\n");

//    printf("check {basedir} [{uid}]          Checks the database\n");

//    printf("update {basedir} {uid}           Sends a user update to followers\n");
//    printf("passwd {basedir} {uid}           Sets the password for {uid}\n");
//    printf("unfollow {basedir} {uid} {actor} Unfollows an actor\n");
//    printf("mute {basedir} {uid} {actor}     Mutes an actor\n");
//    printf("unmute {basedir} {uid} {actor}   Unmutes an actor\n");
//    printf("like {basedir} {uid} {url}       Likes an url\n");
//    printf("announce {basedir} {uid} {url}   Announces (boosts) an url\n");
//    printf("note {basedir} {uid} {'text'}    Sends a note to followers\n");

    printf("request {basedir} {uid} {url}    Requests an object\n");
    printf("actor {basedir} {uid} {url}      Requests an actor\n");
    printf("note {basedir} {uid} {'text'}    Sends a note to followers\n");

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

        return initdb(basedir);
    }

    if ((basedir = GET_ARGV()) == NULL)
        return usage();

    if (!srv_open(basedir)) {
        srv_log(xs_fmt("error opening database at %s", basedir));
        return 1;
    }

    if (strcmp(cmd, "adduser") == 0) {
        user = GET_ARGV();

        return adduser(user);

        return 0;
    }

    if (strcmp(cmd, "httpd") == 0) {
        httpd();
        return 0;
    }

    if (strcmp(cmd, "purge") == 0) {
        /* iterate all users */
        xs *list = user_list();
        char *p, *uid;

        p = list;
        while (xs_list_iter(&p, &uid)) {
            if (user_open(&snac, uid)) {
                purge(&snac);
                user_free(&snac);
            }
        }

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

    if (strcmp(cmd, "announce") == 0) {
        xs *msg = msg_admiration(&snac, url, "Announce");

        if (msg != NULL) {
            post(&snac, msg);

            if (dbglevel) {
                xs *j = xs_json_dumps_pp(msg, 4);
                printf("%s\n", j);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "follow") == 0) {
        xs *msg = msg_follow(&snac, url);

        if (msg != NULL) {
            char *actor = xs_dict_get(msg, "object");

            following_add(&snac, actor, msg);

            enqueue_output(&snac, msg, actor, 0);

            if (dbglevel) {
                xs *j = xs_json_dumps_pp(msg, 4);
                printf("%s\n", j);
            }
        }

        return 0;
    }

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

        if (valid_status(status)) {
            xs *j = xs_json_dumps_pp(data, 4);
            printf("%s\n", j);
        }

        return 0;
    }

    if (strcmp(cmd, "note") == 0) {
        xs *content = NULL;
        xs *msg = NULL;
        xs *c_msg = NULL;
        char *in_reply_to = GET_ARGV();

        if (strcmp(url, "-") == 0) {
            /* get the content from an editor */
            FILE *f;

            system("$EDITOR /tmp/snac-edit.txt");

            if ((f = fopen("/tmp/snac-edit.txt", "r")) != NULL) {
                content = xs_readall(f);
                fclose(f);

                unlink("/tmp/snac-edit.txt");
            }
            else {
                printf("Nothing to send\n");
                return 1;
            }
        }
        else
            content = xs_dup(url);

        msg = msg_note(&snac, content, NULL, in_reply_to, NULL);

        c_msg = msg_create(&snac, msg);

        if (dbglevel) {
            xs *j = xs_json_dumps_pp(c_msg, 4);
            printf("%s\n", j);
        }

        post(&snac, c_msg);

        timeline_add(&snac, xs_dict_get(msg, "id"), msg, in_reply_to, NULL);

        return 0;
    }

    return 0;
}
