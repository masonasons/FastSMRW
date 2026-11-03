package me.masonasons.fastsm.keyboard

import me.masonasons.fastsm.ui.CoreViewModel
import org.junit.Assert.*
import org.junit.Test

class UiRequestDeliveryTest {
    @Test fun aRetainedMenuRequestCannotReopenAfterItsRowIsRecreated() {
        val retained=CoreViewModel.KeyboardRowRequest("post","menu",1)
        assertTrue(retained.delivery.claim())
        // A fresh StatusRow sees the same retained ViewModel request.
        assertFalse(retained.delivery.claim())
    }
    @Test fun returningFromAReplyCannotReplayAnOldScrollRequest() {
        val retained=CoreViewModel.ScrollRequest(0,"old-post",1)
        assertTrue(retained.delivery.claim())
        assertFalse(retained.delivery.claim())
    }
    @Test fun aNewUserRequestStillRunsAfterTheOldOneWasDelivered() {
        val first=CoreViewModel.KeyboardRowRequest("post","menu",1)
        val next=CoreViewModel.KeyboardRowRequest("post","menu",2)
        assertTrue(first.delivery.claim()); assertTrue(next.delivery.claim())
        assertFalse(first.delivery.claim()); assertFalse(next.delivery.claim())
    }
    @Test fun aPendingRequestIsAvailableWhenItsTargetFinallyAppears() {
        val retained=CoreViewModel.ScrollRequest(0,"later-post",1)
        // No consumer claims a request until its row is present.
        assertTrue(retained.delivery.claim())
        assertFalse(retained.delivery.claim())
    }
}
