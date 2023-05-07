PREFIX=/usr/local
PREFIX_MAN=$(PREFIX)/man
CFLAGS?=-g -Wall -Wextra

all: snac

snac: snac.o main.o data.o http.o httpd.o webfinger.o \
    activitypub.o html.o utils.o format.o upgrade.o mastoapi.o
	$(CC) $(CFLAGS) -L/usr/local/lib *.o -lcurl -lcrypto -pthread $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -I/usr/local/include -c $<

clean:
	rm -rf *.o *.core snac makefile.depend

dep:
	$(CC) -I/usr/local/include -MM *.c > makefile.depend

install:
	mkdir -p -m 755 $(PREFIX)/bin
	install -m 755 snac $(PREFIX)/bin/snac
	mkdir -p -m 755 $(PREFIX_MAN)/man1
	install -m 644 doc/snac.1 $(PREFIX_MAN)/man1/snac.1
	mkdir -p -m 755 $(PREFIX_MAN)/man5
	install -m 644 doc/snac.5 $(PREFIX_MAN)/man5/snac.5
	mkdir -p -m 755 $(PREFIX_MAN)/man8
	install -m 644 doc/snac.8 $(PREFIX_MAN)/man8/snac.8

activitypub.o: activitypub.c xs.h xs_encdec.h xs_json.h xs_curl.h \
 xs_mime.h xs_openssl.h xs_regex.h xs_time.h xs_set.h snac.h
data.o: data.c xs.h xs_io.h xs_json.h xs_openssl.h xs_glob.h xs_set.h \
 xs_time.h snac.h
format.o: format.c xs.h xs_regex.h snac.h
html.o: html.c xs.h xs_io.h xs_encdec.h xs_json.h xs_regex.h xs_set.h \
 xs_openssl.h xs_time.h xs_mime.h snac.h
http.o: http.c xs.h xs_io.h xs_encdec.h xs_openssl.h xs_curl.h xs_time.h \
 xs_json.h snac.h
httpd.o: httpd.c xs.h xs_io.h xs_encdec.h xs_json.h xs_socket.h \
 xs_httpd.h xs_mime.h snac.h
main.o: main.c xs.h xs_io.h xs_encdec.h xs_json.h snac.h
mastoapi.o: mastoapi.c xs.h xs_encdec.h xs_openssl.h xs_json.h xs_io.h \
 xs_time.h xs_glob.h snac.h
snac.o: snac.c xs.h xs_io.h xs_encdec.h xs_json.h xs_curl.h xs_openssl.h \
 xs_socket.h xs_httpd.h xs_mime.h xs_regex.h xs_set.h xs_time.h xs_glob.h \
 snac.h
upgrade.o: upgrade.c xs.h xs_io.h xs_json.h xs_glob.h snac.h
utils.o: utils.c xs.h xs_io.h xs_encdec.h xs_json.h xs_time.h \
 xs_openssl.h snac.h
webfinger.o: webfinger.c xs.h xs_encdec.h xs_json.h xs_curl.h snac.h
