CFLAGS=-g -Wall

all: snac

snac: snac.o
	$(CC) -L/usr/local/lib *.o -lcurl -lcrypto -o $@

.c.o:
	$(CC) $(CFLAGS) -I/usr/local/include -c $<

snac.o: snac.c snac.h \
    xs.h xs_io.h xs_encdec.h xs_json.h xs_curl.h xs_openssl.h xs_socket.h xs_httpd.h

clean:
	rm -rf *.o *.core snac
