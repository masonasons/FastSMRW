package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class KeyboardShortcutsTest {
    @Test fun everyBindingIsUniqueAndRoundTrips() {
        assertEquals(Shortcut.entries.size, Shortcut.entries.map { it.letter to it.shifted }.distinct().size)
        for (s in Shortcut.entries) assertEquals(s, KeyboardShortcuts.resolve(s.letter, true, s.shifted))
    }
    @Test fun bareTypingAndForeignModifiersAreNeverCommands() {
        for (s in Shortcut.entries) {
            assertNull(KeyboardShortcuts.resolve(s.letter, false, s.shifted))
            assertNull(KeyboardShortcuts.resolve(s.letter, true, s.shifted, alt = true))
            assertNull(KeyboardShortcuts.resolve(s.letter, true, s.shifted, meta = true))
        }
    }
    @Test fun requiredActionsAndTimelinesHaveLetterBindings() {
        val expected = mapOf(
            KeyEvent.KEYCODE_N to Shortcut.NEW, KeyEvent.KEYCODE_R to Shortcut.REPLY,
            KeyEvent.KEYCODE_B to Shortcut.BOOST, KeyEvent.KEYCODE_P to Shortcut.MEDIA,
            KeyEvent.KEYCODE_O to Shortcut.LINKS, KeyEvent.KEYCODE_J to Shortcut.NEXT_TIMELINE,
            KeyEvent.KEYCODE_K to Shortcut.PREVIOUS_TIMELINE)
        for ((key, action) in expected) assertEquals(action, KeyboardShortcuts.resolve(key, true, false))
        assertEquals(Shortcut.MENTIONS, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_M, true, true))
        assertEquals(Shortcut.SENT, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_D, true, true))
        assertEquals(Shortcut.HOME, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_J, true, true))
        assertEquals(Shortcut.NOTIFICATIONS, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_K, true, true))
    }
    @Test fun noNavigationDependsOnArrowNumberOrFunctionKeys() {
        for (s in Shortcut.entries) assertTrue(s.letter in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z || s.letter == KeyEvent.KEYCODE_ENTER)
        assertNull(KeyboardShortcuts.resolve(KeyEvent.KEYCODE_DPAD_DOWN, true, false))
        assertNull(KeyboardShortcuts.resolve(KeyEvent.KEYCODE_1, true, false))
    }
    @Test fun keypadEnterMatchesEnter() {
        assertEquals(Shortcut.THREAD, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_NUMPAD_ENTER, true, false))
        assertEquals(Shortcut.ACTIONS, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_NUMPAD_ENTER, true, true))
    }
    private val rows = listOf("one" to "A post", "two" to "Another post", "three" to "A post")
    @Test fun stableFocusedIdDisambiguatesIdenticalSpokenPosts() {
        assertEquals("three", KeyboardShortcuts.focusedRow(KeyboardShortcuts.ROW_PREFIX+"three", "A post", rows))
    }
    @Test fun ambiguousDescriptionDoesNotChooseTheWrongPost() {
        assertNull(KeyboardShortcuts.focusedRow(null, "A post", rows))
        assertEquals("two", KeyboardShortcuts.focusedRow(null, "Another post", rows))
    }
    @Test fun removedPostsAndToolbarFocusDoNotReuseASelection() {
        assertNull(KeyboardShortcuts.focusedRow(KeyboardShortcuts.ROW_PREFIX+"gone", "Another post", rows))
        assertNull(KeyboardShortcuts.focusedRow(null, "Refresh", rows))
        assertNull(KeyboardShortcuts.focusedRow(null, null, rows))
        assertNull(KeyboardShortcuts.focusedRow(KeyboardShortcuts.ROW_PREFIX+"one", null, emptyList()))
    }
    @Test fun heldKeyDoesNotRepeatAnExternalActionAcrossScreenChanges() {
        val presses=ActionPresses(); var actions=0
        assertTrue(presses.down(1,0) { actions++; true })
        repeat(10) { assertTrue(presses.down(1,it+1) { actions++; true }) }
        assertEquals(1,actions)
        assertTrue(presses.up(1))
        assertTrue(presses.down(1,0) { actions++; true })
        assertEquals(2,actions)
    }
    @Test fun unhandledKeysAndRepeatsKeepNormalAndroidHandling() {
        val presses=ActionPresses(); var actions=0
        assertFalse(presses.down(1,0) { false })
        assertFalse(presses.up(1))
        assertFalse(presses.down(1,4) { actions++; true })
        assertEquals(0,actions)
        assertTrue(presses.down(2,0) { true })
        presses.clear()
        assertFalse(presses.up(2))
    }
    @Test fun enterToSendAndShiftNewlineAreIndependent() {
        for (option in listOf(false,true)) {
            assertEquals(EnterAction.SEND, ComposerKeys.enter(true,false,option))
            assertEquals(EnterAction.NEWLINE, ComposerKeys.enter(false,true,option))
            assertEquals(EnterAction.NEWLINE, ComposerKeys.enter(true,true,option))
            assertEquals(EnterAction.CONSUME, ComposerKeys.enter(true,false,option,repeat=true))
        }
        assertEquals(EnterAction.DEFAULT, ComposerKeys.enter(false,false,false))
        assertEquals(EnterAction.SEND, ComposerKeys.enter(false,false,true))
    }
    @Test fun otherEnterModifiersDoNotSend() {
        assertEquals(EnterAction.DEFAULT, ComposerKeys.enter(true,false,true,alt=true))
        assertEquals(EnterAction.DEFAULT, ComposerKeys.enter(true,false,true,meta=true))
    }
}
