/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_openssl.h"
#include "xs_socket.h"
#include "xs_httpd.h"

#include "snac.h"


void httpd_connection(int rs)
/* the connection loop */
{
    FILE *f;
    xs *req;

    f = xs_socket_accept(rs);

    req = xs_httpd_request(f);

    fclose(f);
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
