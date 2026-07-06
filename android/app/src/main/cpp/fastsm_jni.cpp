// JNI bridge: drives the shared C++ CoreSession from Kotlin. Mirrors what the
// C ABI (capi/fastsm_core.cpp) does for the Win32 app, but constructs
// CoreSession directly so we can inject an Android HTTP client. Kotlin sends
// commands as JSON strings and receives events via an EventSink callback.
//
// Networking is bridged back up to Kotlin (OkHttp) through AndroidHttpClient:
// the core's worker thread calls IHttpClient::send(), which marshals into
// HttpBridge.send() and reads the HttpResult back. SSE streaming (send_stream)
// is not wired yet — that arrives with the streaming phase.

#include <jni.h>

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/fastsm.hpp"
#include "fastsm/net/http_client.hpp"
#include "fastsm/session/core_session.hpp"

using fastsm::CoreSession;
namespace net = fastsm::net;

namespace {

JavaVM* g_vm = nullptr;

// Attach the calling (core-loop / worker) thread to the JVM if needed, returning
// a usable JNIEnv. Sets *attached so the caller detaches when it attached.
JNIEnv* env_for_thread(bool* attached) {
    *attached = false;
    JNIEnv* env = nullptr;
    const jint rc = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK)
            *attached = true;
        else
            return nullptr;
    } else if (rc != JNI_OK) {
        return nullptr;
    }
    return env;
}

std::string jstr(JNIEnv* env, jstring s) {
    if (!s)
        return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out(c ? c : "");
    if (c)
        env->ReleaseStringUTFChars(s, c);
    return out;
}

// IHttpClient implementation that calls up into Kotlin's OkHttp-backed
// HttpBridge. Global refs / IDs are resolved at construction on the JVM thread
// that called nativeCreate; send() runs on the core's worker thread (attached
// on demand).
class AndroidHttpClient : public net::IHttpClient {
public:
    AndroidHttpClient(JNIEnv* env, jobject bridge) {
        bridge_ = env->NewGlobalRef(bridge);

        jclass bridge_cls = env->GetObjectClass(bridge);
        send_ = env->GetMethodID(
            bridge_cls, "send",
            "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[B)"
            "Lme/masonasons/fastsm/core/HttpResult;");
        open_stream_ = env->GetMethodID(
            bridge_cls, "openStream",
            "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[B)J");
        read_chunk_ = env->GetMethodID(bridge_cls, "readChunk", "(J)[B");
        close_stream_ = env->GetMethodID(bridge_cls, "closeStream", "(J)V");
        cancel_all_ = env->GetMethodID(bridge_cls, "cancelAllStreams", "()V");
        env->DeleteLocalRef(bridge_cls);

        jclass str_cls = env->FindClass("java/lang/String");
        string_cls_ = static_cast<jclass>(env->NewGlobalRef(str_cls));
        env->DeleteLocalRef(str_cls);

        jclass result_cls = env->FindClass("me/masonasons/fastsm/core/HttpResult");
        f_status_ = env->GetFieldID(result_cls, "status", "I");
        f_headers_ = env->GetFieldID(result_cls, "headers", "[Ljava/lang/String;");
        f_body_ = env->GetFieldID(result_cls, "body", "[B");
        f_error_ = env->GetFieldID(result_cls, "error", "Ljava/lang/String;");
        env->DeleteLocalRef(result_cls);
    }

    ~AndroidHttpClient() override {
        bool attached = false;
        if (JNIEnv* env = env_for_thread(&attached)) {
            if (bridge_)
                env->DeleteGlobalRef(bridge_);
            if (string_cls_)
                env->DeleteGlobalRef(string_cls_);
            if (attached)
                g_vm->DetachCurrentThread();
        }
    }

    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse out;
        bool attached = false;
        JNIEnv* env = env_for_thread(&attached);
        if (!env) {
            out.error = "JNI attach failed";
            return out;
        }

        jstring jmethod = env->NewStringUTF(req.method.c_str());
        jstring jurl = env->NewStringUTF(req.url.c_str());

        // Flatten headers into [k0, v0, k1, v1, ...].
        const jsize hn = static_cast<jsize>(req.headers.size() * 2);
        jobjectArray jheaders = env->NewObjectArray(hn, string_cls_, nullptr);
        jsize idx = 0;
        for (const auto& [k, v] : req.headers) {
            jstring jk = env->NewStringUTF(k.c_str());
            jstring jv = env->NewStringUTF(v.c_str());
            env->SetObjectArrayElement(jheaders, idx++, jk);
            env->SetObjectArrayElement(jheaders, idx++, jv);
            env->DeleteLocalRef(jk);
            env->DeleteLocalRef(jv);
        }

