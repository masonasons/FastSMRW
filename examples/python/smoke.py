#!/usr/bin/env python3
"""Drive the FastSM core from Python via its C ABI.

This is a proof that fastsm_core is genuinely language-agnostic: the same engine
that backs the Win32 app is here loaded as a DLL and driven from Python with
nothing but the flat C boundary (commands and events as JSON). The identical ABI
binds from Swift (clang module), Kotlin/Java (JNI / Panama FFM), C#, etc.

Build the DLL first:  build.bat dll
Then run:             python examples/python/smoke.py
"""

import ctypes
import json
import os
import tempfile
import threading
import time

DLL = os.path.join("build", "release", "fastsm_core.dll")


def main() -> None:
    lib = ctypes.CDLL(os.path.abspath(DLL))
    lib.fastsm_core_create.restype = ctypes.c_void_p
    lib.fastsm_core_create.argtypes = [ctypes.c_char_p]
    lib.fastsm_core_version.restype = ctypes.c_char_p
    lib.fastsm_core_dispatch.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.fastsm_core_destroy.argtypes = [ctypes.c_void_p]

    EventCb = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t)
    lib.fastsm_core_set_event_sink.argtypes = [ctypes.c_void_p, EventCb, ctypes.c_void_p]

    events, lock = [], threading.Lock()

    def on_event(_user, data, length):
        text = ctypes.string_at(data, length).decode("utf-8")
        with lock:
            events.append(json.loads(text))
        print("  EVENT:", text[:160])

    sink = EventCb(on_event)  # keep a reference alive for the core's lifetime

    print("core version:", lib.fastsm_core_version().decode())
    config = {"config_dir": tempfile.mkdtemp(prefix="fastsm_py_")}
    core = lib.fastsm_core_create(json.dumps(config).encode())
    assert core, "fastsm_core_create failed"
    lib.fastsm_core_set_event_sink(core, sink, None)

    def send(command):
        payload = json.dumps(command).encode()
        lib.fastsm_core_dispatch(core, payload, len(payload))

    send({"cmd": "get_settings"})
    send({"cmd": "start"})
    time.sleep(1.5)  # events arrive on the core's own thread

    names = [e.get("event") for e in events]
    lib.fastsm_core_destroy(core)
    print("got events:", names)
    assert "settings" in names and "timelines_changed" in names, "missing expected events"
    print("OK: Python drove the C++ core through the C ABI.")


if __name__ == "__main__":
    main()
