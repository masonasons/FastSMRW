plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "me.masonasons.fastsm"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        // Distinct from the shipping app (me.masonasons.fastsm) so this in-progress
        // build installs alongside it during development instead of clobbering it.
        // (The Kotlin/JNI package stays me.masonasons.fastsm.*, so native symbol
        // names are unaffected.) Reclaim the real id when it's ready to ship.
        applicationId = "me.masonasons.fastsmrw"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "0.2.1"

        // Build the shared core (fastsm_core + JNI bridge) for these ABIs:
        // arm64 for real devices, x86_64 for the emulator. armeabi-v7a can be
        // added later. Keeping the set small keeps native build times down.
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Release signing is driven by gradle properties / env vars so the keystore
    // and its password are never committed. If they're absent (e.g. a local build
    // or a fork without secrets), the release build falls back to debug signing —
    // fine for sideload testing, rejected by Play.
    signingConfigs {
        create("release") {
            val storeFilePath = (project.findProperty("FASTSMRW_RELEASE_STORE_FILE") as String?)
                ?: System.getenv("FASTSMRW_RELEASE_STORE_FILE")
            val storePwd = (project.findProperty("FASTSMRW_RELEASE_STORE_PASSWORD") as String?)
                ?: System.getenv("FASTSMRW_RELEASE_STORE_PASSWORD")
            val keyAliasValue = (project.findProperty("FASTSMRW_RELEASE_KEY_ALIAS") as String?)
                ?: System.getenv("FASTSMRW_RELEASE_KEY_ALIAS")
            val keyPwd = (project.findProperty("FASTSMRW_RELEASE_KEY_PASSWORD") as String?)
                ?: System.getenv("FASTSMRW_RELEASE_KEY_PASSWORD")
            if (storeFilePath != null && storePwd != null && keyAliasValue != null && keyPwd != null) {
                storeFile = file(storeFilePath)
                storePassword = storePwd
                keyAlias = keyAliasValue
                keyPassword = keyPwd
            }
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
        release {
            // Minify stays off for now: the JNI layer reads HttpResult fields by
            // name and OkHttp/Media3/Coil need consumer rules — R8 can be enabled
            // later with keep rules. Correctness over size while stabilizing.
            isMinifyEnabled = false
            // Sign with the release key when configured; otherwise fall back to the
            // debug key so CI still produces an installable (sideloadable) APK.
            val releaseSigning = signingConfigs.getByName("release")
            signingConfig = if (releaseSigning.storeFile != null) releaseSigning
            else signingConfigs.getByName("debug")
        }
    }

    // Hand out FastSMRW.apk (release) / FastSMRW-debug.apk rather than app-*.apk.
    applicationVariants.all {
        val suffix = if (buildType.name == "release") "" else "-debug"
        outputs.all {
            (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl).outputFileName =
                "FastSMRW$suffix.apk"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.browser)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    debugImplementation(libs.androidx.compose.ui.tooling)

    // Backs the core's IHttpClient over JNI (Phase 0b).
    implementation(libs.okhttp)

    // In-app media viewer: Coil for images, Media3/ExoPlayer for video/audio.
    implementation(libs.coil.compose)
    implementation(libs.androidx.media3.exoplayer)
    implementation(libs.androidx.media3.ui)
}
