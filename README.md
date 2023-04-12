# snac

A simple, minimalistic ActivityPub instance

## Features

- Lightweight, minimal dependencies
- Extensive support of ActivityPub operations, e.g. write public notes, follow users, be followed, reply to the notes of others, admire wonderful content (like or boost), write private messages...
- Multiuser
- Mastodon API support, so Mastodon-compatible apps can be used (work in progress)
- Simple but effective web interface
- Easily-accessed MUTE button to silence morons
- Tested interoperability with related software
- No database needed
- Totally JavaScript-free
- No cookies either
- Not much bullshit

## About

This program runs as a daemon (proxied by a TLS-enabled real httpd server) and provides the basic services for a Fediverse / ActivityPub instance (sharing messages and stuff from/to other systems like Mastodon, Pleroma, Friendica, etc.).

This is not the manual; man pages `snac(1)` (user manual), `snac(5)` (formats) and `snac(8)` (administrator manual) are what you are looking for.

`snac` stands for Social Networks Are Crap.

## Installation

This 2.x release is written in C (unlike the 1.x prototype, which was written in Python). The only external dependencies are `openssl` and `curl`.

On Debian/Ubuntu, you can satisfy these requirements by running

```
    apt install libssl-dev libcurl4-openssl-dev
```

On OpenBSD you also need to install `curl`:

```
    pkg_add curl
```

Run `make` and then `make install` as root.

See the administrator manual on how to proceed from here.

## Testing via Docker

A `docker-compose` file is provided for development and testing. To start snac with an nginx HTTPS frontend, run:

    docker-compose build && docker-compose up

This will:

- Start snac, storing data in `data/`
- Configure snac to listen on port 8001 with a server name of `localhost` (see `examples/docker-entrypoint.sh`)
- Create a new user `testuser` and print the user's generated password on the console (see `examples/docker-entrypoint.sh`)
- Start nginx to handle HTTPS, using the certificate pair from `nginx-alpine-ssl/nginx-selfsigned.*` (see `examples/nginx-alpine-ssl/entrypoint.sh`)

## License

See the LICENSE file for details.

## Author

grunfink @grunfink@comam.es
