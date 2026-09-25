# FastSMRW — Mac (macOS) User Guide

[← All FastSMRW guides](README.md)

This is the guide for the Mac version of FastSMRW. There are also guides for
[Windows](README-Windows.md), [iPhone](README-iOS.md), [Android](README-Android.md), and [Linux](README-Linux.md).

FastSMRW is a fast, accessible Mastodon and Bluesky client for blind and
low-vision users. On the Mac it is driven by the menu bar and the keyboard,
works cleanly with VoiceOver, and starts almost instantly by showing your cached
posts first and refreshing in the background.

## Getting started

1. Open FastSMRW. The first time, it opens the Add Account sheet.

2. Choose Mastodon or Bluesky from the Platform menu.

   - Mastodon: enter your instance (for example, mastodon.social) and sign in
     through your browser when it opens.
   - Bluesky: enter the service (usually https://bsky.social), your handle (for
     example, you.bsky.social), and an app password.

3. Your Home timeline and your Notifications open automatically. After the first
   run, FastSM shows your saved posts right away and refreshes them in the
   background.

4. Start reading. In the posts list, Up and Down move between posts; Left and
   Right move between timelines.

## How the window is laid out

- A timelines sidebar on the left lists every open timeline (Home,
  Notifications, and any you add).
- A posts list on the right shows the posts in the selected timeline.
- The menu bar (FastSMRW, File, Edit, Status, Timeline, Account, Window) holds
  every command, each with its shortcut shown next to it.
- A toolbar offers New Post, Refresh, and Add Account buttons.

Press Tab to move from the posts list to the sidebar, and Tab (or Shift+Tab)
to move back.

## Reading and moving through posts

In the posts list:

- `Up` / `Down` — Previous / next post.
- `Left` / `Right` — Previous / next timeline.
- `Space` — Open the thread.
- `Return` — Default action for the post (configurable in Settings, under Behavior; Post Info out of the box).
- `Shift+Return` — Secondary action (also configurable).
- `Option+Up` / `Option+Down` — Jump by the movement unit.
- `Option+Left` / `Option+Right` — Choose the movement unit.

The movement units are same user, whole thread, time gaps, and post counts. Pick
one with Option+Left/Right and jump by it with Option+Up/Down.

In the timelines sidebar:

- `Up` / `Down` — Select a timeline and load its posts.
- `Shift+Up` / `Shift+Down` — Reorder the selected timeline.
- `Delete` — Close the selected timeline (if it can be closed).
- Right-click (or the context-menu key) offers Pin/Unpin, Mute/Unmute sounds,
  Move Up, Move Down, Clear Items, and Close.

## Acting on the current post

With a post focused, single keys act on it (listed just below). Command+Delete
deletes your own post, and Delete on its own closes the current timeline.

The Status menu holds the full set, with shortcuts:

- `R` — Reply.
- `B` — Boost.
- `F` — Favorite.
- `M` — Bookmark.
- `Q` — Quote.
- `E` — Edit.
- `Cmd+Delete` — Delete your post.
- `P` — Pin to your profile.
- `D` — Send a direct message (Mastodon).
- `Cmd+Semicolon` — Speak the user.
- `Cmd+Shift+Semicolon` — Speak the referenced reply.
- `Cmd+I` — Post info.
- `Cmd+O` — Open a link in the post.
- `Space` — View the thread.
- `U` — Open the user's timeline.
- `Cmd+U` — Open the user's profile.
- `Cmd+Shift+N` — Add or edit an alias for the user.
  (Mute Conversation, View Media, Followers, Following, Follow Hashtag, and Open
  in Browser are on this menu with no shortcut.)

Following, muting, blocking, and reporting a person live on the user profile
dialog (open Open User Profile): Follow or unfollow, Mute or unmute, Block or
unblock, Show or hide boosts, Add to a list, Report, Followers, Following, and
Open Timeline. Muting or reporting a whole conversation is on the Post Info
dialog.

## Opening timelines

Home and Notifications open by themselves. To open anything else, choose New
Timeline (Cmd+T) from the Timeline menu. A sheet lists everything you can open;
a choice that needs a value (a hashtag, a search term, an instance, or a user)
shows a field to fill in, and the rest open immediately.

What is offered depends on your account:

