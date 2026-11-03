package me.masonasons.fastsm.keyboard

import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityNodeProvider
import android.view.accessibility.AccessibilityEvent
import androidx.core.view.AccessibilityDelegateCompat
import androidx.core.view.ViewCompat
import androidx.core.view.accessibility.AccessibilityNodeProviderCompat
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat
import java.util.WeakHashMap

/** Preserve Compose's complete accessibility delegate and node provider while
 * observing successful focus actions on its virtual post nodes. Parent events
 * alone are insufficient: an app-local AccessibilityEvent can have no source
 * connection, so getSource() cannot reliably identify its virtual row. */
class KeyboardFocusBridge(private val focus: (String?, String?) -> Unit) {
    private data class Binding(val original: AccessibilityDelegateCompat, val observer: AccessibilityDelegateCompat)
    private val installed = WeakHashMap<View, Binding>()

    /** Restore the exact original delegate outside the post-reading screen. */
    fun uninstall() {
        for ((view, binding) in installed.toMap()) {
            if (ViewCompat.getAccessibilityDelegate(view) === binding.observer)
                ViewCompat.setAccessibilityDelegate(view, binding.original)
        }
        installed.clear()
    }

    fun install(root: View) {
        if (root.javaClass.name == "androidx.compose.ui.platform.AndroidComposeView" && !installed.containsKey(root)) {
            val original = ViewCompat.getAccessibilityDelegate(root)
            if (original != null) {
                val observer = object : AccessibilityDelegateCompat() {
                    private var sourceProvider: AccessibilityNodeProvider? = null
                    private var cachedProvider: AccessibilityNodeProviderCompat? = null
                    override fun dispatchPopulateAccessibilityEvent(host: View, event: AccessibilityEvent) =
                        original.dispatchPopulateAccessibilityEvent(host, event)
                    override fun onInitializeAccessibilityEvent(host: View, event: AccessibilityEvent) =
                        original.onInitializeAccessibilityEvent(host, event)
                    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfoCompat) =
                        original.onInitializeAccessibilityNodeInfo(host, info)
                    override fun onPopulateAccessibilityEvent(host: View, event: AccessibilityEvent) =
                        original.onPopulateAccessibilityEvent(host, event)
                    override fun onRequestSendAccessibilityEvent(host: ViewGroup, child: View, event: AccessibilityEvent) =
                        original.onRequestSendAccessibilityEvent(host, child, event)
                    override fun performAccessibilityAction(host: View, action: Int, args: Bundle?) =
                        original.performAccessibilityAction(host, action, args)
                    override fun sendAccessibilityEvent(host: View, eventType: Int) = original.sendAccessibilityEvent(host, eventType)
                    override fun sendAccessibilityEventUnchecked(host: View, event: AccessibilityEvent) =
                        original.sendAccessibilityEventUnchecked(host, event)
                    override fun getAccessibilityNodeProvider(host: View): AccessibilityNodeProviderCompat? {
                        val wrapped = original.getAccessibilityNodeProvider(host) ?: return null
                        val provider = wrapped.provider as? AccessibilityNodeProvider ?: return wrapped
                        if (provider === sourceProvider) return cachedProvider
                        val facade = AccessibilityNodeProviderCompat(object : AccessibilityNodeProvider() {
                            override fun createAccessibilityNodeInfo(id: Int): AccessibilityNodeInfo? = provider.createAccessibilityNodeInfo(id)
                            override fun findAccessibilityNodeInfosByText(text: String?, id: Int): MutableList<AccessibilityNodeInfo>? =
                                provider.findAccessibilityNodeInfosByText(text, id)
                            override fun findFocus(kind: Int): AccessibilityNodeInfo? = provider.findFocus(kind)
                            override fun addExtraDataToAccessibilityNodeInfo(id: Int, info: AccessibilityNodeInfo,
                                                                           key: String, args: Bundle?) {
                                provider.addExtraDataToAccessibilityNodeInfo(id, info, key, args)
                            }
                            override fun performAction(id: Int, action: Int, args: Bundle?): Boolean {
                                val node = if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS ||
                                    action == AccessibilityNodeInfo.ACTION_FOCUS) provider.createAccessibilityNodeInfo(id) else null
                                val result = provider.performAction(id, action, args)
                                if (result) when (action) {
                                    AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, AccessibilityNodeInfo.ACTION_FOCUS ->
                                        focus(node?.viewIdResourceName, node?.contentDescription?.toString())
                                    AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS, AccessibilityNodeInfo.ACTION_CLEAR_FOCUS ->
                                        focus(null, null)
                                }
                                return result
                            }
                        })
                        sourceProvider = provider
                        cachedProvider = facade
                        return facade
                    }
                }
                ViewCompat.setAccessibilityDelegate(root, observer)
                installed[root] = Binding(original, observer)
            }
        }
        if (root is ViewGroup) for (i in 0 until root.childCount) install(root.getChildAt(i))
    }
}
