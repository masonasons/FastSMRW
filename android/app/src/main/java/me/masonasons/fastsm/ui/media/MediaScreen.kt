package me.masonasons.fastsm.ui.media

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
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

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(item.title.ifBlank { "Media" }) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.Filled.Close, contentDescription = "Close")
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
