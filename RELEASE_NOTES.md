# Release Notes

## 2.35

Fixed URL links with the # symbol on them.

When sanitizing HTML input, unsupported tags are deleted instead of escaped.

There is new a user configuration setup checkbox to mark an account as a bot.

Mastodon API: several bug fixes (contributed by Poesty), improved account object (contributed by Haijo7).

## 2.34

Polls can now be created from the web interface.

New Mastodon API features: polls are shown and can be voted on.

The user@host identifier is now shown next to the user avatar (contributed by Haijo7). A small tweak to the default CSS was made to suit this change; please consider updating your style.css.

Clicking on an image attachment opens it in a new tab (contributed by Haijo7).

Bots are marked as such using an emoji (contributed by Haijo7).

## 2.33

Polls (in #ActivityPub lingo, Questions) are shown and can be voted on. Creating a poll is not yet implemented, though.

If an URL written in a post links to an image, it's converted to an attachment.

Fixed a bug in the semaphore code that caused total hangs on OpenBSD.

## 2.32

New user configuration switch to drop direct messages from people you don't follow, in order to mitigate spam from Mastodon open registration instances.

When updating user information (e.g. the display name or the bio), the changes are also sent to the people being followed (so they have an updated description about who the user is).

Mastodon API: account search has been implemented, so mention completion works from apps; new support for blocking users (this operation is called MUTE here).

## 2.31

Fixed webfinger and curl issues, which crashed snac while trying to follow Mitra users (contributed by Poesty).

Use named semaphores instead of anonymous ones, to make it work under MacOS (contributed by Saagar Jha).

Fixed semaphore name conflicts (contributed by Poesty).

Fix warning in ugly pointer substraction (contributed by Saagar Jha).

Added user-agent to webfinger queries (contributed by Poesty).

Mastodon API: implemented notification type filters, fixed (partially) the issue with mentions without server.

## 2.30

Fixed a bug that made some notifications to be missed.

New Mastodon API features: the instance public timeline is now a real one, unfavourite / unreblog is supported (somewhat). Some regression bugs regarding image posting were also fixed.

The non-standard `Ping` and `Pong` #ActivityPub activities have been implemented as proposed by @tedu@honk.tedunangst.com in the https://humungus.tedunangst.com/r/honk/v/tip/f/docs/ping.txt document (with a minor diversion: retries are managed in the same way as the rest of #snac messages).

The build process now includes the `-Wextra` flag.

## 2.29

New Mastodon API features: account search, relationships (so the Follow/Unfollow buttons now appear for each account), follow and unfollow accounts, an instance-level timeline (very kludgy), custom emojis for accounts and statuses, many bug fixes (sadly, the Mastodon official app still does not work).

## 2.28

Added a new notification area to the web interface, accessible from a link at the top, that also shows the number of unseen events. This area lists all notifications in reverse chronological order and provides a button to clear all.

More work in the Mastodon API. The new supported features are: notifications, post of new and reply messages (including attached images). Some API v2 entry points had to be implemented, so you'll need to update your HTTPS proxy configuration again, see `snac(8)`. #Tusky no longer crashes, or so I think. The official app and close relatives like #Megalodon still don't work, though.

If you are not interested in this Mastodon API crap, you can compile it out of your #snac by defining the `NO_MASTODON_API` preprocessor directive and forget about it.

Fixed an HTML cache bug (it was not refreshed after editing a post).

## 2.27

Started Mastodon API support, so you can use Mastodon-compatible apps to access #snac accounts. What works so far: login, private and public timelines, full post information (replies and ancestors), liking and boosting posts. Things that don't work yet but eventually will: following accounts, posting and replying to messages (I still have to figure out how some things work, like posting images), notifications (needs some internal support), the instance timeline (snac does not have one, but it can be simulated with not much effort) and probably many other things. Things that will never work: bookmarks, pinning, a federated timeline, many other things that I don't remember right now. Please note that if you want to use this API in your instance, you must add some lines to your HTTP proxy configuration, see the `snac(8)` (administrator documentation) manual page. I'm doing my tests using the #Tusky (which sometimes crashes, surely my fault), #AndStatus, #Fedilab and #Husky Android apps. Success or failure reports will be appreciated.

Fixed some buffer overflows (contributed by Saagar Jha).

Fixed overzealous rejection of some local boosts.

## 2.26

The OpenSSL code has been refactored to avoid using deprecated functions.

Added more aggressive filtering on unwanted `Announce` (boost) messages.

## 2.25

Federation with other instances have been improved by collecting shared inbox information from input messages more thoroughly.

Fixed an obscure bug that caused connection rejections from some instances.

Some rules regarding incoming messages have been tightened; messages that are not related to the user are not added to the timeline. This has to be implemented because some ill-behaving ActivityPub implementations were found injecting unwanted messages to user inboxes.

Messages from MUTEd users are rejected as soon as possible with a 403 Forbidden HTTP status.

Fixed a minor bug regarding the scope of the 'Update' activity (edited posts were sent to more recipients that it should).

More aggressive input sanitization (some posts were found that included strange ASCII control codes).

Added "Open Graph" HTML meta tags for better previsualization of `snac` links from other social media.

## 2.24

Sending non-public messages is now much easier: a checkbox to post a message to only those people mentioned in the message body has been added.

Fixed an over-optimization bug that caused some mentioned recipients to be skipped.

Added some new administrator tweaks: email notifications can be globally disabled.

## 2.23

The user avatar can now be uploaded (instead of given as an URL).

Minor speed improvements int output messages.

Minor improvements in error logging.

## 2.22

Fixed a bug with the Delete button in the web interface: sometimes, instead of the post (as the user intended), the follower was deleted instead :facepalm:

Fixed a bug in the command-line option `follow` (and probably others), that made it fail silently if there was no running server.

Fixed a crash under OpenBSD (a recent change needed a new permission to the `pledge()` call that was forgotten).

## 2.21

Users can now specify an expire time for the entries in their timelines (both their own and others').

Added support for sending notifications (replies, follows, likes, etc.) via Telegram.

Followers can now be deleted (from the *people* page in the web interface). Yes, to stop sending in vain your valuable and acute posts to those accounts that disappeared long ago and flood the log with connection errors.

The internal way of processing connections have been rewritten to be more efficient or, as technical people say, "scalable". This way, `snac` is much faster in processing outgoing connections and less prone to choke on an avalanche of incoming messages. This is a big step towards the secret and real purpose of the creation of this software: being able to host the account of #StephenKing when he finally leaves that other site.

The `note` action from the command-line tool can also accept the post content from the standard input.

## 2.20

Image attachments in posts can now have a description (a.k.a. "alt text").

## 2.19

You can edit your own posts from now on.

Fixed the breakage of Emojis I introduced when implementing HashTags because I am a moron.

Added adaptative timeouts when sending messages to other instances.

## 2.18

Added support for #HashTags (they are not internally indexed yet, only propagated to other instances).

Added support for OpenBSD enhanced security functions `unveil()` and `pledge()` (contributed by alderwick).

The purge ttl for stray global objects has been shortened.

In the HTML interface, don't show the collapse widget for non-existent children.

Added support for HTTP signature pseudo-headers `(created)` and `(expires)`, that are used by some ActivityPub implementations (e.g. Lemmy).

When replying, the mentioned people inherited from the original post will be clearly labelled with a CC: prefix string instead of just being dropped out there like noise like Mastodon and others do. (I hope) this will help you realise that you are involving other people in the conversation.

## 2.17

Fixed a header bug (contributed by alderwick).

Fixed a bug in the Boost by URL option when the URL of the boosted message is not the canonical id for the message (e.g. Mastodon's host/@user/NNN vs. host/users/user/statuses/NNN).

Fixed crash on a corner case regarding failed webfinger requests.

## 2.16

Some outgoing connection tweaks: the protocol is no longer forced to be HTTP/1.1 and timeouts are less restrictive. This has proven useful for some unreachable instances.

Conversations can be collapsed.

Added support for edited posts on input. These updated messages will be marked by two dates (creation and last update).

Some tweaks to the docker configuration to generate a smaller image (contributed by ogarcia).

## 2.15

Fixed bug in message posting that may result in 400 Bad Request errors (contributed by tobyjaffey).

Fixed crash and a deletion error in the unfollow code.

Added configuration files and examples for running snac with docker (contributed by tobyjaffey).

Serve /robots.txt (contributed by kensanata).

## 2.14

Previous posts in the public and private timelines can be reached by a "More..." post at the end (contributed by kensanata).

Clicking the 'Like' and 'Boost' buttons don't move the full conversation up; after that, the page is reloaded to a more precise position. Still not perfect, but on the way.

New command-line operation, `resetpwd`, to reset a user's password to a new, random one.

Added a user setup option to toggle if sensitive content is shown or not by default (contributed by kensanata).

All images are loaded in lazy mode for a snappier feel (contributed by kensanata).

Fixed crash in the data storage upgrade process when debug level >= 2 (contributed by kensanata).

Log message improvements for excelence (contributed by kensanata).

The logging of "new 'Delete'..." messages has been moved to debug level 1, because I'm fed up of seeing my logs swamped with needless cruft.

Don't show the 'Boost' button for private messages.

Added (partial) support for /.well-known/nodeinfo site information. This is not mandatory at all, but if you want to serve it, remember that you need to proxy this address from your web server to the `snac` server.

Some internal structure improvements.

## 2.13

A big disk layout rework, to make it more efficient when timelines get very big. Please take note that you must run `snac upgrade` when you install this version over an already existing one.

Fixed HTML loose close tag (contributed by kensanata).

Fixed bug when closing sendmail pipe (contributed by jpgarcia and themusicgod1).

## 2.12

Fixed some bugs triggered when a GET query does not have an `Accept:` header.

RSS feeds are a bit less ugly.

## 2.11

Marking posts and replies as sensitive content is now possible.

On output, shared inboxes are used on instances that support it. This reduces bandwidth usage greatly, specially for very popular users with thousands of followers.

Added RSS output support for the user posts (by adding .rss to the actor URL or by requesting the actor with an Accept HTTP header of `text/xml` or `application/rss+xml`).

Much more aggresive HTML sanitization.

## 2.10

The local timeline purge has been implemented. The maximum days to live can be configured by setting the field `local_purge_days` in the server configuration file. By default it's 0 (no purging).

Some memory usage fixes.

More formatting fixes.

The only JavaScript snippet has been replaced by pure HTML, so `snac` is now purely JavaScript-free.

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