        jbyteArray jbody = nullptr;
        if (!req.body.empty()) {
            jbody = env->NewByteArray(static_cast<jsize>(req.body.size()));
            env->SetByteArrayRegion(jbody, 0, static_cast<jsize>(req.body.size()),
                                    reinterpret_cast<const jbyte*>(req.body.data()));
        }

        jobject result = env->CallObjectMethod(bridge_, send_, jmethod, jurl, jheaders, jbody);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            out.error = "HttpBridge.send threw";
        } else if (!result) {
            out.error = "HttpBridge.send returned null";
        } else {
            out.status = env->GetIntField(result, f_status_);

            auto jerr = static_cast<jstring>(env->GetObjectField(result, f_error_));
            out.error = jstr(env, jerr);
            if (jerr)
                env->DeleteLocalRef(jerr);

            auto jresp_headers = static_cast<jobjectArray>(env->GetObjectField(result, f_headers_));
            if (jresp_headers) {
                const jsize rn = env->GetArrayLength(jresp_headers);
                for (jsize i = 0; i + 1 < rn; i += 2) {
                    auto k = static_cast<jstring>(env->GetObjectArrayElement(jresp_headers, i));
                    auto v = static_cast<jstring>(env->GetObjectArrayElement(jresp_headers, i + 1));
                    out.headers.emplace_back(jstr(env, k), jstr(env, v));
                    if (k)
                        env->DeleteLocalRef(k);
                    if (v)
                        env->DeleteLocalRef(v);
                }
                env->DeleteLocalRef(jresp_headers);
            }

            auto jresp_body = static_cast<jbyteArray>(env->GetObjectField(result, f_body_));
            if (jresp_body) {
                const jsize bn = env->GetArrayLength(jresp_body);
                out.body.resize(static_cast<size_t>(bn));
                if (bn > 0)
                    env->GetByteArrayRegion(jresp_body, 0, bn,
                                            reinterpret_cast<jbyte*>(out.body.data()));
                env->DeleteLocalRef(jresp_body);
            }
            env->DeleteLocalRef(result);
        }

        env->DeleteLocalRef(jmethod);
        env->DeleteLocalRef(jurl);
        env->DeleteLocalRef(jheaders);
        if (jbody)
            env->DeleteLocalRef(jbody);
        if (attached)
            g_vm->DetachCurrentThread();
        return out;
    }

    // Long-lived SSE stream. The read loop lives here; Kotlin just opens the
    // connection and blocks for bytes. Runs on the core's per-stream thread.
    void send_stream(const net::HttpRequest& req, const std::function<bool()>& should_continue,
                     const std::function<void(std::string_view)>& on_chunk) override {
        bool attached = false;
        JNIEnv* env = env_for_thread(&attached);
        if (!env)
            return;

        jstring jmethod = env->NewStringUTF(req.method.c_str());
        jstring jurl = env->NewStringUTF(req.url.c_str());
        const jsize hn = static_cast<jsize>(req.headers.size() * 2);
        jobjectArray jheaders = env->NewObjectArray(hn, string_cls_, nullptr);
        jsize idx = 0;
        for (const auto& [k, v] : req.headers) {
            jstring jk = env->NewStringUTF(k.c_str());
            jstring jv = env->NewStringUTF(v.c_str());
            env->SetObjectArrayElement(jheaders, idx++, jk);
            env->SetObjectArrayElement(jheaders, idx++, jv);
            env->DeleteLocalRef(jk);
            env->DeleteLocalRef(jv);
        }
        jbyteArray jbody = nullptr;
        if (!req.body.empty()) {
            jbody = env->NewByteArray(static_cast<jsize>(req.body.size()));
            env->SetByteArrayRegion(jbody, 0, static_cast<jsize>(req.body.size()),
                                    reinterpret_cast<const jbyte*>(req.body.data()));
        }

        const jlong id = env->CallLongMethod(bridge_, open_stream_, jmethod, jurl, jheaders, jbody);
        env->DeleteLocalRef(jmethod);
        env->DeleteLocalRef(jurl);
        env->DeleteLocalRef(jheaders);
        if (jbody)
            env->DeleteLocalRef(jbody);

        if (id != 0) {
            while (should_continue()) {
                auto chunk =
                    static_cast<jbyteArray>(env->CallObjectMethod(bridge_, read_chunk_, id));
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    break;
                }
                if (!chunk)
                    break; // EOF / error / cancelled
                const jsize n = env->GetArrayLength(chunk);
                if (n > 0) {
                    std::string data;
                    data.resize(static_cast<size_t>(n));
                    env->GetByteArrayRegion(chunk, 0, n, reinterpret_cast<jbyte*>(data.data()));
                    on_chunk(data);
                }
                env->DeleteLocalRef(chunk);
            }
            env->CallVoidMethod(bridge_, close_stream_, id);
        }

        if (attached)
            g_vm->DetachCurrentThread();
    }

    void cancel_streams() override {
        bool attached = false;
        JNIEnv* env = env_for_thread(&attached);
        if (!env)
            return;
        env->CallVoidMethod(bridge_, cancel_all_);
        if (attached)
            g_vm->DetachCurrentThread();
    }

