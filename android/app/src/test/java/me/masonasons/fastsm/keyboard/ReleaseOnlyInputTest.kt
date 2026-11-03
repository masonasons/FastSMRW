package me.masonasons.fastsm.keyboard

import android.view.KeyEvent
import org.junit.Assert.*
import org.junit.Test

class ReleaseOnlyInputTest {
    private fun up(router: KeyboardInputRouter, code: Int, time: Long, control: Boolean=true,
                   cancelled: Boolean=false, handler: ((Shortcut)->Boolean)?=null,
                   send: (()->Boolean)?=null): Boolean = router.event(KeyEvent.ACTION_UP,code,0,0,time,
                       control,false,false,false,cancelled,handler,send)

    @Test fun theUsersExactReleaseOnlyReportRunsReplyForBothRReleases() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        assertTrue(up(router,KeyEvent.KEYCODE_R,179187492,handler=handler))
        assertFalse(up(router,KeyEvent.KEYCODE_DPAD_DOWN,179189535,false,handler=handler))
        assertFalse(up(router,KeyEvent.KEYCODE_DPAD_DOWN,179190980,false,handler=handler))
        assertTrue(up(router,KeyEvent.KEYCODE_R,179217823,handler=handler))
        assertEquals(listOf(Shortcut.REPLY,Shortcut.REPLY),commands)
    }
    @Test fun completeDownAndUpRunsOneReplyRatherThanTwo() {
        val router=KeyboardInputRouter(); var replies=0
        val handler: (Shortcut)->Boolean={ replies++; true }
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,100,true,false,false,false,handler=handler))
        assertTrue(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_R,0,100,120,true,false,false,false,handler=handler))
        assertEquals(1,replies)
    }
    @Test fun releaseOnlyReplyCanFollowAnEarlierReplyWhoseUpWasLost() {
        val router=KeyboardInputRouter(); var replies=0
        val handler: (Shortcut)->Boolean={replies++;true}
        router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,100,true,false,false,false,handler=handler)
        assertTrue(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_R,0,200,220,true,false,false,false,handler=handler))
        assertEquals(2,replies)
    }
    @Test fun aDuplicatedReleaseIsConsumedWithoutRepeatingTheCommand() {
        val router=KeyboardInputRouter(); var actions=0
        val handler: (Shortcut)->Boolean={ actions++; true }
        repeat(5) { assertTrue(up(router,KeyEvent.KEYCODE_R,123,handler=handler)) }
        assertEquals(1,actions)
    }
    @Test fun aCancelledReleaseCannotReplyOrSend() {
        val router=KeyboardInputRouter(); var actions=0
        assertFalse(up(router,KeyEvent.KEYCODE_R,123,cancelled=true,handler={ actions++; true }))
        assertFalse(up(router,KeyEvent.KEYCODE_G,124,cancelled=true,send={ actions++; true }))
        assertEquals(0,actions)
    }
    @Test fun releaseOnlyPrefixAndSecondStrokeWorkAndThenReset() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        up(router,KeyEvent.KEYCODE_V,1,handler=handler)
        up(router,KeyEvent.KEYCODE_R,2,handler=handler)
        up(router,KeyEvent.KEYCODE_R,3,handler=handler)
        assertEquals(listOf(Shortcut.REFRESH,Shortcut.REPLY),commands)
    }
    @Test fun releaseOnlyNavigationCancelsAnUnfinishedPrefix() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        up(router,KeyEvent.KEYCODE_V,1,handler=handler)
        assertFalse(up(router,KeyEvent.KEYCODE_DPAD_DOWN,2,false,handler=handler))
        up(router,KeyEvent.KEYCODE_R,3,handler=handler)
        assertEquals(listOf(Shortcut.REPLY),commands)
    }
    @Test fun ordinaryWriterEditingAndUnmodifiedReleasesNeverRunAppCommands() {
        val router=KeyboardInputRouter(); var actions=0
        for (key in listOf(KeyEvent.KEYCODE_C,KeyEvent.KEYCODE_V,KeyEvent.KEYCODE_X,KeyEvent.KEYCODE_A))
            assertFalse(up(router,key,key.toLong(),send={ actions++; true }))
        assertFalse(up(router,KeyEvent.KEYCODE_R,100,false,handler={ actions++; true }))
        assertEquals(0,actions)
    }
    @Test fun releaseOnlySendIsOnceAndCompleteSendAlsoStaysOnce() {
        val router=KeyboardInputRouter(); var sends=0
        val send: ()->Boolean={ sends++; true }
        assertTrue(up(router,KeyEvent.KEYCODE_G,100,send=send))
        assertTrue(up(router,KeyEvent.KEYCODE_G,100,send=send))
        assertTrue(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_G,0,200,200,true,false,false,false,send=send))
        assertTrue(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_G,0,200,220,true,false,false,false,send=send))
        assertEquals(2,sends)
    }
    @Test fun aDownSeenOnAnotherScreenDoesNotTurnItsUpIntoAReply() {
        val router=KeyboardInputRouter(); var replies=0
        assertFalse(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_R,0,100,100,true,false,false,false))
        assertFalse(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_R,0,100,120,true,false,false,false,handler={replies++;true}))
        assertEquals(0,replies)
    }
    @Test fun keyboardTestCanEscapeAPendingPrefixWithoutAFocusedPost() {
        val router=KeyboardInputRouter(); val commands=mutableListOf<Shortcut>()
        val handler: (Shortcut)->Boolean={ commands.add(it); true }
        up(router,KeyEvent.KEYCODE_V,1,handler=handler)
        up(router,KeyEvent.KEYCODE_X,2,handler=handler)
        up(router,KeyEvent.KEYCODE_R,3,handler=handler)
        assertEquals(listOf(Shortcut.KEYBOARD_TEST,Shortcut.REPLY),commands)
    }
    @Test fun ordinaryFullEventStreamsStillBypassTheCommandCatalog() {
        var lookups=0; var actions=0
        val router=KeyboardInputRouter { _,_,_,_,_-> lookups++; Shortcut.NEW }
        val handler: (Shortcut)->Boolean={ actions++; true }
        repeat(100_000) {
            val stamp=it.toLong()*2+1
            assertFalse(router.event(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_A,0,stamp,stamp,false,false,false,false,handler=handler))
            assertFalse(router.event(KeyEvent.ACTION_UP,KeyEvent.KEYCODE_A,0,stamp,stamp+1,false,false,false,false,handler=handler))
        }
        assertEquals(0,lookups); assertEquals(0,actions)
    }
}
