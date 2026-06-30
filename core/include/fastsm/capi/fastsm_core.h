#ifndef FASTSM_CORE_H
#define FASTSM_CORE_H

/* FastSM core C ABI. The whole engine is driven through this flat interface so
 * any language (Swift, Kotlin/Java, C#, Python, ...) can bind it: submit a
 * command as JSON, receive events as JSON. No C++ types cross this boundary. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastsm_core fastsm_core;

/* Event sink. Called by the core (on its own thread) with a UTF-8 JSON event;
 * `event_json` is valid only for the duration of the call — copy it. The front
 * end is responsible for marshalling onto its UI thread. */
typedef void (*fastsm_event_fn)(void* user, const char* event_json, size_t len);

/* Create a core instance. `config_json` carries paths/options, e.g.
 * {"config_dir":"...","soundpacks_dir":"...","user_agent":"FastSMRW/0.0.1"}.
 * Returns NULL on failure. */
fastsm_core* fastsm_core_create(const char* config_json);

/* Register (or replace) the event sink. May be called before or after create-time
 * work; events emitted while no sink is set are dropped. */
void fastsm_core_set_event_sink(fastsm_core* core, fastsm_event_fn cb, void* user);

/* Submit a command (UTF-8 JSON, `len` bytes). Non-blocking; results arrive as
 * events. Unknown/malformed commands are ignored. */
void fastsm_core_dispatch(fastsm_core* core, const char* command_json, size_t len);

/* Destroy the instance, stopping its threads. The sink is not called afterward. */
void fastsm_core_destroy(fastsm_core* core);

/* Engine version string (static storage). */
const char* fastsm_core_version(void);

#ifdef __cplusplus
}
#endif

#endif /* FASTSM_CORE_H */
