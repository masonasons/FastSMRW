package me.masonasons.fastsm.keyboard

/** A held action key invokes its command once, including when a command opens
 * another screen before key-up. Unhandled keys remain available to Android. */
class ActionPresses {
    private val pressed = mutableMapOf<Int, Long>()
    fun isPressed(code: Int): Boolean = pressed.containsKey(code)
    fun isPressedEvent(code: Int, repeats: Int, downTime: Long): Boolean {
        val previous = pressed[code] ?: return false
        // A later initial down is a fresh press even when the old up was lost.
        // The same press identity, old events and repeats remain suppressed.
        if (repeats == 0 && downTime > 0 && downTime > previous) {
            pressed.remove(code)
            return false
        }
        return true
    }
    fun up(code: Int): Boolean = upEvent(code, 0)
    fun upEvent(code: Int, downTime: Long): Boolean {
        val previous = pressed[code] ?: return false
        if (downTime > 0 && downTime > previous) {
            pressed.remove(code)
            return false // a newer release-only press, not the old held command
        }
        if (downTime == 0L || previous == 0L || downTime == previous) pressed.remove(code)
        return true
    }
    fun clear() = pressed.clear()
    fun down(code: Int, repeatCount: Int, action: () -> Boolean): Boolean =
        downEvent(code, repeatCount, 0, action)
    fun downEvent(code: Int, repeatCount: Int, downTime: Long, action: () -> Boolean): Boolean {
        if (isPressedEvent(code, repeatCount, downTime)) return true
        if (repeatCount > 0) return false
        if (!action()) return false
        pressed[code] = downTime
        return true
    }
}
