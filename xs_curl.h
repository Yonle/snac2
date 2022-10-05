/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_CURL_H

#define _XS_CURL_H

d_char *xs_http_request(char *method, char *url, d_char *headers,
                        d_char *body, int b_size,
                        int *status, d_char **payload, int *p_size);

#ifdef XS_IMPLEMENTATION

#include <curl/curl.h>

static size_t _header_callback(char *buffer, size_t size,
                               size_t nitems, d_char **userdata)
{
    d_char *headers = *userdata;
    xs *l;

    /* get the line */
    l = xs_str_new(NULL);
    l = xs_append_m(l, buffer, size * nitems);
    l = xs_strip(l);

    /* only the HTTP/x 200 line and the last one doesn't have ': ' */
    if (xs_str_in(l, ": ") != -1) {
        xs *knv = xs_split_n(l, ": ", 1);

        xs_tolower(xs_list_get(knv, 0));

        headers = xs_dict_set(headers, xs_list_get(knv, 0), xs_list_get(knv, 1));
    }
    else
    if (xs_startswith(l, "HTTP/"))
        headers = xs_dict_set(headers, "_proto", l);

    *userdata = headers;

    return nitems * size;
}


struct _payload_data {
    char *data;
    int size;
    int offset;
};

static int _data_callback(void *buffer, size_t size,
                          size_t nitems, struct _payload_data *pd)
{
    int sz = size * nitems;

    /* open space */
    pd->size += sz;
    pd->data = realloc(pd->data, pd->size + 1);

    /* copy data */
    memcpy(pd->data + pd->offset, buffer, sz);
    pd->offset += sz;

    return sz;
}


static int _post_callback(char *buffer, size_t size,
                          size_t nitems, struct _payload_data *pd)
{
    /* size of data left */
    int sz = pd->size - pd->offset;

    /* if it's still bigger than the provided space, trim */
    if (sz > size * nitems)
        sz = size * nitems;

    memcpy(buffer, pd->data + pd->offset, sz);

    /* skip sent data */
    pd->offset += sz;

    return sz;
}


d_char *xs_http_request(char *method, char *url, d_char *headers,
                        d_char *body, int b_size,
                        int *status, d_char **payload, int *p_size)
/* does an HTTP request */
{
    d_char *response;
    CURL *curl;
    struct curl_slist *list = NULL;
    char *k, *v, *p;
    long lstatus;

    response = xs_dict_new();

    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, url);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    /* force HTTP/1.1 */
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    /* obey redirections */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* store response headers here */
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, _header_callback);

    struct _payload_data ipd = { NULL, 0, 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ipd);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  _data_callback);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        if (body != NULL) {
            if (b_size <= 0)
                b_size = xs_size(body);

            /* add the content-length header */
            char tmp[32];
            sprintf(tmp, "content-length: %d", b_size);
            list = curl_slist_append(list, tmp);

            struct _payload_data pd = { body, b_size, 0 };

            curl_easy_setopt(curl, CURLOPT_READDATA,     &pd);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, _post_callback);
        }
    }

    /* fill the request headers */
    p = headers;
    while (xs_dict_iter(&p, &k, &v)) {
        xs *h = xs_fmt("%s: %s", k, v);

        list = curl_slist_append(list, h);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    /* do it */
    curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);

    curl_easy_cleanup(curl);

    if (status != NULL)
        *status = (int) lstatus;

    if (p_size != NULL)
        *p_size = ipd.size;

    if (payload != NULL) {
        *payload = ipd.data;

        /* add an asciiz just in case (but not touching p_size) */
        if (ipd.data != NULL)
            ipd.data[ipd.size] = '\0';
    }
    else
        free(ipd.data);

    return response;
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_CURL_H */
