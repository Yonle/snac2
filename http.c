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
    xs *hdrs;
    char *host;
    char *target;
    char *seckey;
    char *k, *v;
    d_char *response;

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
    {
        xs *s;

        if (body != NULL)
            s = xs_sha256_base64(body, b_size);
        else
            s = xs_sha256_base64("", 0);

        digest = xs_fmt("SHA-256=%s", s);
    }

    seckey = xs_dict_get(snac->key, "secret");

    {
        /* build the string to be signed */
        xs *s = xs_fmt("(request-target): %s /%s\n"
                       "host: %s\n"
                       "digest: %s\n"
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

    /* transfer the original headers */
    hdrs = xs_dict_new();
    while (xs_dict_iter(&headers, &k, &v))
        hdrs = xs_dict_append(hdrs, k, v);

    /* add the new headers */
    hdrs = xs_dict_append(hdrs, "content-type", "application/activity+json");
    hdrs = xs_dict_append(hdrs, "accept",       "application/activity+json");
    hdrs = xs_dict_append(hdrs, "date",         date);
    hdrs = xs_dict_append(hdrs, "signature",    signature);
    hdrs = xs_dict_append(hdrs, "digest",       digest);
    hdrs = xs_dict_append(hdrs, "host",         host);
    hdrs = xs_dict_append(hdrs, "user-agent",   "snac/2.x");

    response = xs_http_request(method, url, hdrs,
                           body, b_size, status, payload, p_size);

    srv_archive("SEND", hdrs, body, b_size, *status, response, *payload, *p_size);

    return response;
}
