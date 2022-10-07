# Release Notes

## 2.02

Fixed a bug that caused empty actor names look like crap.

Fixed a severe bug in number parsing and storage.

Children entries are not indented further than 4 levels, so that fucking Twitter-like threads are readable.

Added some asserts for out-of-memory situations.

The publication timestamp only shows the date, so timezone configuration and conversion is no longer an issue.

The reply textareas are pre-filled with the user ids mentioned in the message.

Fixed bug in the web command `Boost (by URL)`.

New command-line option `purge`.

Fixed a null-termination bug in Basic Authentication parsing.

Updated documentation.

New systemd and OpenBSD startup script examples in the examples/ directory.

## 2.01

This is the first public release of the 2.x branch.
