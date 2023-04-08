/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_time.h"

#include "snac.h"

int mastoapi_post_handler(xs_dict *req, char *q_path, char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    int status = 404;

    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    xs *j = xs_json_dumps_pp(req, 4);
    printf("%s\n", j);
    printf("%s\n", payload);

    return status;
}
