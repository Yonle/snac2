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
/* does a signed HTTP request */
{
    xs *l1;
    xs *date;
    xs *digest;
    xs *s64;
    xs *signature;
    char *host;
    char *target;
    char *seckey;

    date = xs_utc_time("%a, %d %b %Y %H:%M:%S GMT");

    {
        xs *s = xs_replace(url, "https:/" "/", "");
        l1 = xs_split_n(s, "/", 1);
    }

    /* strip the url to get host and target */
    host = xs_list_get(l1, 0);

    if (xs_list_len(l1) == 2)
        target = xs_list_get(l1, 1);
    else
        target = "";

    /* digest */
    if (body != NULL)
        digest = xs_sha256_hex(body, b_size);
    else
        digest = xs_sha256_hex("", 0);

    seckey = xs_dict_get(snac->key, "secret");

    {
        /* build the string to be signed */
        xs *s = xs_fmt("(request-target): %s /%s\n"
                       "host: %s\n"
                       "digest: SHA-256=%s\n"
                       "date: %s",
                    strcmp(method, "POST") == 0 ? "post" : "get",
                    target, host, digest, date);

        s64 = xs_rsa_sign(seckey, s, strlen(s));
    }

    /* build now the signature header */
    signature = xs_fmt("keyId=\"%s#main-key\","
                       "algorithm=\"rsa-sha256\","
                       "headers=\"(request-target) host digest date\","
                       "signature=\"%s\"",
                        snac->actor, s64);

    /* now add all these things to the headers */
    headers = xs_dict_append(headers, "content-type", "application/activity+json");
    headers = xs_dict_append(headers, "date",         date);
    headers = xs_dict_append(headers, "signature",    signature);
    headers = xs_dict_append(headers, "digest",       digest);
    headers = xs_dict_append(headers, "user-agent",   "snac/2.x");

//    return xs_http_request(method, url, headers,
//                           body, b_size, status, payload, p_size);
    return NULL;
}
