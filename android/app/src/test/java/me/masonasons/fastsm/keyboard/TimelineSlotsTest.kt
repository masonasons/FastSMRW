package me.masonasons.fastsm.keyboard

import org.junit.Assert.*
import org.junit.Test

class TimelineSlotsTest {
    @Test fun reorderingMovesCachedRowsToTheirTimeline() {
        assertEquals(mapOf(0 to "notifications-rows",1 to "home-rows"),
            remapTimelineSlots(listOf("home","notifications"),listOf("notifications","home"),
                mapOf(0 to "home-rows",1 to "notifications-rows")))
    }
    @Test fun closingATabDoesNotGiveTheNextTabItsPosts() {
        assertEquals(mapOf(0 to "home",1 to "messages"),
            remapTimelineSlots(listOf("home","mentions","messages"),listOf("home","messages"),
                mapOf(0 to "home",1 to "mentions",2 to "messages")))
    }
    @Test fun aNewTimelineWaitsForItsOwnRows() {
        assertEquals(mapOf(0 to "home"),
            remapTimelineSlots(listOf("home"),listOf("home","messages"),mapOf(0 to "home")))
    }
}
