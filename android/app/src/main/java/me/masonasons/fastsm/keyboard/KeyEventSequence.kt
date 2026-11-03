package me.masonasons.fastsm.keyboard

/** Track metadata with fixed storage, including ordinary editing keys. A key-up
 * may run an app command only when this Activity did not see its matching down.
 * All app command key codes are below 256. No characters are retained. */
class KeyEventSequence {
    enum class Release { MATCHED_DOWN, RELEASE_ONLY, DUPLICATE, CANCELLED }
    private val downSeen = BooleanArray(256)
    private val downTimes = LongArray(256)
    private val releaseSeen = BooleanArray(256)
    private val releaseTimes = LongArray(256)
    private val releaseDownTimes = LongArray(256)
    fun down(code: Int, downTime: Long) {
        if (code !in downSeen.indices) return
        downSeen[code] = true
        downTimes[code] = downTime
    }
    fun up(code: Int, downTime: Long, eventTime: Long, cancelled: Boolean): Release {
        if (code !in downSeen.indices) return Release.MATCHED_DOWN
        if (releaseSeen[code] && releaseTimes[code] == eventTime && releaseDownTimes[code] == downTime)
            return Release.DUPLICATE
        releaseSeen[code] = true
        releaseTimes[code] = eventTime
        releaseDownTimes[code] = downTime
        val matched = downSeen[code] && (downTime == 0L || downTimes[code] == 0L || downTimes[code] == downTime)
        if (matched || downTime >= downTimes[code]) downSeen[code] = false
        if (cancelled) return Release.CANCELLED
        return if (matched) Release.MATCHED_DOWN else Release.RELEASE_ONLY
    }
    fun clear() {
        downSeen.fill(false)
        releaseSeen.fill(false)
    }
}
