/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_socket.h"
#include "xs_httpd.h"

#include "snac.h"


void server_get_handler(d_char *req, int *status, char **body, int *b_size, char **ctype)
/* basic server services */
{
    char *req_hdrs = xs_dict_get(req, "headers");
    char *acpt     = xs_dict_get(req_hdrs, "accept");
    char *q_path   = xs_dict_get(req_hdrs, "path");

    if (acpt == NULL) {
        *status = 400;
        return;
    }

    /* get the server prefix */
    char *prefix = xs_dict_get(srv_config, "prefix");
    if (*prefix == '\0')
        prefix = "/";

    /* is it the server root? */
    if (strcmp(q_path, prefix) == 0) {
        /* try to open greeting.html */
        xs *fn = xs_fmt("%s/greeting.html", srv_basedir);
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            d_char *s = xs_readall(f);
            fclose(f);

            *status = 200;

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

                s = xs_replace(s, "%userlist%", ul);
            }

            *body = s;
        }
    }
}

void httpd_connection(int rs)
/* the connection loop */
{
    FILE *f;
    xs *req;
    char *req_hdrs;
    char *method;
    int status  = 0;
    char *body  = NULL;
    int b_size  = 0;
    char *ctype = NULL;
    xs *headers = NULL;

    f = xs_socket_accept(rs);

    req = xs_httpd_request(f);

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("%s\n", j);
    }

    req_hdrs = xs_dict_get(req, "headers");

    method = xs_dict_get(req_hdrs, "method");

    if (strcmp(method, "GET") == 0) {
        /* cascade through */
        if (status == 0)
            server_get_handler(req, &status, &body, &b_size, &ctype);
    }
    else
    if (strcmp(method, "POST") == 0) {
    }

    /* let's go */
    headers = xs_dict_new();

    /* unattended? it's an error */
    if (status == 0)
        status = 404;

    if (status == 404)
        body = "<h1>404 Not Found</h1>";

    if (status == 400)
        body = "<h1>400 Bad Request</h1>";

    if (status == 303)
        headers = xs_dict_append(headers, "location", body);

    if (status == 401)
        headers = xs_dict_append(headers, "WWW-Authenticate", "Basic realm=\"IDENTIFY\"");

    if (ctype == NULL)
        ctype = "text/html; charset=utf-8";

    headers = xs_dict_append(headers, "content-type", ctype);
    headers = xs_dict_append(headers, "x-creator",    "snac/2.x");

    if (b_size == 0 && body != NULL)
        b_size = strlen(body);

    xs_httpd_response(f, status, headers, body, b_size);

    fclose(f);

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
