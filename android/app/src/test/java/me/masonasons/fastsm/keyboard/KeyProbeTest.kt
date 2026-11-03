package me.masonasons.fastsm.keyboard

import org.junit.Assert.*
import org.junit.Test

class KeyProbeTest {
    @Test fun releaseOnlyControlRIsShownEvenWithoutAnyKeyDown() {
        val probe=KeyProbe()
        probe.record(KeySignal(1,46,4096,0,0,67109121,179187492,"window"))
        assertNull(probe.lastDown)
        assertEquals(46,probe.lastCommand!!.code)
        assertEquals(1,probe.lastCommand!!.action)
    }
    @Test fun menuNavigationDoesNotOverwriteTheLastModifiedCombination() {
        val probe=KeyProbe()
        probe.record(KeySignal(1,46,4096,0,0,67109121,179187492,"window"))
        probe.record(KeySignal(1,20,0,0,0,67109121,179189535,"window"))
        assertEquals(46,probe.lastCommand!!.code)
        probe.clear(); assertNull(probe.lastCommand)
    }
    private fun event(time: Long, route: String="window")=KeySignal(0,38,8192,0,0,257,time,route)
    @Test fun rawMetadataIsPreservedRatherThanGuessingACommand() {
        val probe=KeyProbe(); probe.record(event(1))
        assertEquals(8192,probe.lastDown!!.state)
        assertEquals(38,probe.lastDown!!.code)
        assertTrue(KeyboardMeta.control(probe.lastDown!!.state))
        assertFalse(KeyboardMeta.shift(probe.lastDown!!.state))
    }
    @Test fun duplicateWindowAndFieldDeliveryIsCountedOnce() {
        val probe=KeyProbe()
        assertTrue(probe.record(event(1)))
        assertFalse(probe.record(event(1,"field")))
        assertEquals(1,probe.totalEvents)
    }
    @Test fun releasedModifiersDoNotReplaceTheLastKeyDown() {
        val probe=KeyProbe(); probe.record(event(1))
        probe.record(KeySignal(1,113,0,0,0,257,2,"window"))
        assertEquals(38,probe.lastDown!!.code)
    }
    @Test fun historyIsBoundedButTheTotalRemainsAccurate() {
        val probe=KeyProbe(); repeat(1_000) { probe.record(event(it.toLong())) }
        assertEquals(32,probe.events.size); assertEquals(1_000,probe.totalEvents)
    }
    @Test fun committedInputIsCountedWithoutAcceptingText() {
        val probe=KeyProbe(); probe.textInput(3); probe.textInput(-1)
        assertEquals(3,probe.textInputCount)
    }
    @Test fun closingOrClearingCanEraseAllCapturedMetadata() {
        val probe=KeyProbe(); probe.record(event(1)); probe.textInput(2); probe.clear()
        assertEquals(0,probe.totalEvents); assertEquals(0,probe.textInputCount)
        assertTrue(probe.events.isEmpty()); assertNull(probe.lastDown)
    }
}
