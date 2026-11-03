# FastSMRW Android keyboard commands

Tested by the contributor on a BrailleSense 7: no layered command tried so far has failed, and Reply continues to work reliably across repeated uses. Only a couple of Control+Shift combinations have worked in this testing, and those have been less reliable. Control+Shift behavior on other Android devices has not been tested. These are user-reported device results, separate from automated tests; they are not a claim that every function or Android device has been tested.

## Keyboard shortcuts

The commands below apply on the main timeline reading screen unless another screen is stated. Focus the intended post before a post action. Control+Shift shortcuts remain available alongside the layered alternatives. In writing fields, ordinary text editing retains its usual behavior.

### Layered commands on BrailleSense 7

Press Control+V, release both keys, then press Control and the letter for the corresponding Shift command. Do not hold Shift on either stroke. Complete the second stroke within 30 seconds. Control+V again cancels. Normal unmodified navigation cancels a pending layer. Control+V continues to paste while writing.

Useful examples: Control+V then Control+J opens Home; Control+V then Control+K opens Notifications; Control+V then Control+M opens Mentions; Control+V then Control+W opens Mastodon Messages; Control+V then Control+R refreshes without requesting top; Control+V then Control+T opens the focused conversation; Control+V then Control+Z opens post actions.

### Complete reference

Control+N: Write a new post.

Control+R: Reply to the focused post.

Control+Q: Quote the focused post.

Control+B: Boost or unboost the focused post.

Control+F: Favourite or unfavourite the focused post.

Control+M: Bookmark or remove the bookmark.

Control+P: Open and play the focused post's media; pause or resume in the media viewer.

Control+S: Stop playback and close the media viewer.

Control+O: Open links in the focused post.

Control+Shift+O: Open the focused post itself in the web browser.

Layered alternative: Control+V, release, then Control+O.

Control+C: Copy the focused post.

Control+U: Open the author's timeline.

Control+Shift+U: Open the author's profile.

Layered alternative: Control+V, release, then Control+U.

Control+D: Write a direct message to the author on Mastodon.

Control+E: Edit your own focused post.

Control+L: Follow or unfollow the author.

Control+I: Read information about the author.

Control+Shift+I: Read the post this reply is answering.

Layered alternative: Control+V, release, then Control+I.

Control+Shift+N: Add or edit an alias for the author.

Layered alternative: Control+V, release, then Control+N.

Control+Enter: Open the focused conversation; send while writing.

Control+Shift+Enter: Open the focused post's actions menu.

Layered alternative: Control+V, release, then Control+Z.

Control+J: Switch to the next open timeline.

Control+K: Switch to the previous open timeline.

Control+Shift+J: Open the Home timeline.

Layered alternative: Control+V, release, then Control+J.

Control+Shift+K: Open Notifications.

Layered alternative: Control+V, release, then Control+K.

Control+Shift+M: Open Mentions.

Layered alternative: Control+V, release, then Control+M.

Control+Shift+D: Open Sent posts.

Layered alternative: Control+V, release, then Control+D.

Control+T: Add a timeline or search.

Control+W: Close the current timeline if it can be closed.

Control+Shift+R: Refresh the current timeline.

Layered alternative: Control+V, release, then Control+R.

Control+A: Toggle automatic reading of new posts in this timeline.

Control+Shift+F: Find text in the current timeline.

Layered alternative: Control+V, release, then Control+F.

Control+G: Find the next match.

Control+Shift+G: Find the previous match.

Layered alternative: Control+V, release, then Control+G.

Control+Shift+L: Open the current timeline's display filter.

Layered alternative: Control+V, release, then Control+L.

Control+Shift+P: Pin or unpin the current timeline.

Layered alternative: Control+V, release, then Control+P.

Control+Shift+Q: Mute or unmute the current timeline's sounds.

Layered alternative: Control+V, release, then Control+Q.

Control+Shift+A: Add an account.

Layered alternative: Control+V, release, then Control+A.

Control+Shift+C: Switch to the next account.

Layered alternative: Control+V, release, then Control+C.

Control+Shift+B: Switch to the previous account.

Layered alternative: Control+V, release, then Control+B.

Control+Shift+S: Open settings for this account.

Layered alternative: Control+V, release, then Control+S.

Control+Shift+E: Edit your profile.

Layered alternative: Control+V, release, then Control+E.

Control+Y: Open application settings.

Control+H: Open keyboard help.

Control+X: Open keyboard test.

Control+Z: Go to the top of the current timeline.

Control+Shift+W: Open Messages on Mastodon.

Layered alternative: Control+V, release, then Control+W.

### Writing field keys

Control+G: Send from the focused main writing field. Control+Enter and Control+Shift+T also send from that field. On the timeline Control+G retains Find next.

Shift+Enter and Control+Shift+Enter: Insert a new line in the main writing field. Plain Enter sends if Return key sends the post is enabled in Settings, General; otherwise it inserts a new line.

Control+C, Control+V, Control+X, Control+A and Control+Z retain normal copying, pasting, cutting, selecting and undo while writing. Timeline commands are inactive there.

## Reading, timelines and editing

Use the usual Android or BrailleSense reading commands to focus a post. App actions use actual keyboard or screen-reader post focus and refuse a stale or ambiguous post. The post action Use keyboard commands on this post supplies a fallback target.

Refresh and opening Home retain the local reading place. Control+Z, or More, Go to top, explicitly moves to the first currently loaded post without refreshing. Refresh first, wait for new posts, then use Go to top if you want newly fetched posts at the top. Control+Z remains Undo in the writer.

Local reading places are separate for each account and timeline. They record the visible post ID, screen offset and focused post. They do not record a particular word or braille cursor. A saved post must still be available for exact restoration. Once a local place exists, automatic server position updates do not pull the viewport away from it.

More has Go to top, Mentions and, on Mastodon accounts, Messages near its beginning. Messages is the existing Conversations timeline; Add timeline calls it Messages (Conversations). Control+V then Control+M opens Mentions and Control+V then Control+W opens Messages.

Press Control+E once on your own focused post. Opening editor announces that the original text is being retrieved. Repeated presses do not queue additional pending editors. Cancel editor ignores the delayed result; it does not abort the underlying server fetch. Another draft or account switch also cancels that pending editor.

## Keyboard delivery test

Control+X opens Keyboard test from a screen that owns app commands. It is also under More, Keyboard test and Keyboard help. Control+X remains Cut while writing. Try a command before entering the test input, then activate that input and repeat the combination. Read last result retains the latest modified key while you navigate controls.

Some BrailleSense input reaches Android as key-release events without a corresponding press. The router accepts both release-only input and complete press-and-release pairs, while suppressing duplicate, cancelled and held action/send events. A fresh press can recover after a lost release.

The diagnostic window retains at most 32 key-event metadata records in memory and counts committed characters without retaining them. Copy test results writes the diagnostic report to the clipboard only when requested. No report is sent automatically.
