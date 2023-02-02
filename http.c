/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_openssl.h"
#include "xs_curl.h"
#include "xs_time.h"
#include "xs_json.h"

#include "snac.h"

xs_dict *http_signed_request_raw(const char *keyid, const char *seckey,
                            const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout)
/* does a signed HTTP request */
{
    xs *l1 = NULL;
    xs *date = NULL;
    xs *digest = NULL;
    xs *s64 = NULL;
    xs *signature = NULL;
    xs *hdrs = NULL;
    char *host;
    char *target;
    char *k, *v;
    xs_dict *response;

    date = xs_str_utctime(0, "%a, %d %b %Y %H:%M:%S GMT");

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

    {
        /* build the string to be signed */
        xs *s = xs_fmt("(request-target): %s /%s\n"
                       "host: %s\n"
                       "digest: %s\n"
                       "date: %s",
                    strcmp(method, "POST") == 0 ? "post" : "get",
                    target, host, digest, date);

        s64 = xs_evp_sign(seckey, s, strlen(s));
    }

    /* build now the signature header */
    signature = xs_fmt("keyId=\"%s#main-key\","
                       "algorithm=\"rsa-sha256\","
                       "headers=\"(request-target) host digest date\","
                       "signature=\"%s\"",
                        keyid, s64);

    /* transfer the original headers */
    hdrs = xs_dict_new();
    while (xs_dict_iter(&headers, &k, &v))
        hdrs = xs_dict_append(hdrs, k, v);

    /* add the new headers */
    if (strcmp(method, "POST") == 0)
        hdrs = xs_dict_append(hdrs, "content-type", "application/activity+json");
    else
        hdrs = xs_dict_append(hdrs, "accept",       "application/activity+json");

    hdrs = xs_dict_append(hdrs, "date",         date);
    hdrs = xs_dict_append(hdrs, "signature",    signature);
    hdrs = xs_dict_append(hdrs, "digest",       digest);
    hdrs = xs_dict_append(hdrs, "host",         host);
    hdrs = xs_dict_append(hdrs, "user-agent",   USER_AGENT);

    response = xs_http_request(method, url, hdrs,
                           body, b_size, status, payload, p_size, timeout);

    srv_archive("SEND", hdrs, body, b_size, *status, response, *payload, *p_size);

    return response;
}


xs_dict *http_signed_request(snac *snac, const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout)
/* does a signed HTTP request */
{
    char *seckey = xs_dict_get(snac->key, "secret");
    xs_dict *response;

    response = http_signed_request_raw(snac->actor, seckey, method, url,
                headers, body, b_size, status, payload, p_size, timeout);

    return response;
}


static int _check_signature(snac *snac, char *req, char **err)
/* check the signature */
{
    char *sig_hdr = xs_dict_get(req, "signature");
    xs *keyId = NULL;
    xs *headers = NULL;
    xs *signature = NULL;
    xs *created = NULL;
    xs *expires = NULL;
    char *pubkey;
    char *p;

    {
        /* extract the values */
        xs *l = xs_split(sig_hdr, ",");
        char *v;

        p = l;
        while (xs_list_iter(&p, &v)) {
            if (xs_startswith(v, "keyId"))
                keyId = xs_crop_i(xs_dup(v), 7, -1);
            else
            if (xs_startswith(v, "headers"))
                headers = xs_crop_i(xs_dup(v), 9, -1);
            else
            if (xs_startswith(v, "signature"))
                signature = xs_crop_i(xs_dup(v), 11, -1);
            else
            if (xs_startswith(v, "created"))
                created = xs_crop_i(xs_dup(v), 9, -1);
            else
            if (xs_startswith(v, "expires"))
                expires = xs_crop_i(xs_dup(v), 9, -1);
        }
    }

    if (keyId == NULL || headers == NULL || signature == NULL) {
        *err = xs_fmt("bad signature header");
        return 0;
    }

    /* strip the # from the keyId */
    if ((p = strchr(keyId, '#')) != NULL)
        *p = '\0';

    xs *actor = NULL;

    if (!valid_status(actor_request(snac, keyId, &actor))) {
        *err = xs_fmt("unknown actor %s", keyId);
        return 0;
    }

    if ((p = xs_dict_get(actor, "publicKey")) == NULL ||
        ((pubkey = xs_dict_get(p, "publicKeyPem")) == NULL)) {
        *err = xs_fmt("cannot get pubkey from %s", keyId);
        return 0;
    }

    /* now build the string to be signed */
    xs *sig_str = xs_str_new(NULL);

    {
        xs *l = xs_split(headers, " ");
        char *v;

        p = l;
        while (xs_list_iter(&p, &v)) {
            char *hc;
            xs *ss = NULL;

            if (*sig_str != '\0')
                sig_str = xs_str_cat(sig_str, "\n");

            if (strcmp(v, "(request-target)") == 0) {
                ss = xs_fmt("%s: post %s", v, xs_dict_get(req, "path"));
            }
            else
            if (strcmp(v, "(created)") == 0) {
                ss = xs_fmt("%s: %s", v, created);
            }
            else
            if (strcmp(v, "(expires)") == 0) {
                ss = xs_fmt("%s: %s", v, expires);
            }
            else {
                /* add the header */
                if ((hc = xs_dict_get(req, v)) == NULL) {
                    *err = xs_fmt("cannot find header '%s'", v);
                    return 0;
                }

                ss = xs_fmt("%s: %s", v, hc);
            }

            sig_str = xs_str_cat(sig_str, ss);
        }
    }

    if (xs_evp_verify(pubkey, sig_str, strlen(sig_str), signature) != 1) {
        *err = xs_fmt("RSA verify error %s", keyId);
        return 0;
    }

    return 1;
}


int check_signature(snac *snac, char *req)
/* checks the signature and archives the error */
{
    int ret;
    xs *err = NULL;

    if ((ret = _check_signature(snac, req, &err)) == 0) {
        snac_debug(snac, 1, xs_fmt("check_signature %s", err));

        xs *ntid = tid(0);
        xs *fn   = xs_fmt("%s/error/check_signature_%s", srv_basedir, ntid);
        FILE *f;

        if ((f = fopen(fn, "w")) != NULL) {
            fprintf(f, "Error: %s\nRequest headers:\n", err);

            xs *j = xs_json_dumps_pp(req, 4);

            fwrite(j, strlen(j), 1, f);
            fclose(f);
        }
    }

    return ret;
}
