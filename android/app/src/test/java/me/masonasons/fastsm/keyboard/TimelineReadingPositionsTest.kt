package me.masonasons.fastsm.keyboard

import org.junit.Assert.*
import org.junit.Test

class TimelineReadingPositionsTest {
    @Test fun incomingPostsKeepTheSameVisiblePostAndOffset() {
        val store=TimelineReadingPositions()
        store.viewport("account|home","reading",67,1)
        assertEquals(TimelineRestore(3,67,true),store.restore("account|home",listOf("new2","new1","old","reading","end"),"new2"))
    }
    @Test fun leavingForAReplyAndReturningRestoresByIdInsteadOfOldIndex() {
        val disk=mutableMapOf<String,TimelineReadingPlace>()
        val before=TimelineReadingPositions(load={disk[it]},save={key,place->disk[key]=place})
        before.viewport("a|home","post",34,2); before.focus("a|home","focus")
        val after=TimelineReadingPositions(load={disk[it]})
        assertEquals(TimelineRestore(4,34,true),after.restore("a|home",listOf("n1","n2","a","b","post","focus"),"n1"))
    }
    @Test fun focusDoesNotReplaceTheViewportAnchorWithADifferentVisibleRow() {
        val store=TimelineReadingPositions()
        store.viewport("home","first-visible",52,10)
        store.focus("home","being-read")
        assertEquals("first-visible",store.get("home")!!.visibleId)
        assertEquals(52,store.get("home")!!.offset)
        assertEquals("being-read",store.get("home")!!.focusedId)
    }
    @Test fun viewportReportingDoesNotOverwriteTheFocusedReadingPost() {
        val store=TimelineReadingPositions()
        store.focus("home","read-post"); store.viewport("home","above-read-post",20,5)
        assertEquals("read-post",store.preferredSelection("home",listOf("above-read-post","read-post"),"server-post"))
    }
    @Test fun backgroundCoreSelectionsCannotMoveAnEstablishedPlace() {
        val store=TimelineReadingPositions(); store.viewport("home","post",40,7)
        repeat(20) { assertFalse(store.acceptsCoreMove("home",false)) }
        assertEquals(TimelineReadingPlace("post",40,7,"post"),store.get("home"))
    }
    @Test fun anExplicitNavigationRequestIsStillAllowed() {
        val store=TimelineReadingPositions(); store.viewport("home","post",40,7)
        assertTrue(store.acceptsCoreMove("home",true))
    }
    @Test fun anInitialTimelineWithoutALocalPlaceCanUseTheCorePosition() {
        val store=TimelineReadingPositions()
        assertTrue(store.acceptsCoreMove("fresh",false))
        assertEquals(TimelineRestore(1,0,true),store.restore("fresh",listOf("new","saved"),"saved"))
    }
    @Test fun topIsOnlyAppliedWhenExplicitlyRequested() {
        val store=TimelineReadingPositions(); store.viewport("home","reading",22,8)
        store.restore("home",listOf("new","reading"),"new")
        assertEquals("reading",store.get("home")!!.visibleId)
        store.top("home","new")
        assertEquals(TimelineReadingPlace("new",0,0,"new"),store.get("home"))
    }
    @Test fun accountAndTimelinePositionsRemainIndependent() {
        val store=TimelineReadingPositions()
        store.viewport("a|home","a-home",1,2); store.viewport("a|mentions","a-mention",3,4)
        store.viewport("b|home","b-home",5,6)
        assertEquals("a-home",store.get("a|home")!!.visibleId)
        assertEquals("a-mention",store.get("a|mentions")!!.visibleId)
        assertEquals("b-home",store.get("b|home")!!.visibleId)
    }
    @Test fun aDeletedAnchorFallsBackToANearbyAvailableIndex() {
        val store=TimelineReadingPositions(); store.viewport("home","deleted",99,10)
        assertEquals(TimelineRestore(2,0,false),store.restore("home",listOf("one","two","three"),"one"))
    }
    @Test fun repeatedUnchangedViewportReportsDoNotWriteAgain() {
        var writes=0; val store=TimelineReadingPositions(save={_,_->writes++})
        assertTrue(store.viewport("home","post",4,2))
        repeat(1000) { assertFalse(store.viewport("home","post",4,2)) }
        assertEquals(1,writes)
    }
    @Test fun aNewReadingFocusChangesTheEpochForLateRestoreProtection() {
        val store=TimelineReadingPositions(); val entry=store.focusEpoch("home")
        store.focus("home","chosen")
        assertTrue(store.focusEpoch("home")>entry)
    }
}
