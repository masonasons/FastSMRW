# FastSMRW — Windows User Guide

[← All FastSMRW guides](README.md)

This is the guide for the Windows version of FastSMRW. There are also guides for
[Mac](README-macOS.md), [iPhone](README-iOS.md), [Android](README-Android.md), and [Linux](README-Linux.md).

FastSMRW is a fast, accessible Mastodon and Bluesky client for blind and
low-vision users. On Windows it is keyboard-only, works cleanly with screen
readers, uses gentle earcons (sounds) for feedback, and starts almost instantly
by showing your cached posts first and refreshing in the background.

This guide covers how to get started, how the window is laid out, how to read
and act on posts, how to open (spawn) timelines like Sent, Local, and
Federated, the invisible interface for driving FastSM from other apps, and a
full list of every keystroke.

## Getting started

1. Run FastSMRW.exe. (If you were given a run folder, the program lives inside
   it as FastSMRW.exe.)

2. Add your account. Open the Account menu and choose "Add Account" (or press
   Ctrl+Shift+A). Pick Mastodon or Bluesky and sign in. You can add more than
   one account and switch between them at any time.

3. Wait a moment while your timelines load. Your Home timeline and your
   Notifications open automatically. After the first run, FastSM shows your
   saved posts immediately and quietly refreshes them, so opening the app is
   fast.

4. Start reading. Move between your open timelines with Left and Right arrows,
   and move through the posts in a timeline with Up and Down arrows. Your
   screen reader announces each post as you land on it.

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

- `Ctrl+Left` / `Ctrl+Right` — Choose the movement unit.
- `Ctrl+Up` / `Ctrl+Down` — Jump backward / forward by the chosen unit.

The available movement units are: same user, whole thread, 1 hour, 2 hours,
6 hours, 1 day, 20 posts, 50 posts, and 100 posts. For example, set the unit to
"same user" and Ctrl+Down jumps to the next post by a different person; set it
to "1 day" to leap a day at a time.

## Acting on the current post

With a post focused in the posts list, these single keys act on it:

