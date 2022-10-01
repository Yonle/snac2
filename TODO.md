# TODO

## Open

Show dates in local time and not UTC.

Add web interface for private messages.

Add web interface for the list of people being followed.

Add an RSS to the local timeline.

After 'Unfollow' or 'MUTE', the timeline should be rebuilt (regardless of the cached version).

Implement image attachments.

## Wishlist and batshit crazy ideas

Should this user's notes with in_reply_to be resolved inside the object?

Should admirations download the admired object into the timeline instead of resolving?

Add a user configuration flag to hide likes from the timeline.

The 'history' pages are now just monthly snapshots of the local timeline. This is ok and cheap and easy, but is problematic if you i.e. delete a post because it will be there in the history forever.

Implement an input queue.

Implement a way to save interesting posts.

Create the `mastodon2snac` helper program (reading directly from the boxes, not using any exported data).

Refactor HTML rendering because it's a mess and write build_timeline(), that generates a big structure with everything to show in a timeline, to be passed to the HTML renderer.

Disk layout improve, related to build_timeline(): the tid in the timeline filenames is the published time of the message. When a timeline is processed, its in_reply_to is requested (recursively) and the entry moved down the tree while all ids are stored in the 'already seen' set. This allows: 1) to avoid rewriting the parents in timeline storage 2) download the parents asynchronously (by requesting them from a queue) instead of at message arrival.

## Closed

Start a TODO file (2022-08-25T10:07:44+0200).

Change the layout to be multi-user (2022-08-25T16:23:17+0200).

Read address:port from server.json (2022-08-26T09:14:08+0200).

Added an installation setup (2022-08-26T09:39:46+0200).

Purge timeline entries older than a configurable value (2022-08-26T13:15:21+0200).

Move all the interactive code (like initdb()) to a special source file that is only imported on demand (2022-08-26T18:08:05+0200).

Add Basic Authentication for /admin* (2022-08-28T18:55:05+0200).

Add unfollow (2022-08-28T19:59:29+0200).

Generate the /outbox, unpaged, of the 20 latest entries, like what honk does (2022-08-29T14:29:48+0200).

If a user serves a static style.css file, it's embedded in the HTML (2022-08-29T14:52:57+0200).

Filter note texts through a Markdown-like filter (2022-08-29T21:06:47+0200).

If a `Like` or `Announce` is received from the exterior but the object is of a different user in the same instance, when the object is resolved a loop happens SNAC/activitypub.py:506: (2022-08-30T10:03:03+0200).

Ensure that likes and boosts are applied to the object instead of the container. More on this: for Mastodon, we're showing the '.../activity' link instead of the proper Note (2022-08-30T11:57:31+0200).

Fix signature checks (2022-08-30T18:32:22+0200).

Add @user@host citation in notes (2022-08-31T10:42:46+0200).

Add a snac.L() localisation function.

Add an `adduser` command-line option (2022-08-31T19:02:22+0200).

`Accept` + `Follow` types should not be trusted (anyone can be followed by sending them) (2022-09-01T08:49:57+0200).

It happened twice that a reply-to Note is lost (from the web) (2022-09-02T12:31:49+0200).

Implement the like button (2022-09-02T19:28:04+0200).

Implement the boost button (2022-09-02T19:28:04+0200).

Implement the follow button (2022-09-02T19:28:04+0200).

Implement the unfollow button (2022-09-02T19:28:04+0200).

Implement the mute button (2022-09-02T19:28:04+0200).

Don't write messages in the timeline if they are already there (2022-09-03T19:14:58+0200).

Implement the Emoji tags in Notes (2022-09-03T22:14:56+0200).

Implement the 'one page' timeline (2022-09-04T05:41:07+0200).

Re-implement the renaming of updated messages in the local time (2022-09-04T05:58:51+0200).

Add support for a server `greeting.html` that will be returned when the server URL is queried, with a special mark to insert the user list (2022-09-05T10:05:21+0200).

Implement HTML caches for both timelines (2022-09-05T13:45:27+0200).

Implement a history for the local timeline (2022-09-05T14:20:15+0200).

Disable the Boost button for private messages (2022-09-05T19:32:15+0200).

Implement a user config page, where they can change their name, avatar, bio and password (2022-09-05T22:29:26+0200).

Also replace Emoji tags in people's names (2022-09-05T23:00:29+0200).

Implement `Delete` + `Tombstone` on input (2022-09-07T09:20:20+0200).

Implement `Delete` + `Tombstone` on output (2022-09-07T09:42:09+0200).

Entries in the local timeline appear again after being shown in a thread. Try implementing an 'already shown entries' set (2022-09-07T11:21:52+0200).

The Delete button doesn't work for Likes and Announces (it points to the wrong message id) (2022-09-07T15:46:29+0200).

Document `server.json` in the admin manual (2022-09-08T11:01:43+0200).

Document the command-line interface in the user manual (2022-09-08T11:26:11+0200).

Document the web interface in the user manual (2022-09-08T14:00:11+0200).

Enable back the caches (2022-09-08T19:12:51+0200).

Do not show `Like` or `Boost` buttons if that was already done (2022-09-12T19:29:04+0200).

Parents of a parent should also move up the timeline (2022-09-13T22:41:23+0200).

When a new note has an in-reply-to, also download it (2022-09-24T07:20:16+0200).
