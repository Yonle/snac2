CFLAGS=-g -Wall

all: snac

snac: snac.o main.o data.o http.o httpd.o webfinger.o activitypub.o html.o
	$(CC) -L/usr/local/lib *.o -lcurl -lcrypto -pthread -o $@

.c.o:
	$(CC) $(CFLAGS) -I/usr/local/include -c $<

clean:
	rm -rf *.o *.core snac makefile.depend

dep:
	$(CC) -I/usr/local/include -MM *.c > makefile.depend

activitypub.o: activitypub.c xs.h xs_encdec.h xs_json.h xs_curl.h \
 xs_mime.h xs_openssl.h xs_regex.h snac.h
data.o: data.c xs.h xs_io.h xs_json.h xs_openssl.h snac.h
html.o: html.c xs.h xs_io.h xs_encdec.h xs_json.h xs_regex.h xs_set.h \
 snac.h
http.o: http.c xs.h xs_io.h xs_encdec.h xs_openssl.h xs_curl.h snac.h
httpd.o: httpd.c xs.h xs_io.h xs_encdec.h xs_json.h xs_socket.h \
 xs_httpd.h snac.h
main.o: main.c xs.h xs_io.h xs_encdec.h xs_json.h snac.h
snac.o: snac.c xs.h xs_io.h xs_encdec.h xs_json.h xs_curl.h xs_openssl.h \
 xs_socket.h xs_httpd.h xs_mime.h xs_regex.h xs_set.h snac.h
webfinger.o: webfinger.c xs.h xs_encdec.h xs_json.h xs_curl.h snac.h
