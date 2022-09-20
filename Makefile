CFLAGS=-g -Wall

all: snac

snac: snac.o main.o data.o http.o
	$(CC) -L/usr/local/lib *.o -lcurl -lcrypto -o $@

.c.o:
	$(CC) $(CFLAGS) -I/usr/local/include -c $<

clean:
	rm -rf *.o *.core snac

snac.o: snac.c snac.h \
    xs.h xs_io.h xs_encdec.h xs_json.h xs_curl.h xs_openssl.h xs_socket.h xs_httpd.h

main.o: main.c snac.h xs.h

data.o: data.c snac.h xs.h xs_json.h

http.o: http.c snac.h xs.h xs_io.h xs_encdec.h xs_openssl.h xs_curl.h
