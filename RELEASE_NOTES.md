# Release Notes

## 2.06

The purge is managed internally, so there is no longer a need to add a cron job for that. If you don't want any timeline data to be purged, set `timeline_purge_days` to 0.

The :shortnames: or Emoji or whatever the fuck these things are called are now shown in actor names and post contents.

## 2.05

Image upload is now supported (one image per post).

Added support for HEAD methods (who knew it was necessary?).

Fixed bug in greeting.html completion (%host% was not being replaced).

Referrers (actors that like or announce posts) are not overwritten.

## 2.04

More multithreading.

Image URLs can also be attached to reply messages.

Improved default mentions in reply text fields.

The static directory now works.

New command line `unfollow`.

## 2.03

Notes can now attach images and other media. The web interface still limits this feature to only one attachment (given by URL, so the file must have been uploaded elsewhere).

Videos attached to notes are now shown.

A small set of ASCII emoticons are translated to emojis.

The new (optional) server configuration directive, `disable_cache`, disables the caching of timeline HTML output if set to `true` (useful only for debugging, don't use it otherwise).

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
