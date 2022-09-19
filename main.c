/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"

#include "snac.h"

int main(int argc, char *argv[])
{
    snac snac;

    printf("%s\n", tid());

    srv_open("/home/angel/lib/snac/comam.es");

    user_open(&snac, "mike");
    snac_log(&snac, xs_str_new("ok"));

    {
        xs *list = user_list();
        char *p, *uid;

        p = list;
        while (xs_list_iter(&p, &uid)) {
            user_open(&snac, uid);

            printf("%s (%s)\n", uid, xs_dict_get(snac.config, "name"));

            user_free(&snac);
        }
    }

    return 0;
}
