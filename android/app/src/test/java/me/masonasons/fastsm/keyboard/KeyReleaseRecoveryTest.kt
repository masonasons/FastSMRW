package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class KeyReleaseRecoveryTest {
    @Test fun aNewReplyPressWorksAfterThePreviousReleaseWasLost() {
        val presses=ActionPresses(); var replies=0
        assertTrue(presses.downEvent(KeyEvent.KEYCODE_R,0,100) { replies++; true })
        // Return from the reply screen with no R-up delivered to the Activity.
        assertTrue(presses.downEvent(KeyEvent.KEYCODE_R,0,200) { replies++; true })
        assertEquals(2,replies)
    }
    @Test fun aHeldKeyWithTheSameDownTimeStillRunsOnceEvenWithAnIncorrectZeroRepeat() {
        val presses=ActionPresses(); var sends=0
        repeat(10) { assertTrue(presses.downEvent(KeyEvent.KEYCODE_G,0,100) { sends++; true }) }
        assertEquals(1,sends)
    }
    @Test fun anOldLateReleaseDoesNotUnlockANewerHeldSend() {
        val presses=ActionPresses(); var sends=0
        presses.downEvent(KeyEvent.KEYCODE_G,0,100) { sends++; true }
        presses.downEvent(KeyEvent.KEYCODE_G,0,200) { sends++; true }
        assertTrue(presses.upEvent(KeyEvent.KEYCODE_G,100))
        assertTrue(presses.downEvent(KeyEvent.KEYCODE_G,1,200) { sends++; true })
        assertEquals(2,sends)
    }
    @Test fun unknownPressIdentitiesRemainConservativeUntilRelease() {
        val presses=ActionPresses(); var sends=0
        repeat(5) { presses.downEvent(KeyEvent.KEYCODE_G,0,0) { sends++; true } }
        assertEquals(1,sends)
        presses.upEvent(KeyEvent.KEYCODE_G,0)
        presses.downEvent(KeyEvent.KEYCODE_G,0,0) { sends++; true }
        assertEquals(2,sends)
    }
    @Test fun freshDownAfterReplyReturnsCanRecoverWithoutTheOldUp() {
        val router=KeyboardInputRouter(); var replies=0
        val handler: (Shortcut)->Boolean={ replies++; true }
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,100,true,false,false,false,handler=handler))
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,200,200,true,false,false,false,handler=handler))
        assertEquals(2,replies)
    }
    @Test fun heldCommandCannotBecomeDraftTextButAFreshTypingPressCan() {
        val router=KeyboardInputRouter()
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,100,true,false,false,false,handler={true}))
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,110,false,false,false,false))
        assertFalse(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,200,200,false,false,false,false))
    }
    @Test fun anotherPrefixWorksEvenIfTheFirstPrefixKeyUpWasLost() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_V,0,100,100,true,false,false,false,handler=handler)
        router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_J,0,200,200,true,false,false,false,handler=handler)
        router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_J,0,200,220,true,false,false,false,handler=handler)
        router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_V,0,300,300,true,false,false,false,handler=handler)
        router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_K,0,400,400,true,false,false,false,handler=handler)
        assertEquals(listOf(Shortcut.HOME,Shortcut.NOTIFICATIONS),commands)
    }
}