On Mastodon: Local, Federated, Mentions, Bookmarks, Favourites, Trends,
Conversations, Sent (your own posts), Hashtag, Search Posts, Search People,
Remote Instance Timeline, Remote User Timeline, and your Lists.

On Bluesky: Mentions, Sent, Hashtag, Search Posts, Search People, your Lists,
and your custom Feeds.

You can also open your own followers and following from the Account menu, and
open a user's timeline, followers, or following from a post. Timelines you
already have open are left out of the sheet.

Managing your open timelines (Timeline menu):

- `Cmd+1` through `Cmd+9` — Jump to that timeline.
- `Cmd+P` — Pin or unpin the timeline.
- `Cmd+M` — Mute or unmute its sounds.
- `Delete` — Close the timeline (press it in the posts list or the sidebar; also on the Timeline menu as Close Timeline).
- `Cmd+R` — Refresh the current timeline.
- `Cmd+Shift+R` — Refresh all timelines.
- `Cmd+Z` — Undo navigation (go back).
- `Cmd+L` — Filter the current timeline.
- `Cmd+Shift+Delete` — Clear the current timeline.
- `Cmd+Shift+Option+Delete` — Clear all timelines.
- Auto-read New Posts and Move Timeline Up/Down are on the menu without a
  shortcut.

## Your account (the Account menu)

The Account menu holds your account tools:

- Add Account (Cmd+Shift+A).
- Account Settings (Cmd+Shift+Comma) — sets that account's soundpack.
- Edit Profile — display name, bio, and Mastodon profile options.
- View My Followers, View My Following.
- Followed Hashtags, Trending Hashtags (Mastodon).
- Manage Lists (Mastodon).
- Server Filters (Mastodon).
- User Aliases — the custom spoken names you give people.
- User Analysis — people who follow you but you don't follow back, people you
  follow who don't follow you back, or mutual follows.
- Remove Current Account.
- Previous Account (Cmd+[), Next Account (Cmd+]).

The Mastodon-only items will tell you so if you use them on a Bluesky account.

## Other shortcuts

Application menu:

- `Cmd+Comma` — Settings.
- `F1` — Open this user guide on the web (Help, User Guide).
- `Cmd+H` — Hide FastSMRW.
- `Cmd+Q` — Quit.
  (About and Check for Updates are on this menu with no shortcut.)

File menu:

- `Cmd+N` — New Post.
- `Cmd+R` — Refresh Timeline.
- `Cmd+W` — Close the window (this quits FastSMRW; it does not close a timeline — use Delete for that).

Edit menu: the standard Cut, Copy, Paste, and Select All.

## Writing a post

The composer opens from New Post (Cmd+N) or from Reply, Quote, and Edit on a
post. Whether Return sends the post or inserts a new line is set in Settings
under General. When "Return sends" is on, Return sends and Shift+Return makes a
new line; when it is off, Command+Return sends and Return makes a new line.
Option+A opens the mention autocomplete. Depending on the account you can also
set a content warning, visibility, attachments with alt text, and more.

## Settings

Open Settings with Cmd+Comma. It is a tabbed window whose pages mirror the other
versions of FastSM:

- General — whether the Return key sends the post.
- Timelines — cache limit, auto-refresh interval, live streaming, showing
  mentions in Notifications, reverse order, auto-loading older posts, syncing
  your Home position (Mastodon), and the movement units.
- Audio — play sounds, the top/bottom boundary sound, soundpack, sound effect
  and media volume, which output device each of those plays through, plus a
  button to open the soundpacks folder.
- Earcons — per-type sounds for images, media, mentions, pinned posts, and
  polls.
- Speech — content-warning handling, emoji in posts and names, how many
  usernames to read, absolute times, and the spoken-field order for posts,
  users, notifications, auto-read, and copy.
- Behavior — the Enter-on-a-post action, the Enter-on-a-user action, the
  secondary action, keeping the media player in the background, and moving extra
  reply mentions to the end.
- Advanced — how many pages of posts to fetch per load.
- Updates — choose Stable version releases or every new build from main, and
  whether to check automatically at startup. Use Application > Check for Updates
  to download the selected Mac disk image.
- Confirmation — ask before boosting, unboosting, liking, unliking, clearing a
  timeline, blocking, unblocking, or deleting a post.
