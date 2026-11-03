package me.masonasons.fastsm.keyboard

data class KeySignal(val action: Int, val code: Int, val state: Int, val repeats: Int,
                     val flags: Int, val source: Int, val time: Long, val route: String,
                     val downTime: Long = 0)

/** Bounded, in-memory metadata only. No characters or draft text are accepted. */
class KeyProbe {
    private val captured = java.util.ArrayDeque<KeySignal>()
    val events: List<KeySignal> get() = captured.toList()
    var totalEvents = 0
        private set
    var textInputCount = 0
        private set
    var lastDown: KeySignal? = null
        private set
    var lastCommand: KeySignal? = null
        private set
    fun record(event: KeySignal): Boolean {
        val old = captured.peekLast()
        if (old != null && old.action == event.action && old.code == event.code &&
            old.state == event.state && old.time == event.time) return false
        captured.addLast(event)
        while (captured.size > 32) captured.removeFirst()
        totalEvents++
        if (event.action == 0) lastDown = event
        val modifier = when (event.code) {
            57,58,59,60,113,114,117,118 -> true
            else -> false
        }
        if (!modifier && (KeyboardMeta.control(event.state) || KeyboardMeta.shift(event.state) ||
                KeyboardMeta.alt(event.state) || KeyboardMeta.meta(event.state))) lastCommand = event
        return true
    }
    fun textInput(length: Int) { textInputCount += length.coerceAtLeast(0) }
    fun clear() { captured.clear(); totalEvents = 0; textInputCount = 0; lastDown = null; lastCommand = null }
}
