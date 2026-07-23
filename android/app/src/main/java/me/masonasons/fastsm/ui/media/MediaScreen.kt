package me.masonasons.fastsm.ui.media

import android.app.DownloadManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import coil.compose.AsyncImage
import me.masonasons.fastsm.ui.MediaItemUi

/**
 * In-app media viewer. Images render with Coil; video/gifv/audio play through an
 * ExoPlayer with standard (accessible) transport controls. Driven by the core's
 * media_open event, which carries the kind.
 */
@OptIn(ExperimentalMaterial3Api::class, UnstableApi::class)
@Composable
fun MediaScreen(item: MediaItemUi, onClose: () -> Unit) {
    BackHandler(enabled = true) { onClose() }
    val context = LocalContext.current

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(item.title.ifBlank { "Media" }) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.Filled.Close, contentDescription = "Close")
                    }
                },
                actions = {
                    var menuOpen by remember { mutableStateOf(false) }
                    IconButton(onClick = { menuOpen = true }) {
                        Icon(Icons.Filled.MoreVert, contentDescription = "More")
                    }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(
                            text = { Text("Save") },
                            onClick = { menuOpen = false; saveMedia(context, item) },
                        )
                        DropdownMenuItem(
                            text = { Text("Share") },
                            onClick = { menuOpen = false; shareMedia(context, item) },
                        )
                    }
                },
            )
        },
    ) { pad ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad),
            contentAlignment = Alignment.Center,
        ) {
            if (item.kind == "image") {
                AsyncImage(
                    model = item.url,
                    contentDescription = item.title.ifBlank { "Image" },
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                val context = LocalContext.current
                val player = remember(item.url) {
                    ExoPlayer.Builder(context).build().apply {
                        setMediaItem(MediaItem.fromUri(item.url))
                        prepare()
                        playWhenReady = true
                    }
                }
                DisposableEffect(item.url) {
                    onDispose { player.release() }
                }
                AndroidView(
                    factory = { ctx -> PlayerView(ctx).apply { this.player = player } },
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }
    }
}

/** Save the media to the public Downloads folder (a notification reports done). */
private fun saveMedia(context: Context, item: MediaItemUi) {
    runCatching {
        val uri = Uri.parse(item.url)
        val name = uri.lastPathSegment?.takeIf { it.isNotBlank() } ?: "media"
        val request = DownloadManager.Request(uri)
            .setTitle(item.title.ifBlank { name })
            .setNotificationVisibility(
                DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED,
            )
            .setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name)
        (context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager).enqueue(request)
    }
}

/** Share the media (its link) to another app. */
private fun shareMedia(context: Context, item: MediaItemUi) {
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, item.url)
    }
    context.startActivity(Intent.createChooser(send, "Share"))
}
