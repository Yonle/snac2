/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_HTTPD_H

#define _XS_HTTPD_H

d_char *xs_url_dec(char *str);
d_char *xs_url_vars(char *str);
d_char *xs_httpd_request(FILE *f, d_char **payload, int *p_size);
void xs_httpd_response(FILE *f, int status, d_char *headers, char *body, int b_size);


#ifdef XS_IMPLEMENTATION

d_char *xs_url_dec(char *str)
/* decodes an URL */
{
    d_char *s = xs_str_new(NULL);

    while (*str) {
        if (*str == '%') {
            int i;

            if (sscanf(str + 1, "%02x", &i) == 1) {
                unsigned char uc = i;

                s = xs_append_m(s, (char *)&uc, 1);
                str += 2;
            }
        }
        else
        if (*str == '+')
            s = xs_append_m(s, " ", 1);
        else
            s = xs_append_m(s, str, 1);

        str++;
    }

    return s;
}


d_char *xs_url_vars(char *str)
/* parse url variables */
{
    d_char *vars;

    vars = xs_dict_new();

    if (str != NULL) {
        char *v, *l;
        xs *args;

        /* split by arguments */
        args = xs_split(str, "&");

        l = args;
        while (xs_list_iter(&l, &v)) {
            xs *kv = xs_split_n(v, "=", 2);

            if (xs_list_len(kv) == 2)
                vars = xs_dict_append(vars,
                    xs_list_get(kv, 0), xs_list_get(kv, 1));
        }
    }

    return vars;
}


d_char *xs_httpd_request(FILE *f, d_char **payload, int *p_size)
/* processes an httpd connection */
{
    d_char *req = NULL;
    xs *q_vars  = NULL;
    xs *p_vars  = NULL;
    xs *l1, *l2;
    char *v;

    xs_socket_timeout(fileno(f), 2.0, 0.0);

    /* read the first line and split it */
    l1 = xs_strip(xs_readline(f));
    l2 = xs_split(l1, " ");

    if (xs_list_len(l2) != 3) {
        /* error or timeout */
        return NULL;
    }

    req = xs_dict_new();

    req = xs_dict_append(req, "method", xs_list_get(l2, 0));
    req = xs_dict_append(req, "proto",  xs_list_get(l2, 2));

    {
        /* split the path with its optional variables */
        xs *udp = xs_url_dec(xs_list_get(l2, 1));
        xs *pnv = xs_split_n(udp, "?", 1);

        /* store the path */
        req = xs_dict_append(req, "path", xs_list_get(pnv, 0));

        /* get the variables */
        q_vars = xs_url_vars(xs_list_get(pnv, 1));
    }

    /* read the headers */
    for (;;) {
        xs *l, *p = NULL;

        l = xs_strip(xs_readline(f));

        /* done with the header? */
        if (strcmp(l, "") == 0)
            break;

        /* split header and content */
        p = xs_split_n(l, ": ", 1);

        if (xs_list_len(p) == 2)
            req = xs_dict_append(req, xs_tolower(xs_list_get(p, 0)), xs_list_get(p, 1));
    }

    xs_socket_timeout(fileno(f), 5.0, 0.0);

    if ((v = xs_dict_get(req, "content-length")) != NULL) {
        /* if it has a payload, load it */
        *p_size  = atoi(v);
        *payload = xs_read(f, *p_size);
    }

    /* is the payload form urlencoded variables? */
    v = xs_dict_get(req, "content-type");

    if (v && strcmp(v, "application/x-www-form-urlencoded") == 0) {
        xs *upl = xs_url_dec(*payload);
        p_vars  = xs_url_vars(upl);
    }
    else
        p_vars = xs_dict_new();

    req = xs_dict_append(req, "q_vars",  q_vars);
    req = xs_dict_append(req, "p_vars",  p_vars);

    if (errno) {
        free(req);
        req = NULL;
    }

    return req;
}


void xs_httpd_response(FILE *f, int status, d_char *headers, char *body, int b_size)
/* sends an httpd response */
{
    xs *proto;
    char *p, *k, *v;

    proto = xs_fmt("HTTP/1.1 %d", status);
    fprintf(f, "%s\r\n", proto);

    p = headers;
    while (xs_dict_iter(&p, &k, &v)) {
        fprintf(f, "%s: %s\r\n", k, v);
    }

    if (b_size != 0)
        fprintf(f, "content-length: %d\r\n", b_size);

    fprintf(f, "\r\n");

    if (body != NULL && b_size != 0)
        fwrite(body, b_size, 1, f);
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_HTTPD_H */
