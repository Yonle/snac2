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

    return 0;
}
