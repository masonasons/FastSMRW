# FastSMRW — iPhone (iOS) User Guide

[← All FastSMRW guides](README.md)

This is the guide for the iPhone version of FastSMRW. There are also guides for
[Windows](README-Windows.md), [Mac](README-macOS.md), [Android](README-Android.md), and [Linux](README-Linux.md).

FastSMRW is a fast, accessible Mastodon and Bluesky client for blind and
low-vision users. On iPhone you drive it with touch and VoiceOver: flicking
through posts, VoiceOver custom actions and rotors, the escape and magic-tap
gestures, and, if you attach a keyboard, hardware shortcuts. Everything you hear
is composed by the shared FastSM core, so it stays consistent with the other
versions.

## Getting started

1. Open FastSM. The first time, it takes you to the add-account screen.

2. Choose Mastodon or Bluesky with the selector at the top.

   - Mastodon: enter your instance (for example, mastodon.social) and tap Log
     In. A secure browser opens for you to sign in, then returns to the app.
   - Bluesky: enter the service (usually https://bsky.social), your handle (for
     example, you.bsky.social), and an app password, then tap Add Account.

3. Your Home timeline and your Notifications open as the first two tabs. After
   the first run, FastSM shows your saved posts right away and refreshes them in
   the background.

4. Start reading. Flick left and right through the posts, the way VoiceOver
   moves through any list.
   Switch timelines by activating a tab in the tab strip.

## How the screen is laid out

- A strip of timeline tabs, which you can place at the top or the bottom of the
  screen in Settings. Each tab is announced with its name and state, such as
  "Home tab, pinned, muted."
- A full-screen table of posts for the current timeline; each post is one
  VoiceOver item that reads as the whole composed post.
- A compose button to write a new post, and a More button that holds the menus.

## Moving around

- Flick left and right to move through posts. FastSM updates the list without
  yanking your VoiceOver cursor around.
- Switch timelines by activating a tab in the tab strip.
- Movement rotors: each movement unit (same user, whole thread, time gaps, post
  counts) is a VoiceOver rotor. Twist the rotor to a unit, then flick up or down
  to jump by it.
- Two-finger scrub (the escape gesture) closes the current timeline when it can
  be closed, or plays a boundary sound when it cannot. Home and Notifications
  cannot be closed.
- Two-finger double-tap (the magic tap) normally runs the secondary action on
  the focused post, or opens the composer if no post is focused. When the
  secondary action is View media and the post has no usable media, it opens the
  composer instead. In Settings under Behavior, choose **VoiceOver magic tap**
  to always open compose or let iOS handle the gesture.
- Your reading position is remembered for each timeline and each account.

## Acting on a post

VoiceOver double-tap on a post runs its main "interact" action, which you set in
Settings under Behavior.

For everything else, use the post's VoiceOver custom actions (swipe up or down
on the post, then double-tap). Sighted users can long-press the post for the
same menu. You choose which actions appear and in what order in Settings, under
Post Actions. Available actions include:

- Reply, Quote.
- Boost or remove boost, Favorite or remove favorite, Bookmark.
- View Thread, Post Info.
- View Media (when the post has media), Open Links, Open in Browser.
- Copy.
- User Profile, User Timeline, Followers, Following.
- Mute Conversation.
- See who favorited it, See who boosted it (when there are any).
- Add or Edit Alias (a custom spoken name for the author).
- Follow Hashtag.
- Speak user, Speak the referenced reply, or Jump to the referenced reply (when
  the post is a reply).
- Edit, Pin to Profile, Delete (your own posts only).
- Report.

