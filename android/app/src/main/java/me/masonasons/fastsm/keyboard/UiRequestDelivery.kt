package me.masonasons.fastsm.keyboard

/** Lives with the ViewModel request, so remounting a row or screen cannot replay it. */
class UiRequestDelivery {
    private val delivered = java.util.concurrent.atomic.AtomicBoolean(false)
    fun claim(): Boolean = delivered.compareAndSet(false, true)
}
