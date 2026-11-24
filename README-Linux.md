# FastSMRW — Linux User Guide

[← All FastSMRW guides](README.md)

This is the guide for the Linux version of FastSMRW. There are also guides for
[Windows](README-Windows.md), [Mac](README-macOS.md), [iPhone](README-iOS.md),
and [Android](README-Android.md).

FastSMRW is a fast, accessible Mastodon and Bluesky client for blind and
low-vision users. On Linux it is keyboard-only, works with the Orca screen
reader, uses gentle earcons (sounds) for feedback, and starts almost instantly
by showing your cached posts first and refreshing in the background.

Announcements from the app (like "Boosted" or new-post auto-reading) speak
through Orca itself when Orca is running, so everything comes out in your
usual voice. Without Orca, the app speaks on its own through Speech
Dispatcher.

## Getting started

1. Get the app: download `FastSMRW-Linux.tar.gz` from the
   [releases page](https://github.com/masonasons/FastSMRW/releases), unpack it
   anywhere, and run the `FastSMRW` program inside. Or build it yourself: from
   the FastSMRW folder, run `linux/build.sh`; the finished program is
   `dist/linux/FastSMRW`. You will need these packages
   (Debian/Ubuntu names): g++, pkg-config, libgtk-3-dev, libcurl4-openssl-dev,
   libspeechd-dev, libgstreamer1.0-dev, and the gstreamer1.0-plugins-base and
   gstreamer1.0-plugins-good plugin packages (for the audio player).

2. Add your account. Open the Account menu and choose "Add Account" (or press
   Ctrl+Shift+A). Pick Mastodon or Bluesky and sign in — Mastodon opens your
   browser to authorize, Bluesky asks for your handle and an app password. You
   can add more than one account and switch between them at any time.

3. Wait a moment while your timelines load. Your Home timeline and your
   Notifications open automatically. After the first run, FastSM shows your
   saved posts immediately and quietly refreshes them, so opening the app is
   fast.

4. Start reading. Move between your open timelines with Left and Right arrows,
   and move through the posts in a timeline with Up and Down arrows. Orca
   announces each post as you land on it.

5. Open more timelines whenever you like with Ctrl+T (see "Spawning
   timelines" below).

That is all you need to begin. Everything else in this guide is optional
depth.

## How the window is laid out

The window has two lists and a menu bar.

- The timelines list holds every timeline you currently have open (Home,
  Notifications, and any you add). Left and Right arrows move between them.

- The posts list shows the posts in whichever timeline you are on. Up and Down
  arrows move through the posts.

- The menu bar (Application, Me, Status, Timeline, Account) holds every command,
  and each menu item shows its shortcut. You can always find a command in the
  menus even if you do not remember its key.

## Reading and moving through posts

When you are focused on the posts list:

- `Up` / `Down` — Previous / next post.
- `Left` / `Right` — Previous / next timeline.
- `Home` / `End` — First / last post in the timeline.
- `Shift` plus a letter — Jump to the next post whose spoken text starts with that letter (first-letter navigation).
- `Space` — Open the thread the post belongs to.
- `Enter` — Default action for the post. Out of the box this shows Post Info, but you can change what Enter does in Settings, on the Behavior page.
- `Shift+Enter` — Secondary action (for example, play the post's media). Also configurable on the Behavior page.

Movement units let you skip through a timeline in bigger steps:

- `Ctrl+Left` / `Ctrl+Right` — Choose the movement unit (same poster, same conversation, a time gap, or a fixed number of posts).
- `Ctrl+Up` / `Ctrl+Down` — Jump by the chosen unit.
- Pick which units are offered, and their order, in Settings, Timelines, Movement Units.

## Acting on posts

Single letters work on the focused post:

- `R` — Reply.
- `B` — Boost (or unboost).
- `F` — Favorite (or unfavorite).
- `M` — Bookmark.
- `Q` — Quote.
- `E` — Edit your own post.
- `P` — Pin or unpin your own post to your profile.
- `A` — Auto-read: new posts in this timeline are read aloud as they arrive.
- `U` — Open the poster's posts as a timeline.
- `D` — Send the poster a direct message (Mastodon).
- `H` — Follow a hashtag from the post.
- `Delete` — Delete your own post.
- `Period` — Load older posts.

And with modifiers:

- `Ctrl+C` — Copy the post to the clipboard.
- `Ctrl+O` — Open the post's links (a chooser appears when there are several).
- `Ctrl+U` — Open the poster's profile, with follow, mute, block, lists, and report actions.
- `Ctrl+L` — Follow or unfollow the poster.
- `Ctrl+;` — Speak the post's user(s).
- `Ctrl+Shift+;` — Speak the post this one replies to.
- `Ctrl+Shift+N` — Add or edit your private alias for the poster.

Post Info (Enter by default) shows the full details of a post and offers every
action in one place, including voting in polls, seeing who favorited or
boosted, muting the conversation, and reporting.

## Writing posts

- `Ctrl+N` — New post.
- In the composer, write your text, optionally set a content warning and who
  can see the post, and press `Ctrl+Enter` to send. (You can make plain Enter
  send in Settings, General.)
- When replying, a checklist of the people in the conversation lets you choose
  who is mentioned.

## Spawning timelines

Press `Ctrl+T` (Timeline menu, New Timeline) and choose from the list: Sent,
Favorites, Bookmarks, Local, Federated, a hashtag, a search, a list, and more —
the choices depend on the platform of the current account. Some entries ask
for a value, like the hashtag name or a search query.

Timeline housekeeping:

- `Ctrl+R` — Refresh the timeline.
- `Ctrl+Shift+R` — Speak when the server last answered, and whether it is unreachable now.
- `Ctrl+P` — Pin the timeline so you cannot close it accidentally.
- `Ctrl+M` — Mute the timeline's sounds.
- `Ctrl+W` — Close the timeline.
- `Ctrl+F` — Find in the timeline; `F3` and `Shift+F3` repeat the search.
- `Ctrl+Shift+F` — Client filters: choose which kinds of posts the timeline shows.
- `Ctrl+Delete` — Clear the timeline; `Ctrl+Shift+Delete` clears them all.
- `Ctrl+Z` — Undo navigation (go back to where you were).
- `Ctrl+1` through `Ctrl+9` — Jump straight to a timeline by position.
- `Shift+Up` / `Shift+Down` — In the timelines list, reorder the timelines.

## Accounts

- `Ctrl+Shift+A` — Add an account.
- `Ctrl+[` / `Ctrl+]` — Previous / next account.
- `Ctrl+Shift+Comma` — Account settings (per-account soundpack).

## The invisible interface

The invisible interface lets you control FastSM from anywhere on the system
with global hotkeys, without the window focused. Turn it on in Settings, on
the Invisible interface page.

On Linux the app uses direct keyboard input when it can read the keyboard devices.
In an X11 session without direct keyboard access, it registers the configured
global shortcuts with X11 instead. X11 shortcuts need Control, Alt, or Super,
and a shortcut already in use by another app may need to be reassigned. Keys
use physical QWERTY positions.

For direct keyboard input, add your user to the `input` group and log out and
back in. The keyboard manager lets you edit which key combinations run actions.

## Settings

Open Settings with `Ctrl+Comma`. The pages match the Windows app: General,
Timelines, Audio, Earcons, Speech, Advanced, Confirmations, Behavior,
Invisible interface, and Updates. Highlights:

- Timelines: auto-refresh, streaming, reversed timelines, home-position sync,
  and the Movement Units editor.
- Audio: soundpacks, sound effect and media volume, and which output
  device each of those plays through.
- Earcons: the little sounds that mark images, media, mentions, pins, and polls.
- Speech: what is spoken for posts, users, and notifications — reorder fields,
  turn them off, and add your own text before or after any field. Content
  warnings, emoji stripping, and mention collapsing live here too.
- Confirmations: which actions ask before doing.
- Behavior: what Enter and Shift+Enter do.

## Sounds

Earcons ship in a default soundpack; volume and the pack are in Settings,
Audio. Per-account packs are in Account Settings. If sound ever stops working
under WSL, run `wsl --shutdown` from Windows and start the app again.

## Help

- `F1` — Open this guide.
- `Shift+F1` — Check for updates.
