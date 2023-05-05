#! /bin/sh
if [ ! -e /data/data/server.json ]
then
    echo -ne "0.0.0.0\r\n8001\r\nlocalhost\r\n\r\n\r\n" | snac init /data/data
    snac adduser /data/data testuser
fi
SSLKEYLOGFILE=/data/key snac httpd /data/data
