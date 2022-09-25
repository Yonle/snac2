# snac

A simple, minimalistic ActivityPub instance

# Features

- lightweight, minimal dependencies
- large support of ActivityPub operations, e.g. write public notes, follow users, be followed, reply to the notes of others, admire wonderful content (like or boost), write private messages...
- Easily-accessed MUTE button to silence morons
- Tested interoperability with similar software
- No database needed
- Not much bullshit

# About

This program runs as a daemon (proxied by a TLS-enabled real httpd server) and provides the basic services for a Fediverse / ActivityPub instance (sharing messages and stuff from/to other systems like Mastodon, Pleroma, Friendica, etc.).

This is not the manual; man pages `snac(1)` (user manual), `snac(5)` (formats) and `snac(8)` (administrator manual) are what you are looking for.

**This is a work in progress and everything is on fire and you can get hurt.**

`snac` stands for Social Networks Are Crap.

# Installation

This 2.x release is written in C (unlike the 1.x prototype, which was written in Python). The only external dependencies are `openssl` and `curl`.

On Debian/Ubuntu, you can satisfy these requirements by running

```
    apt install libssl-dev libcurl4-openssl-dev
```

On OpenBSD you don't need to install anything because the base system provides all the needed software.

Run `make` and then `make install` as root.

See the administrator manual on how to proceed from here.

# License and author

See the LICENSE file for details.