Muting, blocking, and following a person are on their User Profile screen (open
it from a post's User Profile action): Follow or unfollow, Mute or unmute, Block
or unblock, Hide or show boosts, Open their timeline, Followers, Following, Add
to a list, Open in browser, and Report.

## Opening timelines

Home and Notifications are always open. To open anything else, use the More menu
and choose "New Timeline." You get a picker of everything you can open; a choice
that needs a value (a hashtag, a search term, a remote instance) prompts you
first, and the rest open immediately as a new tab.

What is offered depends on your account:

On Mastodon: Local, Federated, Mentions, Bookmarks, Favourites, Trends,
Conversations, Sent (your own posts), Hashtag, Search Posts, Search People,
Remote Instance Timeline, Remote User Timeline, and your Lists.

On Bluesky: Mentions, Sent, Hashtag, Search Posts, Search People, your Lists,
and your custom Feeds.

You can also open your own followers and following, and your Sent posts, from
the Manage submenu, open a hashtag from the Followed or Trending Hashtags
managers, and open a list from the Lists manager. Timelines you already have
open are left out of the picker.

## Your account (the Manage menu)

Open the More menu and its Manage submenu for your account tools:

- Edit Profile — display name, bio, and on Mastodon your default post privacy,
  the require-follow-requests, bot, discoverable, and sensitive-media toggles,
  and your profile fields.
- View My Followers, View My Following.
- Manage Lists — create, rename, delete, or open a list.
- Followed Hashtags, Trending Hashtags — follow, unfollow, or open (Mastodon).
- Server Filters — create and edit server-side filters (Mastodon).
- User Aliases — edit or remove the custom spoken names you have given people.
- User Analysis — people who follow you but you don't follow back, people you
  follow who don't follow you back, or mutual follows; opens as a timeline.

Some of these (Lists, Followed and Trending Hashtags, Server Filters) are
Mastodon features and will tell you so on a Bluesky account.

Switching accounts: in the More menu's account section (titled with your current
handle) choose Next Account or Previous Account. That section also has Account
Settings and a Remove Account option.

The More menu also has Help (User Guide), which opens this guide on the web.

## Hardware keyboard shortcuts

If you attach a keyboard, these shortcuts work (they are meant for iPad and
Bluetooth keyboards but work on iPhone too):

- `Up` / `Down` — Previous / next post.
- `Left` / `Right` — Previous / next timeline.
- `Option+Up` / `Option+Down` — Jump by the movement unit.
- `Option+Left` / `Option+Right` — Choose the movement unit.
- `Shift+Up` / `Shift+Down` — Move the current timeline up or down.
- `Return` — Interact (your configured main action).
- `Shift+Return` — Secondary action.
- `Space` — View the thread.
- `R` — Reply.
- `B` — Boost.
- `F` — Favorite.
- `M` — Bookmark.
- `Q` — Quote.
- `U` — User timeline.
- `E` — Edit.
- `P` — Pin to profile.
- `D` — Send a direct message (Mastodon).
- `Cmd+U` — User profile.
- `Cmd+I` — Post info.
- `Cmd+O` — Open link.
- `Cmd+Shift+C` — Copy.
- `Cmd+Delete` — Delete.
- `Cmd+N` — New post.
- `Cmd+R` — Refresh.
- `Cmd+Shift+R` — Refresh all.
- `Cmd+Option+R` — Speak refresh status.
- `Cmd+Z` — Undo navigation.
- `Cmd+T` — New timeline.
- `Cmd+W` — Close timeline.
- `Cmd+L` — Filter timeline.
- `Cmd+[` / `Cmd+]` — Previous / next account.
- `Cmd+1` through `Cmd+9` — Go to that timeline.

In the composer, a hardware Return sends the post while the on-screen keyboard's
Return still inserts a new line.

## Writing a post

The composer opens from the compose button, the magic tap, Cmd+N, or Reply,
Quote, and Edit on a post. Its left button is Cancel and its right button is
Post (or Save when editing). Depending on the account and context it can show: a
recipients checklist for replies, a content-warning field, the main text box
with a live "characters remaining" count, a "Mention Someone" search, a
visibility choice (Public, Quiet public, Followers, or Specific people), a
language choice, a poll (up to four choices, allow-multiple, and a duration), a
"schedule for later" date, and attachments (photos or videos, each with an
alt-text description).

## Settings

Open Settings from the More menu. It is organized into sections that mirror the
other versions of FastSM:

- General — whether the Return key sends the post.
- Timelines — cache limit, auto-refresh interval, live streaming, showing
  mentions in Notifications, reverse order, auto-loading older posts, syncing
  your Home position (Mastodon), tab-bar position, and the movement rotors.
- Audio — play sounds, boundary sound, soundpack, and volume.
- Earcons — per-type sounds for images, media, mentions, pinned posts, and
  polls.
- Speech — content-warning handling, emoji handling, how many usernames to read,
  absolute or relative times, and the spoken-field order for posts, users,
  notifications, auto-read, and copy.
- Behavior — the tap-on-post action, the tap-on-user action, the secondary
  action, the post-actions list, and media-background and reply-mention options.
- Advanced — how many pages of posts to fetch per load.
- Confirmation — ask before boosting, unboosting, liking, unliking, clearing a
  timeline, blocking, unblocking, or deleting a post.

Account Settings (in the More menu's account section) sets that account's
soundpack.
