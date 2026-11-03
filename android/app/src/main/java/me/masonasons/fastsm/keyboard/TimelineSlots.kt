package me.masonasons.fastsm.keyboard

/** The native event uses indices; UI caches must follow timeline identities. */
fun <T> remapTimelineSlots(oldKeys: List<String>, newKeys: List<String>, values: Map<Int,T>): Map<Int,T> {
    val byKey = oldKeys.mapIndexedNotNull { index, key -> values[index]?.let { key to it } }.toMap()
    return newKeys.mapIndexedNotNull { index, key -> byKey[key]?.let { index to it } }.toMap()
}
