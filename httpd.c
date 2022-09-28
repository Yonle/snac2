/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_socket.h"
#include "xs_httpd.h"

#include "snac.h"

/* susie.png */
const char *susie =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAUUlEQVQoz43R0QkAMQwCUDdw/y3dwE"
    "vsvzlL4X1IoQkAisKmwfAFT3RgJHbQezpSRoXEq"
    "eqCL9BJBf7h3QbOCCxV5EVWMEMwG7K1/WODtlvx"
    "AYTtEsDU9F34AAAAAElFTkSuQmCC";


int server_get_handler(d_char *req, char *q_path,
                       char **body, int *b_size, char **ctype)
/* basic server services */
{
    int status = 0;
    char *acpt = xs_dict_get(req, "accept");

    if (acpt == NULL)
        return 400;

    /* is it the server root? */
    if (*q_path == '\0') {
        /* try to open greeting.html */
        xs *fn = xs_fmt("%s/greeting.html", srv_basedir);
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            d_char *s = xs_readall(f);
            fclose(f);

            status = 200;

            /* does it have a %userlist% mark? */
            if (xs_str_in(s, "%userlist%") != -1) {
                char *host = xs_dict_get(srv_config, "host");
                xs *list = user_list();
                char *p, *uid;
                xs *ul = xs_str_new("<ul class=\"snac-user-list\">\n");

                p = list;
                while (xs_list_iter(&p, &uid)) {
                    snac snac;

                    if (user_open(&snac, uid)) {
                        xs *u = xs_fmt(
                            "<li><a href=\"%s\">@%s@%s (%s)</a></li>\n",
                                snac.actor, uid, host,
                                xs_dict_get(snac.config, "name"));

                        ul = xs_str_cat(ul, u);

                        user_free(&snac);
                    }
                }

                ul = xs_str_cat(ul, "</ul>\n");

                s = xs_replace_i(s, "%userlist%", ul);
            }

            *body = s;
        }
    }
    else
    if (strcmp(q_path, "/susie.png") == 0) {
        status = 200;
        *body  = xs_base64_dec(susie, b_size);
        *ctype = "image/png";
    }

    if (status != 0)
        srv_debug(1, xs_fmt("server_get_handler serving '%s' %d", q_path, status));

    return status;
}


void httpd_connection(int rs)
/* the connection loop */
{
    FILE *f;
    xs *req;
    char *method;
    int status  = 0;
    char *body  = NULL;
    int b_size  = 0;
    char *ctype = NULL;
    xs *headers = NULL;
    xs *q_path  = NULL;
    xs *payload = NULL;
    int p_size = 0;
    char *p;

    f = xs_socket_accept(rs);

    req = xs_httpd_request(f, &payload, &p_size);

    method = xs_dict_get(req, "method");
    q_path = xs_dup(xs_dict_get(req, "path"));

    /* crop the q_path from leading / and the prefix */
    if (xs_endswith(q_path, "/"))
        q_path = xs_crop(q_path, 0, -1);

    p = xs_dict_get(srv_config, "prefix");
    if (xs_startswith(q_path, p))
        q_path = xs_crop(q_path, strlen(p), 0);

    if (strcmp(method, "GET") == 0) {
        /* cascade through */
        if (status == 0)
            status = server_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = webfinger_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = activitypub_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = html_get_handler(req, q_path, &body, &b_size, &ctype);
    }
    else
    if (strcmp(method, "POST") == 0) {
        if (status == 0)
            status = activitypub_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);

        if (status == 0)
            status = html_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);
    }

    /* let's go */
    headers = xs_dict_new();

    /* unattended? it's an error */
    if (status == 0) {
        srv_debug(1, xs_fmt("httpd_connection unattended %s %s", method, q_path));
        status = 404;
    }

    if (status == 404)
        body = xs_str_new("<h1>404 Not Found</h1>");

    if (status == 400)
        body = xs_str_new("<h1>400 Bad Request</h1>");

    if (status == 303)
        headers = xs_dict_append(headers, "location", body);

    if (status == 401)
        headers = xs_dict_append(headers, "WWW-Authenticate", "Basic realm=\"IDENTIFY\"");

    if (ctype == NULL)
        ctype = "text/html; charset=utf-8";

    headers = xs_dict_append(headers, "content-type", ctype);
    headers = xs_dict_append(headers, "x-creator",    USER_AGENT);

    if (b_size == 0 && body != NULL)
        b_size = strlen(body);

    xs_httpd_response(f, status, headers, body, b_size);

    fclose(f);

    srv_archive("RECV", req, payload, p_size, status, headers, body, b_size);

    free(body);
}


void httpd(void)
/* starts the server */
{
    char *address;
    int port;
    int rs;

    address = xs_dict_get(srv_config, "address");
    port    = xs_number_get(xs_dict_get(srv_config, "port"));

    if ((rs = xs_socket_server(address, port)) == -1) {
        srv_log(xs_fmt("cannot bind socket to %s:%d", address, port));
        return;
    }

    srv_running = 1;

    srv_log(xs_fmt("httpd start %s:%d", address, port));

    for (;;) {
        httpd_connection(rs);
    }

    srv_log(xs_fmt("httpd stop %s:%d", address, port));
}
