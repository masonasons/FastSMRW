FastSMRW default soundpack
==========================

A soundpack is a folder of short audio files named by event. Drop OGG Vorbis
(.ogg) files here to enable earcons (WAV and MP3 also work; OGG is preferred and
matches the original FastSM soundpacks). Filenames below, without extension:

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
