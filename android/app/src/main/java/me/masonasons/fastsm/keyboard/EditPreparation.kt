package me.masonasons.fastsm.keyboard

/** Only one original-source fetch may own a pending editor. */
class EditPreparation {
    var inFlight = false
        private set
    private var cancelled = false
    fun begin(): Boolean {
        if (inFlight) return false
        inFlight = true; cancelled = false
        return true
    }
    fun cancel() { if (inFlight) cancelled = true }
    fun finish(): Boolean {
        val accept = !cancelled
        inFlight = false; cancelled = false
        return accept
    }
    fun fail() { inFlight = false; cancelled = false }
}
