/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_SOCKET_H

#define _XS_SOCKET_H

int xs_socket_timeout(int s, float rto, float sto);
int xs_socket_server(char *addr, int port);
FILE *xs_socket_accept(int rs);


#ifdef XS_IMPLEMENTATION

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>


int xs_socket_timeout(int s, float rto, float sto)
/* sets the socket timeout in seconds */
{
    struct timeval tv;
    int ret = 0;

    if (rto > 0.0) {
        tv.tv_sec  = (int)rto;
        tv.tv_usec = (int)((rto - (float)(int)rto) * 1000000.0);

        ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    }

    if (sto > 0.0) {
        tv.tv_sec  = (int)sto;
        tv.tv_usec = (int)((sto - (float)(int)sto) * 1000000.0);

        ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
    }

    return ret;
}


int xs_socket_server(char *addr, int port)
/* opens a server socket */
{
    int rs = -1;
    struct sockaddr_in host;

    memset(&host, '\0', sizeof(host));

    if (addr != NULL) {
        struct hostent *he;

        if ((he = gethostbyname(addr)) != NULL)
            memcpy(&host.sin_addr, he->h_addr_list[0], he->h_length);
        else
            goto end;
    }

    host.sin_family = AF_INET;
    host.sin_port   = htons(port);

    if ((rs = socket(AF_INET, SOCK_STREAM, 0)) != -1) {
        /* reuse addr */
        int i = 1;
        setsockopt(rs, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof(i));

        if (bind(rs, (struct sockaddr *)&host, sizeof(host)) == -1) {
            close(rs);
            rs = -1;
        }
        else
            listen(rs, SOMAXCONN);
    }

end:
    return rs;
}


FILE *xs_socket_accept(int rs)
/* accepts an incoming connection */
{
    int cs = -1;
    struct sockaddr_in host;
    socklen_t l = sizeof(host);

    cs = accept(rs, (struct sockaddr *)&host, &l);

    return cs == -1 ? NULL : fdopen(cs, "r+");
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_SOCKET_H */