package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class AllShortcutEventFormsTest {
    @Test fun everyCatalogShortcutRunsOnceFromReleaseOnlyInput() {
        assertEquals(47,Shortcut.entries.size)
        for (shortcut in Shortcut.entries) {
            val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
            val handler: (Shortcut)->Boolean={commands.add(it);true}
            repeat(2) {
                assertTrue(shortcut.chord,router.event(KeyEvent.ACTION_UP,shortcut.letter,0,100,120,
                    true,shortcut.shifted,false,false,handler=handler))
            }
            assertEquals(shortcut.chord,listOf(shortcut),commands)
        }
    }
    @Test fun everyCatalogShortcutRunsOnceFromCompleteInput() {
        for (shortcut in Shortcut.entries) {
            val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
            val handler: (Shortcut)->Boolean={commands.add(it);true}
            assertTrue(shortcut.chord,router.event(KeyEvent.ACTION_DOWN,shortcut.letter,0,100,100,
                true,shortcut.shifted,false,false,handler=handler))
            assertTrue(shortcut.chord,router.event(KeyEvent.ACTION_UP,shortcut.letter,0,100,120,
                true,shortcut.shifted,false,false,handler=handler))
            assertEquals(shortcut.chord,listOf(shortcut),commands)
        }
    }
    @Test fun everyShiftAlternativeWorksWithTwoReleaseOnlyStrokes() {
        for (shortcut in Shortcut.entries.filter {it.shifted}) {
            val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
            val handler: (Shortcut)->Boolean={commands.add(it);true}
            assertTrue(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_V,0,100,120,true,false,false,false,handler=handler))
            val letter=if(shortcut==Shortcut.ACTIONS) KeyEvent.KEYCODE_Z else shortcut.letter
            assertTrue(shortcut.chord,router.event(KeyEvent.ACTION_UP,letter,0,200,220,true,false,false,false,handler=handler))
            assertEquals(shortcut.chord,listOf(shortcut),commands)
        }
    }
    @Test fun everySendCombinationIsOnceForBothCompleteAndReleaseOnlyStreams() {
        val forms=listOf(KeyEvent.KEYCODE_G to false,KeyEvent.KEYCODE_ENTER to false,
            KeyEvent.KEYCODE_NUMPAD_ENTER to false,KeyEvent.KEYCODE_DPAD_CENTER to false,
            KeyEvent.KEYCODE_T to true)
        for((code,shift) in forms) for(complete in listOf(false,true)) {
            val router=KeyboardInputRouter(); var sends=0
            val send: ()->Boolean={sends++;true}
            if(complete) assertTrue(router.event(KeyEvent.ACTION_DOWN,code,0,100,100,true,shift,false,false,send=send))
            repeat(2) {assertTrue(router.event(KeyEvent.ACTION_UP,code,0,100,120,true,shift,false,false,send=send))}
            assertEquals("key $code shift $shift complete $complete",1,sends)
        }
    }
    @Test fun unmodifiedKeysAndWriterEditingNeverBecomeTimelineCommands() {
        for(shortcut in Shortcut.entries) {
            val router=KeyboardInputRouter(); var actions=0
            assertFalse(router.event(KeyEvent.ACTION_UP,shortcut.letter,0,100,120,false,
                shortcut.shifted,false,false,handler={actions++;true}))
            assertEquals(0,actions)
            if(!ComposerKeys.sendChord(shortcut.letter,true,shortcut.shifted)) {
                assertFalse(router.event(KeyEvent.ACTION_UP,shortcut.letter,0,200,220,true,
                    shortcut.shifted,false,false,send={actions++;true}))
                assertEquals(0,actions)
            }
        }
    }
}