private:
    jobject bridge_ = nullptr;
    jmethodID send_ = nullptr;
    jmethodID open_stream_ = nullptr;
    jmethodID read_chunk_ = nullptr;
    jmethodID close_stream_ = nullptr;
    jmethodID cancel_all_ = nullptr;
    jclass string_cls_ = nullptr;
    jfieldID f_status_ = nullptr;
    jfieldID f_headers_ = nullptr;
    jfieldID f_body_ = nullptr;
    jfieldID f_error_ = nullptr;
};

// One live core instance. Owns the session and a global ref to the Kotlin sink.
struct Bridge {
    std::unique_ptr<CoreSession> session;
    jobject sink = nullptr;       // global ref to the EventSink object
    jmethodID on_event = nullptr; // EventSink.onEvent(String)
};

std::filesystem::path to_path(const nlohmann::json& cfg, const char* key) {
    // Paths arrive as UTF-8 JSON; on POSIX a filesystem::path is UTF-8 bytes.
    return std::filesystem::path(cfg.value(key, std::string{}));
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_masonasons_fastsm_core_FastSmNative_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(fastsm::version());
}

extern "C" JNIEXPORT jlong JNICALL Java_me_masonasons_fastsm_core_FastSmNative_nativeCreate(
    JNIEnv* env, jclass, jstring config_json, jobject sink, jobject http_bridge) {
    nlohmann::json cfg;
    try {
        cfg = nlohmann::json::parse(jstr(env, config_json));
    } catch (...) {
        cfg = nlohmann::json::object();
    }

    CoreSession::Paths paths;
    paths.config_dir = to_path(cfg, "config_dir");
    paths.bundled_soundpacks = to_path(cfg, "soundpacks_dir");
    paths.bundled_keymaps = to_path(cfg, "keymaps_dir");

    auto* b = new Bridge();
    b->sink = env->NewGlobalRef(sink);
    jclass sink_cls = env->GetObjectClass(sink);
    b->on_event = env->GetMethodID(sink_cls, "onEvent", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(sink_cls);
    if (!b->on_event) {
        env->DeleteGlobalRef(b->sink);
        delete b;
        return 0;
    }

    auto emit = [b](const std::string& json) {
        bool attached = false;
        JNIEnv* e = env_for_thread(&attached);
        if (!e)
            return;
        jstring js = e->NewStringUTF(json.c_str());
        e->CallVoidMethod(b->sink, b->on_event, js);
        e->DeleteLocalRef(js);
        if (attached)
            g_vm->DetachCurrentThread();
    };

    auto http = std::make_unique<AndroidHttpClient>(env, http_bridge);
    b->session =
        std::make_unique<CoreSession>(std::move(paths), std::move(http), std::move(emit));
    return reinterpret_cast<jlong>(b);
}

extern "C" JNIEXPORT void JNICALL Java_me_masonasons_fastsm_core_FastSmNative_nativeDispatch(
    JNIEnv* env, jclass, jlong handle, jstring command_json) {
    auto* b = reinterpret_cast<Bridge*>(handle);
    if (!b || !b->session)
        return;
    b->session->dispatch(jstr(env, command_json));
}

extern "C" JNIEXPORT void JNICALL
Java_me_masonasons_fastsm_core_FastSmNative_nativeDestroy(JNIEnv* env, jclass, jlong handle) {
    auto* b = reinterpret_cast<Bridge*>(handle);
    if (!b)
        return;
    b->session.reset(); // joins the core threads; no emit fires afterward
    if (b->sink)
        env->DeleteGlobalRef(b->sink);
    delete b;
}
