FastSMRW default soundpack
==========================

This is the bundled default soundpack (the original FastSM "default" pack, OGG
Vorbis). A soundpack is a folder of short audio files named by event; replace
these or add your own pack under %APPDATA%\FastSMRW\soundpacks\<PackName>\ (WAV
and MP3 also work; OGG is preferred). Event filenames, without extension:

  boundary      hit the top/bottom of a list
  send_post     a post was sent
  send_reply    a reply was sent
  send_repost   a boost/repost succeeded
  like          favorited a post
  unlike        unfavorited a post
  ready         a timeline finished its first load
  new           new item(s) arrived on refresh
  close         a timeline was closed
  delete        a timeline was cleared / content deleted
  error         an action failed

(There is intentionally NO "navigate" sound — row movement is conveyed by the
screen reader, matching the Mac app.)

Additional event names recognized by the original FastSM (home, mentions,
messages, notification, conversations, list, search, follow, unfollow, mention,
poll, image, media, pinned, max_length, …) will be wired up as those features
land. Extra soundpacks go in %APPDATA%\FastSMRW\soundpacks\<PackName>\ and are
selectable in Settings (coming in a later milestone).
