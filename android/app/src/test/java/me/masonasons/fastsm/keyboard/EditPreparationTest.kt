package me.masonasons.fastsm.keyboard

import org.junit.Assert.*
import org.junit.Test

class EditPreparationTest {
    @Test fun repeatedEditPressesDoNotQueueSeveralSourceRequests() {
        val gate=EditPreparation(); assertTrue(gate.begin())
        repeat(20) {assertFalse(gate.begin())}
        assertTrue(gate.finish()); assertFalse(gate.inFlight)
    }
    @Test fun aNewEditCanStartAfterTheFirstEditorWasPrepared() {
        val gate=EditPreparation(); gate.begin(); gate.finish()
        assertTrue(gate.begin())
    }
    @Test fun cancellationDiscardsTheLateSourceResult() {
        val gate=EditPreparation(); gate.begin(); gate.cancel()
        assertFalse(gate.finish())
        assertFalse(gate.inFlight)
    }
    @Test fun aCancelledRequestCannotBeOvertakenByAnotherQueuedEdit() {
        val gate=EditPreparation(); gate.begin(); gate.cancel()
        assertFalse(gate.begin()); assertFalse(gate.finish()); assertTrue(gate.begin())
    }
    @Test fun aRejectedEditUnlocksTheNextRequest() {
        val gate=EditPreparation(); gate.begin(); gate.fail()
        assertTrue(gate.begin())
    }
}
