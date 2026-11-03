package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class KeyboardPrefixTest {
    @Test fun pressingControlAgainBetweenStrokesPreservesThePrefix() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler)
        assertFalse(router.keyDown(KeyEvent.KEYCODE_CTRL_LEFT,0,true,false,false,false,handler))
        router.keyUp(KeyEvent.KEYCODE_CTRL_LEFT)
        press(router,KeyEvent.KEYCODE_J,handler)
        assertEquals(listOf(Shortcut.HOME),commands)
    }
    private fun press(router: KeyboardInputRouter, key: Int, handler: (Shortcut) -> Boolean): Boolean {
        val result=router.keyDown(key,0,true,false,false,false,handler)
        router.keyUp(key)
        return result
    }
    @Test fun everyShiftCommandCanBeReachedWithoutShiftOrEnter() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        for (command in Shortcut.entries.filter { it.shifted }) {
            assertTrue(press(router,KeyEvent.KEYCODE_V,handler))
            val letter=if (command==Shortcut.ACTIONS) KeyEvent.KEYCODE_Z else command.letter
            assertTrue(press(router,letter,handler))
            assertEquals(command,commands.last())
        }
    }
    @Test fun conversationAndSendHaveNoShiftNoEnterRoutes() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler); press(router,KeyEvent.KEYCODE_T,handler)
        assertEquals(listOf(Shortcut.THREAD),commands)
        assertTrue(ComposerKeys.sendChord(KeyEvent.KEYCODE_G,true,false))
        assertFalse(ComposerKeys.sendChord(KeyEvent.KEYCODE_G,true,true))
    }
    @Test fun prefixIsOneShotAndNormalShortcutsKeepTheirActions() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler); press(router,KeyEvent.KEYCODE_J,handler)
        press(router,KeyEvent.KEYCODE_J,handler)
        assertEquals(listOf(Shortcut.HOME,Shortcut.NEXT_TIMELINE),commands)
    }
    @Test fun timeoutConsumesTheIntendedShiftCommandInsteadOfRunningAnotherAction() {
        var now=0L; val router=KeyboardInputRouter(clock={now}); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler); now=30_001
        assertTrue(press(router,KeyEvent.KEYCODE_R,handler))
        assertTrue(commands.isEmpty())
    }
    @Test fun prefixCancellationAndScreenChangesNeverLeakIntoTextEditing() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler); router.cancelPrefix()
        press(router,KeyEvent.KEYCODE_J,handler)
        assertEquals(listOf(Shortcut.NEXT_TIMELINE),commands)
        assertFalse(router.keyDown(KeyEvent.KEYCODE_V,0,true,false,false,false,null))
        assertFalse(router.keyUp(KeyEvent.KEYCODE_V))
    }
    @Test fun ordinaryInputCancelsThePrefixAndIsNeverConsumed() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        press(router,KeyEvent.KEYCODE_V,handler)
        assertFalse(router.keyDown(KeyEvent.KEYCODE_A,0,false,false,false,false,handler))
        press(router,KeyEvent.KEYCODE_R,handler)
        assertEquals(listOf(Shortcut.REPLY),commands)
    }
    @Test fun heldPrefixAndActionCannotRepeatOrToggleTheirState() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        assertTrue(router.keyDown(KeyEvent.KEYCODE_V,0,true,false,false,false,handler))
        repeat(10) { assertTrue(router.keyDown(KeyEvent.KEYCODE_V,it+1,true,false,false,false,handler)) }
        router.keyUp(KeyEvent.KEYCODE_V)
        assertTrue(router.keyDown(KeyEvent.KEYCODE_J,0,true,false,false,false,handler))
        repeat(10) { assertTrue(router.keyDown(KeyEvent.KEYCODE_J,it+1,true,false,false,false,handler)) }
        assertEquals(listOf(Shortcut.HOME),commands)
    }
}
