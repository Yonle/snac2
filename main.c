/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"

#include "snac.h"

int usage(void)
{
    printf("usage:\n");
    return 1;
}


int main(int argc, char *argv[])
{
    char *cmd;
    char *basedir;
    int argi = 1;

    argc--;
    if (argc < argi)
        return usage();

    cmd = argv[argi++];

    if (strcmp(cmd, "init") == 0) {
        return 0;
    }

    if (argc < argi)
        return usage();

    basedir = argv[argi++];

    if (!srv_open(basedir)) {
        srv_log(xs_fmt("error opening database at %s", basedir));
        return 1;
    }

    if (strcmp(cmd, "httpd") == 0) {
        httpd();
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
