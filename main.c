/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"

#include "snac.h"

int main(int argc, char *argv[])
{
    snac snac;

    printf("%s\n", tid(0));

    srv_open("/home/angel/lib/snac/comam.es/");

    user_open(&snac, "mike");

    {
        xs *list = queue(&snac);
        char *p, *fn;

        p = list;
        while (xs_list_iter(&p, &fn)) {
            printf("%s\n", fn);
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
