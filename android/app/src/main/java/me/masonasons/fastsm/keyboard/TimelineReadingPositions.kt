package me.masonasons.fastsm.keyboard

data class TimelineReadingPlace(val visibleId: String, val offset: Int, val index: Int,
                                val focusedId: String = visibleId)
data class TimelineRestore(val index: Int, val offset: Int, val exact: Boolean)

/** Local viewport anchors are distinct from the core/server's selected row.
 * Stable row IDs survive prepends; offsets survive leaving for a reply. */
class TimelineReadingPositions(
    private val load: (String) -> TimelineReadingPlace? = { null },
    private val save: (String, TimelineReadingPlace) -> Unit = { _, _ -> },
) {
    private val places = mutableMapOf<String, TimelineReadingPlace?>()
    private val focusEpochs = mutableMapOf<String, Long>()
    fun get(key: String): TimelineReadingPlace? {
        if (!places.containsKey(key)) places[key] = load(key)
        return places[key]
    }
    fun focusEpoch(key: String): Long = focusEpochs[key] ?: 0
    private fun put(key: String, place: TimelineReadingPlace): Boolean {
        if (get(key) == place) return false
        places[key] = place
        save(key, place)
        return true
    }
    fun viewport(key: String, id: String, offset: Int, index: Int): Boolean {
        if (id.isBlank()) return false
        val old = get(key)
        return put(key, TimelineReadingPlace(id, offset.coerceAtLeast(0), index.coerceAtLeast(0),
            old?.focusedId?.takeIf { it.isNotBlank() } ?: id))
    }
    fun focus(key: String, id: String) {
        if (id.isBlank()) return
        focusEpochs[key] = focusEpoch(key) + 1
        val old = get(key)
        put(key, old?.copy(focusedId = id) ?: TimelineReadingPlace(id, 0, 0, id))
    }
    fun top(key: String, id: String) {
        if (id.isBlank()) return
        focusEpochs[key] = focusEpoch(key) + 1
        put(key, TimelineReadingPlace(id, 0, 0, id))
    }
    fun restore(key: String, ids: List<String>, coreSelected: String): TimelineRestore {
        return resolve(get(key),ids,coreSelected)
    }
    fun preferredSelection(key: String, ids: List<String>, coreSelected: String): String {
        val place = get(key) ?: return coreSelected
        return when {
            place.focusedId in ids -> place.focusedId
            place.visibleId in ids -> place.visibleId
            else -> coreSelected
        }
    }
    fun acceptsCoreMove(key: String, explicitlyRequested: Boolean): Boolean =
        explicitlyRequested || get(key) == null
    companion object {
        fun resolve(place: TimelineReadingPlace?, ids: List<String>, coreSelected: String): TimelineRestore {
            val target = place?.visibleId ?: coreSelected
            val index = ids.indexOf(target)
            if (index >= 0) return TimelineRestore(index,place?.offset ?: 0,true)
            return TimelineRestore((place?.index ?: 0).coerceIn(0,(ids.size-1).coerceAtLeast(0)),0,target.isBlank())
        }
    }
}
