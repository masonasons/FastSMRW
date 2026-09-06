# FastSMRW — Android User Guide

[← All FastSMRW guides](README.md)

This is the guide for the Android version of FastSMRW. There are also guides for
[Windows](README-Windows.md), [Mac](README-macOS.md), [iPhone](README-iOS.md), and [Linux](README-Linux.md).

FastSMRW is a fast, accessible Mastodon and Bluesky client for blind and
low-vision users. On Android you drive it entirely by touch and TalkBack:
tapping, long-pressing, swiping, and the back gesture. Everything you hear —
post text, profiles, notifications — is composed by the shared FastSM core and
read out by TalkBack, so it stays consistent with the other versions.

## Getting started

1. Open FastSM. The first time, it takes you straight to the add-account screen.

2. Choose Mastodon or Bluesky with the toggle at the top.

   - Bluesky: enter your handle and an app password. You create an app password
     at bsky.app under Settings, App Passwords.
   - Mastodon: enter your instance (for example, mastodon.social). FastSM opens
     your browser to sign in, then returns to the app automatically.

3. Your Home timeline and your Notifications open as the first two tabs. After
   the first run, FastSM shows your saved posts right away and refreshes them in
   the background.

4. Start reading. Flick left and right through the posts with TalkBack, the way
   you move through any list. Switch timelines by choosing a tab in the tab
   strip.

## How the screen is laid out

- A top bar labeled "FastSM" holds the account picker, a Refresh button, a
  Close button (only when the current timeline can be closed), and a "More"
  overflow menu.
- A tab strip lists your open timelines. You can put it at the top or the bottom
  of the screen in Settings.
- The main area is the current timeline: a scrolling list of posts. Each post is
  one item that TalkBack reads as a single spoken description.
- A floating "New post" button sits at the bottom corner.

## Moving around

- Flick left and right through posts with TalkBack, the way you move through any
  list.
- Switch between your open timelines by choosing a tab in the tab strip, or, with
  TalkBack on, with its page-scrolling gestures (swipe left or right with two
  fingers) over the timeline. With TalkBack off, a normal left or right swipe on
  the timeline area also works.
- Older posts load automatically as you near the end of a timeline. Your reading
  position in each timeline is remembered.
- The back gesture closes the current timeline if it is a closable one (a
  thread, search, user, or list tab). Home and Notifications cannot be closed.

## Acting on a post

Tap a post once to open its thread (for a grouped like or boost notification, a
single tap opens the list of everyone involved).

For everything else, long-press the post to open its menu. The same actions are
offered to TalkBack as custom actions, so you can also reach them by swiping up
or down on the post and double-tapping. You can choose which actions appear and
in what order in Settings, under Post actions. Available actions include:

- Reply, Quote.
- Boost or unboost, Favourite or unfavourite, Bookmark or remove bookmark.
- View media (when the post has any; if there is more than one, you pick which).
- View conversation (open the thread).
- Mute or unmute the conversation.
- See who favourited it, See who boosted it (when there are any).
- Copy.
- Report the post.
- View the author's posts, View the author's profile, Speak user info.
- Send the author a direct message (Mastodon).
- Add or edit an alias (a custom name) for the author.
- Speak the referenced reply, or jump to it (when the post is a reply).
- Edit or Delete (your own posts only; Delete asks you to confirm).
- Expand links: when turned on, each link in the post becomes its own action,
  labeled by the link's title.

To create a brand-new post, use the floating "New post" button.

## Opening timelines

Home and Notifications are always open. To open anything else, use the top-bar
overflow menu ("More") and choose "Add timeline or search." You get a list of
everything you can open; a choice that needs a value (a search term, a hashtag,
an instance) prompts you first, and the rest open immediately as a new tab.

What is offered depends on your account:

On Mastodon: Local, Federated, Mentions, Bookmarks, Favourites, Trends,
Conversations, Sent (your own posts), Hashtag, Search Posts, Search People,
Remote Instance Timeline, Remote User Timeline, your Lists, and also Muted
Users, Blocked Users, and Follow Requests.

On Bluesky: Mentions, Sent, Hashtag, Search Posts, Search People, your Lists,
and your custom Feeds.

You can also open your own followers and following straight from the overflow
menu (see "Your account" below).

Managing an open tab: long-press a tab (or use its TalkBack custom actions) to
Pin or unpin it, Mute or unmute its sounds, Move it left or right, or Close it.
The top-bar Refresh button reloads the current timeline.

## Your account

The top-bar overflow menu ("More") holds your account tools:

- Add timeline or search.
- User aliases — list and edit the custom names you have given people.
- User analysis — pick people who follow you but you don't follow back, people
  you follow who don't follow you back, or mutual follows; the result opens as a
  timeline.
- Trending hashtags — open or follow each (Mastodon).
- Auto-read new posts — turn automatic reading of new posts on or off for the
  current timeline.
- Edit profile — your display name and bio, and on Mastodon also your default
  post privacy, follow-request requirement, bot and discoverable flags,
  sensitive-media default, and profile fields.
- View my followers, View my following.
- Settings.
- Help (user guide) — opens this guide on the web in your browser.

Switching accounts: tap the account picker under the top bar. Its menu lists
your other accounts to switch to, plus Account settings, Add account, and Log
out.

Following, muting, blocking, or reporting a person is done from their Profile
screen (open a user's profile from a post's menu): Follow or unfollow, View
posts, Report, and Open in browser. Muted and blocked users are reviewed by
opening the Muted Users and Blocked Users timelines.

## Writing a post

The composer opens from the "New post" button, or from Reply, Quote, or Edit on
a post. It has a Cancel and a Send button, an optional content-warning field, a
main text box, and a live count of characters remaining (based on your server's
limit). You can:

- Mention someone with the mention button, which searches as you type.
- Add media (where supported), giving each item alt text.
- Set the visibility (Public, Quiet public, Followers, or Specific people).
- Choose which reply participants to include (Mastodon).

## Settings

Open Settings from the overflow menu. It is organized into sections that mirror
the other versions of FastSM:

- General — whether the Return key sends the post.
- Notifications — turn push notifications on or off (Mastodon).
- Timelines — cache size, auto-refresh interval, real-time streaming (Mastodon),
  tab-bar position, showing mentions in Notifications, reversing timelines,
  auto-loading older posts, and syncing your Home reading position (Mastodon).
- Audio — play sounds, choose a soundpack, volume, and the boundary sound.
- Earcons — per-type sounds for images, media, mentions, pinned posts, and
  polls.
- Speech — what is spoken, and in what order, for posts, users, notifications,
  and auto-read; the copy templates; content-warning handling; emoji removal;
  how many usernames to read in a post; absolute or relative times; and the
  separator text.
- Advanced — how many pages of posts to fetch per load.
- Confirmation — ask before boosting, unboosting, liking, unliking, clearing a
  timeline, blocking, unblocking, or deleting a post.
- Behavior — put extra reply mentions at the end.
- Post actions — show, hide, and reorder the per-post actions (the TalkBack
  swipe actions).
- Updates — check for updates on startup, or check now; shows your version.

Per-account settings (from the account picker) let you set that account's
soundpack.

## Push notifications

Turn on Push notifications under Settings, Notifications and FastSMRW will
notify you of mentions, boosts, favorites, follows and other activity even when
the app is closed. Android asks for permission to show notifications the first
time you turn it on. Tapping a notification opens FastSMRW.

Notifications cover every Mastodon account you are signed in to. Bluesky does
not offer them, so accounts there are skipped.

The text of a notification is encrypted by your Mastodon server and only
decrypted on your phone — the relay that wakes the app never sees it.
