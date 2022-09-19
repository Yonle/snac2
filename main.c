/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"

#include "snac.h"

int main(int argc, char *argv[])
{
    snac snac;

    printf("%s\n", tid());

    srv_open("/home/angel/lib/snac/comam.es");

    snac_open(&snac, "mike");
    snac_log(&snac, xs_str_new("ok"));

    char *passwd = xs_dict_get(snac.config, "passwd");
    printf("%d\n", check_password("mike", "1234", passwd));

    return 0;
}
