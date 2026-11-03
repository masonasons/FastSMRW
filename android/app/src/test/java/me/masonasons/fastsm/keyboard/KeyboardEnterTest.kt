package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class KeyboardEnterTest {
    @Test fun leftAndRightControlFlagsAreAcceptedWithoutTheUniversalFlag() {
        for (state in listOf(KeyEvent.META_CTRL_ON, KeyEvent.META_CTRL_LEFT_ON, KeyEvent.META_CTRL_RIGHT_ON)) {
            assertTrue(KeyboardMeta.control(state))
            assertEquals(Shortcut.THREAD, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_ENTER,
                KeyboardMeta.control(state), KeyboardMeta.shift(state)))
        }
        assertFalse(KeyboardMeta.control(KeyEvent.META_CAPS_LOCK_ON))
    }

    @Test fun directionalShiftAltAndMetaFlagsKeepTheirMeaning() {
        assertTrue(KeyboardMeta.shift(KeyEvent.META_SHIFT_LEFT_ON))
        assertTrue(KeyboardMeta.shift(KeyEvent.META_SHIFT_RIGHT_ON))
        assertTrue(KeyboardMeta.alt(KeyEvent.META_ALT_LEFT_ON))
        assertTrue(KeyboardMeta.alt(KeyEvent.META_ALT_RIGHT_ON))
        assertTrue(KeyboardMeta.meta(KeyEvent.META_META_LEFT_ON))
        assertTrue(KeyboardMeta.meta(KeyEvent.META_META_RIGHT_ON))
        val state = KeyEvent.META_CTRL_LEFT_ON or KeyEvent.META_SHIFT_RIGHT_ON
        assertEquals(Shortcut.ACTIONS, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_ENTER,
            KeyboardMeta.control(state), KeyboardMeta.shift(state)))
    }

    @Test fun modifiedActivationEnterWorksOnTheTimelineAndInTheWriter() {
        for (code in listOf(KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_DPAD_CENTER)) {
            assertEquals(Shortcut.THREAD, KeyboardShortcuts.resolve(code, true, false))
            assertEquals(Shortcut.ACTIONS, KeyboardShortcuts.resolve(code, true, true))
            assertTrue(ComposerKeys.sendChord(code, true, false))
            assertFalse(ComposerKeys.sendChord(code, true, true))
        }
    }

    @Test fun bareActivationNeverTurnsIntoASendOrTimelineCommand() {
        assertEquals(KeyEvent.KEYCODE_DPAD_CENTER, KeyboardMeta.enterCode(KeyEvent.KEYCODE_DPAD_CENTER, false))
        assertNull(KeyboardShortcuts.resolve(KeyEvent.KEYCODE_DPAD_CENTER, false, false))
        assertFalse(ComposerKeys.sendChord(KeyEvent.KEYCODE_DPAD_CENTER, false, false))
    }

    @Test fun letterAlternativeOpensTheSameConversationAndSendsWithoutEnter() {
        assertEquals(Shortcut.THREAD, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_T, true, true))
        assertTrue(ComposerKeys.sendChord(KeyEvent.KEYCODE_T, true, true))
        assertEquals(Shortcut.ADD_TIMELINE, KeyboardShortcuts.resolve(KeyEvent.KEYCODE_T, true, false))
        assertFalse(ComposerKeys.sendChord(KeyEvent.KEYCODE_T, true, false))
    }

    @Test fun editingCommandsAndForeignModifiersNeverUseTheDirectSendPath() {
        for (code in listOf(KeyEvent.KEYCODE_C, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_V, KeyEvent.KEYCODE_X,
            KeyEvent.KEYCODE_N, KeyEvent.KEYCODE_R)) {
            for (shift in listOf(false, true)) assertFalse(ComposerKeys.sendChord(code, true, shift))
        }
        assertFalse(ComposerKeys.sendChord(KeyEvent.KEYCODE_ENTER, true, false, alt = true))
        assertFalse(ComposerKeys.sendChord(KeyEvent.KEYCODE_T, true, true, meta = true))
        assertNull(KeyboardShortcuts.resolve(KeyEvent.KEYCODE_T, true, true, alt = true))
    }

    @Test fun directSendAndTimelineRoutingShareOneHeldKeyGuard() {
        val router=KeyboardInputRouter(); var sends=0; var timelineActions=0
        assertTrue(router.directKeyDown(KeyEvent.KEYCODE_ENTER, 0) { sends++; true })
        repeat(10) {
            assertTrue(router.keyDown(KeyEvent.KEYCODE_ENTER, it+1, true, false, false, false) {
                timelineActions++; true
            })
        }
        assertEquals(1, sends); assertEquals(0, timelineActions)
        assertTrue(router.keyUp(KeyEvent.KEYCODE_ENTER))
        assertTrue(router.keyDown(KeyEvent.KEYCODE_ENTER, 0, true, false, false, false) {
            timelineActions++; true
        })
        assertEquals(1, timelineActions)
    }

    @Test fun directSendNeedsAnInitialPressAndAnEligibleFocusedField() {
        val router=KeyboardInputRouter(); var sends=0
        assertFalse(router.directKeyDown(KeyEvent.KEYCODE_T, 4) { sends++; true })
        assertFalse(router.directKeyDown(KeyEvent.KEYCODE_T, 0) { false })
        assertFalse(router.keyUp(KeyEvent.KEYCODE_T)); assertEquals(0, sends)
    }
}
