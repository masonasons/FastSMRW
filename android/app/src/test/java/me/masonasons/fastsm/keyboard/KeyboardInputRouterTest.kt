package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class KeyboardInputRouterTest {
    @Test fun ordinaryTypingNeverRunsShortcutLookupEvenOnTheReadingScreen() {
        var lookups = 0; var actions = 0
        val router = KeyboardInputRouter { _, _, _, _, _ -> lookups++; Shortcut.NEW }
        val handler: (Shortcut) -> Boolean = { actions++; true }
        repeat(200_000) {
            assertFalse(router.keyDown(KeyEvent.KEYCODE_A, 0, false, false, false, false, handler))
            assertFalse(router.keyUp(KeyEvent.KEYCODE_A))
        }
        assertEquals(0, lookups); assertEquals(0, actions)
    }
    @Test fun writingAndSettingsDoNotDecodeOrTakeControlEditingCommands() {
        var lookups = 0
        val router = KeyboardInputRouter { _, _, _, _, _ -> lookups++; Shortcut.COPY }
        for (key in listOf(KeyEvent.KEYCODE_C, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_V, KeyEvent.KEYCODE_N, KeyEvent.KEYCODE_Z)) {
            assertFalse(router.keyDown(key, 0, true, false, false, false, null))
            assertFalse(router.keyUp(key))
        }
        assertEquals(0, lookups)
    }
    @Test fun heldNewPostCommandRemainsConsumedWhenComposerOpensAndControlIsReleased() {
        val router = KeyboardInputRouter(); var actions = 0
        assertTrue(router.keyDown(KeyEvent.KEYCODE_N, 0, true, false, false, false) { actions++; true })
        repeat(10) {
            assertTrue(router.keyDown(KeyEvent.KEYCODE_N, it+1, false, false, false, false, null))
        }
        assertEquals(1, actions)
        assertTrue(router.keyUp(KeyEvent.KEYCODE_N))
        assertFalse(router.keyDown(KeyEvent.KEYCODE_N, 0, false, false, false, false, null))
    }
    @Test fun unsupportedCommandsRetainAndroidHandling() {
        val router = KeyboardInputRouter(); var actions = 0
        assertFalse(router.keyDown(KeyEvent.KEYCODE_F1, 0, true, false, false, false) { actions++; true })
        assertFalse(router.keyUp(KeyEvent.KEYCODE_F1)); assertEquals(0, actions)
    }
    @Test fun altAndMetaDoNotRunTheCommandCatalog() {
        var lookups = 0
        val router = KeyboardInputRouter { _, _, _, _, _ -> lookups++; Shortcut.NEW }
        assertFalse(router.keyDown(KeyEvent.KEYCODE_N, 0, true, false, true, false) { true })
        assertFalse(router.keyDown(KeyEvent.KEYCODE_N, 0, true, false, false, true) { true })
        assertEquals(0, lookups)
    }
    @Test fun anOrphanRepeatCannotSubmitAnAction() {
        val router = KeyboardInputRouter(); var actions = 0
        assertFalse(router.keyDown(KeyEvent.KEYCODE_B, 3, true, false, false, false) { actions++; true })
        assertEquals(0, actions)
    }
    @Test fun aStablePostTagDoesNotBuildOrReadTheWholeTimeline() {
        data class Row(val id: String, val text: String)
        val rows = (0 until 50_000).map { Row("id$it", "Post $it") }
        var ids = 0; var texts = 0
        val selected = KeyboardShortcuts.focusedRow(KeyboardShortcuts.ROW_PREFIX+"id0", null, rows,
            { ids++; it.id }, { texts++; it.text })
        assertEquals("id0", selected); assertEquals(1, ids); assertEquals(0, texts)
    }
    @Test fun duplicateDescriptionsStopAtTheSecondMatchWithoutAllocatingAFilteredTimeline() {
        val rows = List(50_000) { "post$it" to "Same spoken text" }
        var inspected = 0
        assertNull(KeyboardShortcuts.focusedRow(null, "Same spoken text", rows,
            { it.first }, { inspected++; it.second }))
        assertEquals(2, inspected)
    }
}