- `R` — Reply.
- `Q` — Quote.
- `E` — Edit (your own post).
- `B` — Boost or unboost.
- `F` — Like or unlike (favorite).
- `M` — Bookmark or remove the bookmark.
- `C` — Copy the post (or the focused user).
- `U` — Open the author's timeline.
- `D` — Send the author a direct message (Mastodon).
- `P` — Pin or unpin your own post to your profile.
- `H` — Follow a hashtag (the prompt is pre-filled with the post's hashtags).
- `O` — Open the links in the post.
- `A` — Turn auto-reading of new posts on or off for this timeline.
- `N` — Write a new post.
- `Delete` — Delete your own post.
- `Period` — Load older posts into the timeline.

The full Status menu offers more: view the thread, mute the conversation, open
the author's profile, speak the user, speak the post a reply is answering,
follow or unfollow the author, add or edit an alias for the user, and open the
post in your browser. Their shortcuts are in the keystroke list below.

## Spawning timelines

Home and Notifications open by themselves. To open anything else, press Ctrl+T
(Timeline menu, "New Timeline"). A dialog appears with a drop-down of every
timeline you can open. Some choices ask for a value first (a hashtag, a search
term, an instance, or a user); for those a text field appears in the dialog.

What is offered depends on your account:

On Mastodon you can open:

- Local — Public posts from your own instance.
- Federated — Public posts from across the fediverse.
- Mentions — Posts that mention you.
- Bookmarks — Posts you have bookmarked.
- Favourites — Posts you have liked.
- Trends — Trending posts on your instance.
- Conversations — Your direct-message threads.
- Sent — Your own posts.
- Hashtag — Posts for a hashtag you type.
- Search Posts — Posts matching text you type.
- Search People — People matching text you type.
- Remote Instance Timeline — The public timeline of another instance you name.
- Remote User Timeline — Another user's public posts (name@instance).
- Lists — Any of your Mastodon lists.

On Bluesky you can open:

- Mentions, Sent, Hashtag, Search Posts, Search People, your Lists, and your
  custom Feeds.

Once a timeline is open it drops out of the list so you do not open two copies.
You can also open your own followers and following from the Me menu (see below),
and, when you focus a person in a list of users, open that person's timeline,
profile, followers, or following.

Managing your open timelines:

- `Ctrl+1` through `Ctrl+9` — Jump straight to that timeline.
- `Shift+Up` / `Shift+Down` — Reorder the timelines (focus the timelines list first).
- `Ctrl+P` — Pin the timeline so it can't be closed by accident.
- `Ctrl+M` — Mute or unmute this timeline's sounds.
- `Ctrl+W` — Close the timeline.
- `Ctrl+R` — Refresh it now.
- `Ctrl+Shift+R` — Speak when the server last answered, and whether it is unreachable now.
- `Ctrl+Delete` — Clear the posts in this timeline.
- `Ctrl+Shift+Delete` — Clear every timeline.
- `Ctrl+Z` — Undo your last navigation (go back).

## Your account (the Me menu)

The Me menu holds things about you and people:

- Edit Profile — Change your display name, bio, and profile fields.
- View My Followers — Open a timeline of the people who follow you.
- View My Following — Open a timeline of the people you follow.
- Lists — Create and manage your lists.
- View Muted Users — People you have muted.
- View Blocked Users — People you have blocked.
- View Follow Requests — Pending requests to follow you (accept or reject each).
- Followed Hashtags — Manage the hashtags you follow.
- Trending Hashtags — See what is trending.
- User Analysis — A breakdown of the people around a post or timeline.
- User Aliases — Manage the custom names you have given people.

## Accounts

- `Ctrl+Shift+A` — Add an account.
- `Ctrl+[` — Switch to the previous account.
- `Ctrl+]` — Switch to the next account.
- `Ctrl+Shift+Comma` — Account settings for the current account.

## Finding posts

- `Ctrl+F` — Find text in the current timeline.
- `F3` — Find the next match.
- `Shift+F3` — Find the previous match.
- `Ctrl+Shift+F` — Client filters (hide posts that match rules you set).

## Full keystroke list (in the window)

These work while the FastSMRW window is focused.

Application menu:

- `Ctrl+Comma` — Settings.
- `F1` — Open this user guide on the web.
- `Shift+F1` — Check for updates.
- `Ctrl+S` — Stop media playback.
- `Ctrl+H` — Hide the window (to the tray).
- `Ctrl+Q` — Quit FastSMRW.

(Keyboard Manager and Server Filters are on this menu with no shortcut.)

Me menu (no shortcuts; open from the menu):

- Edit Profile, View My Followers, View My Following, Lists, View Muted Users,
  View Blocked Users, View Follow Requests, Followed Hashtags, Trending
  Hashtags, User Analysis, User Aliases.

Status menu (act on the focused post):

- `Ctrl+N` — New post.
- `R` — Reply.
- `B` — Boost.
- `F` — Favorite (like).
- `M` — Bookmark.
- `Ctrl+C` — Copy.
- `Q` — Quote.
- `Enter` — Post info.
- `Ctrl+U` — Open the user's profile.
- `D` — Send a direct message (Mastodon).
- `Ctrl+Semicolon` — Speak the user.
- `Ctrl+Shift+Semicolon` — Speak the post this reply is answering.
- `Ctrl+L` — Follow or unfollow the author.
- `Ctrl+Shift+N` — Add or edit an alias for the user.
- `Ctrl+O` — Open links in the post.

(View Thread, Mute Conversation, Open User Timeline, and Open in Browser are
on this menu with no shortcut.)

Timeline menu:

- `Ctrl+T` — New timeline.
- `Ctrl+R` — Refresh timeline.
- `Ctrl+Shift+R` — Speak refresh status.
- `Ctrl+P` — Pin timeline.
- `Ctrl+M` — Mute timeline sounds.
- `A` — Auto-read new posts.
- `Ctrl+W` — Close timeline.
- `Period` — Load older posts.
- `Ctrl+F` — Find.
- `F3` — Find next.
- `Shift+F3` — Find previous.
- `Ctrl+Shift+F` — Client filters.
- `Ctrl+Delete` — Clear this timeline.
- `Ctrl+Shift+Delete` — Clear all timelines.
- `Ctrl+Z` — Undo navigation.
- `Ctrl+1 to Ctrl+9` — Go to timeline 1 through 9.

Account menu:

- `Ctrl+Shift+A` — Add account.
- `Ctrl+Shift+Comma` — Account settings.
- `Ctrl+[` — Previous account.
- `Ctrl+]` — Next account.

In the posts list (single keys, no menu needed):

- `Up` / `Down` — Previous / next post.
- `Left` / `Right` — Previous / next timeline.
- `Ctrl+Up` / `Ctrl+Down` — Jump by the movement unit.
- `Ctrl+Left` / `Ctrl+Right` — Choose the movement unit.
- `Space` — Open the thread.
- `Enter` — Default action (configurable).
- `Shift+Enter` — Secondary action (configurable).
- `Shift` plus a letter — Jump to the next post starting with that letter.
- `R` — Reply.
- `Q` — Quote.
- `E` — Edit.
- `N` — New post.
- `B` — Boost or unboost.
- `F` — Like or unlike.
- `M` — Bookmark or unbookmark.
- `C` — Copy.
- `U` — Open the author's timeline.
- `D` — Send the author a direct message.
- `P` — Pin or unpin your post to your profile.
- `H` — Follow a hashtag.
- `O` — Open links.
- `A` — Auto-read new posts on or off.
- `Delete` — Delete your own post.
- `Period` — Load older posts.

In the timelines list:

- `Shift+Up` / `Shift+Down` — Move the current timeline up or down in the order.

## The invisible interface

The invisible interface lets you drive FastSM without switching to its window,
so you can read and post while you work in another program. Turn it on in
Settings, on the Invisible Interface page. There are three modes:

- Global hotkeys. FastSM registers system-wide key combinations. Press them
  from anywhere and FastSM responds in the background. This is the simplest
  mode and the default choice.

- Low-level keyhook. Like global hotkeys, but it can capture key combinations
  that Windows normally reserves, giving you more freedom in what you can bind.

- Layer. A single activation key opens a temporary "FastSM layer." While the
  layer is open, plain keys act on FastSM (Up and Down move through posts, R
  replies, and so on), then it closes. Inside the layer, press Slash to hear
  the list of keys and Escape to leave. You choose the activation key on the
  same settings page.

Default global hotkeys (used by Global hotkeys and Low-level keyhook modes).
These are the built-in "default" keymap; you can change any of them in the
Keyboard Manager.

Navigation:

- `Ctrl+Alt+Win+Down` — Next post.
- `Ctrl+Alt+Win+Up` — Previous post.
- `Ctrl+Win+PageDown` — Jump forward by the movement unit.
- `Ctrl+Win+PageUp` — Jump back by the movement unit.
- `Alt+Win+Period` — Next movement unit.
- `Alt+Win+Comma` — Previous movement unit.
- `Alt+Win+Home` — Top of the timeline.
- `Alt+Win+End` — Bottom of the timeline.
- `Ctrl+Alt+Win+Right` — Next timeline.
- `Ctrl+Alt+Win+Left` — Previous timeline.
- `Alt+Shift+Win+Right` — Next account.
- `Alt+Shift+Win+Left` — Previous account.
- `Alt+Win+Space` — Speak the current post.
- `Alt+Win+Z` — Undo navigation (go back).
- `Ctrl+Alt+Win+U` — Refresh the timeline.
- `Ctrl+Alt+Win+R` — Speak when the server last answered, and whether it's unreachable now.

Posts:

- `Ctrl+Win+R` — Reply.
- `Alt+Win+Q` — Quote.
- `Alt+Win+E` — Edit.
- `Alt+Win+N` — New post.
- `Ctrl+Shift+Win+R` — Boost or unboost.
- `Alt+Win+I` — Like or unlike.
- `Alt+Win+V` — Post info.
- `Alt+Win+Enter` — Default action.
- `Alt+Shift+Win+Enter` — Secondary action.
- `Ctrl+Alt+Win+O` — Open the link in the post.
- `Alt+Win+C` — View the thread.

People:

- `Alt+Win+U` — Open the user's timeline.
- `Alt+Win+Shift+U` — Open the user's profile.
- `Alt+Win+Semicolon` — Speak the user.
- `Alt+Win+Shift+Semicolon` — Speak the referenced reply.
- `Alt+Win+L` — Follow or unfollow.
- `Alt+Win+Shift+L` — Mute or unmute the user.
- `Ctrl+Shift+Win+B` — Block or unblock the user.
- `Ctrl+Alt+Win+N` — Add or edit a user alias.

Timeline and app:

- `Ctrl+Alt+Win+T` — New timeline.
- `Ctrl+Alt+Win+P` — Pin or unpin the timeline.
- `Alt+Win+A` — Auto-read new posts.
- `Ctrl+Shift+Win+C` — Copy.
- `Alt+Win+Delete` — Delete your post.
- `Alt+Win+H` — Follow a hashtag.
- `Alt+Win+Apostrophe` — Close the timeline.
- `Ctrl+Win+W` — Show or hide the window.
- `Alt+Win+O` — Settings.
- `Alt+Shift+Win+Q` — Exit FastSMRW.
- `Ctrl+Alt+Win+K` — Keyboard Manager.
- `Ctrl+Alt+Win+S` — Stop media.

Your account:

- `Alt+Win+LeftBracket` — View your followers.
- `Alt+Win+RightBracket` — View your following.

Many more actions ship unbound so you can assign your own keys in the Keyboard
Manager, including: who liked or boosted a post, report a post or user, open a
user's followers or following, Sent, Lists, muted users, blocked users, follow
requests, trending hashtags, user analysis, manage aliases, server and client
filters, add an account, check for updates, About, load older posts, clear the
timeline, and open a post in a browser.

Layer keys (in Layer mode, after the layer opens):

- `Up` / `Down` — Previous / next post.
- `Left` / `Right` — Previous / next timeline.
- `Home` / `End` — Top / bottom of the timeline.
- `Comma` / `Period` — Previous / next movement unit.
- `Page Up` / `Page Down` — Jump back / forward by the movement unit.
- `Space` — Open the thread.
- `Enter` — Default action.
- `Shift+Enter` — Secondary action.
- `R` — Reply.
- `Q` — Quote.
- `E` — Edit.
- `N` — New post.
- `B` — Boost.
- `F` — Like.
- `M` — Bookmark.
- `P` — Pin.
- `A` — Auto-read.
- `C` — Copy.
- `T` — New timeline.
- `U` — User's timeline.
- `D` — Send the user a direct message.
- `W` — Close timeline.
- `L` — Follow or unfollow.
- `H` — Follow a hashtag.
- `O` — Open links.
- `Semicolon` — Speak the user (or open a list of the post's users).
- `Shift+Semicolon` — Speak the post this reply is answering.
- `Shift+H` — Show or hide the window.
- `Slash` — Show this list of keys.
- `Escape` — Leave the layer.

## Customizing the keyboard (Keyboard Manager)

Open the Keyboard Manager from the Application menu (or press Ctrl+Alt+Win+K).
It lets you view and change the keys used by the invisible interface.

- FastSM ships with ready-made keymaps: the built-in "default" set above, a
  "Windows 10" set, and a "Windows 8.1" set. The Windows 8.1 set uses Control
  plus Windows key combinations. Those combos work on Windows 8.1, but Windows
  10 no longer lets apps use Control plus Windows hotkeys, so this set is for
  people on Windows 8.1 who prefer them.

- Duplicate makes an editable copy of any keymap so you can tailor it without
  disturbing the original.

- To rebind or unbind an action, select it and set a new key (or unbind it). If
  you are on a built-in keymap that cannot be edited directly, FastSM
  automatically makes a personal copy called "My Keymap," switches to it, and
  applies your change there, so your edits are always kept.

## Sounds and settings

FastSM plays short earcons for events (a new post arrives, a boost, a like, a
sent post, reaching the top or bottom of a timeline). Moving between posts is
deliberately silent, because your screen reader already announces each one. You
can mute a single timeline's sounds with Ctrl+M, and adjust sound behavior in
Settings.

Settings (Ctrl+Comma) is organized into pages covering general behavior, how
Enter and Shift+Enter behave, the invisible interface, sounds, and more. Each
front end of FastSM keeps the same settings in the same shape, so what you learn
here carries over.
