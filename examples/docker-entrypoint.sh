if [ ! -e /data/data/server.json ]
then
    echo -ne "0.0.0.0\r\n8001\r\nlocalhost\r\n\r\n" | /build/snac init /data/data
    /build/snac adduser /data/data testuser
fi
SSLKEYLOGFILE=/data/key /build/snac httpd /data/data
