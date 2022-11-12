# Release Notes

## 2.10

The local timeline purge has been implemented. The maximum days to live can be configured by setting the field `local_purge_days` in the server configuration file. By default it's 0 (no purging).

Some memory usage fixes.

## 2.09

New button `Hide`, to hide a post and its children.

You can boost your own posts indefinitely, because Social Media is about repeating yourself until the others care about you.

Fixed bug in the webfinger query (uid queries should no longer fail).

Updated documentation.

## 2.08

A new page showing the list of people being followed and that follows us has been implemented, with all operations associated (including sending Direct Messages).

Messages marked as sensitive are now hidden behind a dropdown.

More aggressive HTML sanitization.

Fixed possible crash when following somebody by its @user@host.

Some fixes to formatting in post creation.

Fixed a small memory leak.

After a like or boost, the scrolling position is set to the next entry in the timeline and not to the top of the page. This is still somewhat confusing so it may change again in the future.

## 2.07

Fixed some minor memory leaks.

Mail notifications are sent through the queue (and retried if failed).

## 2.06

Fixed a nasty bug in newly created users.

The purge is managed internally, so there is no longer a need to add a cron job for that. If you don't want any timeline data to be purged, set `timeline_purge_days` to 0.

The :shortnames: or Emoji or whatever the fuck these things are called are now shown in actor names and post contents.

Added optional email notifications.

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
