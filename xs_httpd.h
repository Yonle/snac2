/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_HTTPD_H

#define _XS_HTTPD_H

xs_str *xs_url_dec(char *str);
xs_dict *xs_url_vars(char *str);
xs_dict *xs_httpd_request(FILE *f, xs_str **payload, int *p_size);
void xs_httpd_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size);


#ifdef XS_IMPLEMENTATION

xs_str *xs_url_dec(char *str)
/* decodes an URL */
{
    xs_str *s = xs_str_new(NULL);

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


xs_dict *xs_url_vars(char *str)
/* parse url variables */
{
    xs_dict *vars;

    vars = xs_dict_new();

    if (str != NULL) {
        /* split by arguments */
        xs *args = xs_split(str, "&");

        xs_list *l;
        xs_val *v;

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


xs_dict *_xs_multipart_form_data(xs_str *payload, int p_size, char *header)
/* parses a multipart/form-data payload */
{
    xs *boundary = NULL;
    int offset = 0;
    int bsz;
    char *p;

    /* build the boundary string */
    {
        xs *l1 = xs_split(header, "=");

        if (xs_list_len(l1) != 2)
            return NULL;

        boundary = xs_fmt("--%s", xs_list_get(l1, 1));
    }

    bsz = strlen(boundary);

    xs_dict *p_vars = xs_dict_new();

    /* iterate searching the boundaries */
    while ((p = xs_memmem(payload + offset, p_size - offset, boundary, bsz)) != NULL) {
        xs *s1 = NULL;
        xs *l1 = NULL;
        char *vn = NULL;
        char *fn = NULL;
        char *q;
        int po, ps;

        /* final boundary? */
        p += bsz;

        if (p[0] == '-' && p[1] == '-')
            break;

        /* skip the \r\n */
        p += 2;

        /* now on a Content-Disposition... line; get it */
        q = strchr(p, '\r');
        s1 = xs_realloc(NULL, q - p + 1);
        memcpy(s1, p, q - p);
        s1[q - p] = '\0';

        /* move on (over a \r\n) */
        p = q;

        /* split by " like a primitive man */
        l1 = xs_split(s1, "\"");

        /* get the variable name */
        vn = xs_list_get(l1, 1);

        /* is it an attached file? */
        if (xs_list_len(l1) >= 4 && strcmp(xs_list_get(l1, 2), "; filename=") == 0) {
            /* get the file name */
            fn = xs_list_get(l1, 3);
        }

        /* find the start of the part content */
        if ((p = xs_memmem(p, p_size - (p - payload), "\r\n\r\n", 4)) == NULL)
            break;

        p += 4;

        /* find the next boundary */
        if ((q = xs_memmem(p, p_size - (p - payload), boundary, bsz)) == NULL)
            break;

        po = p - payload;
        ps = q - p - 2;     /* - 2 because the final \r\n */

        /* is it a filename? */
        if (fn != NULL) {
            /* p_var value is a list */
            xs *l1 = xs_list_new();
            xs *vpo = xs_number_new(po);
            xs *vps = xs_number_new(ps);

            l1 = xs_list_append(l1, fn);
            l1 = xs_list_append(l1, vpo);
            l1 = xs_list_append(l1, vps);

            p_vars = xs_dict_append(p_vars, vn, l1);
        }
        else {
            /* regular variable; just copy */
            xs *vc = xs_realloc(NULL, ps + 1);
            memcpy(vc, payload + po, ps);
            vc[ps] = '\0';

            p_vars = xs_dict_append(p_vars, vn, vc);
        }

        /* move on */
        offset = q - payload;
    }

    return p_vars;
}


xs_dict *xs_httpd_request(FILE *f, xs_str **payload, int *p_size)
/* processes an httpd connection */
{
    xs *q_vars = NULL;
    xs *p_vars = NULL;
    xs *l1, *l2;
    char *v;

    xs_socket_timeout(fileno(f), 2.0, 0.0);

    /* read the first line and split it */
    l1 = xs_strip_i(xs_readline(f));
    l2 = xs_split(l1, " ");

    if (xs_list_len(l2) != 3) {
        /* error or timeout */
        return NULL;
    }

    xs_dict *req = xs_dict_new();

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

        l = xs_strip_i(xs_readline(f));

        /* done with the header? */
        if (strcmp(l, "") == 0)
            break;

        /* split header and content */
        p = xs_split_n(l, ": ", 1);

        if (xs_list_len(p) == 2)
            req = xs_dict_append(req, xs_tolower_i(xs_list_get(p, 0)), xs_list_get(p, 1));
    }

    xs_socket_timeout(fileno(f), 5.0, 0.0);

    if ((v = xs_dict_get(req, "content-length")) != NULL) {
        /* if it has a payload, load it */
        *p_size  = atoi(v);
        *payload = xs_read(f, p_size);
    }

    v = xs_dict_get(req, "content-type");

    if (v && strcmp(v, "application/x-www-form-urlencoded") == 0) {
        xs *upl = xs_url_dec(*payload);
        p_vars  = xs_url_vars(upl);
    }
    else
    if (v && xs_startswith(v, "multipart/form-data")) {
        p_vars = _xs_multipart_form_data(*payload, *p_size, v);
    }
    else
        p_vars = xs_dict_new();

    req = xs_dict_append(req, "q_vars",  q_vars);
    req = xs_dict_append(req, "p_vars",  p_vars);

    if (errno)
        req = xs_free(req);

    return req;
}


void xs_httpd_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size)
/* sends an httpd response */
{
    xs *proto;
    xs_dict *p;
    xs_str *k;
    xs_val *v;

    proto = xs_fmt("HTTP/1.1 %d ", status);
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
