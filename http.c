/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_openssl.h"
#include "xs_curl.h"

#include "snac.h"

d_char *http_signed_request(snac *snac, char *method, char *url,
                        d_char *headers,
                        d_char *body, int b_size,
                        int *status, d_char **payload, int *p_size)
/* does an HTTP request */
{
    return xs_http_request(method, url, headers,
                           body, b_size, status, payload, p_size);
}
