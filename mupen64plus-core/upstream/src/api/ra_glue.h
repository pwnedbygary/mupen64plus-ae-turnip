/*
 * ra_glue.h - RetroAchievements (rcheevos) integration glue.
 *
 * Thin C wrapper around the vendored rcheevos v12.5.0 client
 * (mupen64plus-core/rcheevos). The Android JNA layer drives it:
 *
 *   - ra_glue_* functions are called from Java through the
 *     mupen64plus-core JNA interface.
 *   - C -> Java notifications (server call requests, events, async
 *     results) go through trampolined JNA Function pointers that Java
 *     registers with ra_glue_set_java_bridge().
 *
 * HTTP traffic is performed on the Java side; this layer only tracks
 * pending server calls and dispatches responses back into the client.
 *
 * All entry points must be annotated EXPORT: the build compiles with
 * -fvisibility=hidden (build_common/native_common.mk), so an unannotated
 * symbol stays hidden and cannot be exported by api_export.ver.
 */

#ifndef RA_GLUE_H
#define RA_GLUE_H

#include <stdint.h>

#include "m64p_types.h"

/* Lifecycle. Returns 1 on success, 0 on failure (see ra_glue_get_last_error).
 * ra_glue_shutdown() must be called only after emulation has stopped; it drains
 * in-flight server-call dispatches before destroying the client. */
EXPORT int ra_glue_create(void);
EXPORT void ra_glue_shutdown(void);

/* Register JNA trampolined Function pointers:
 *   server_call:  void(int request_id, const char* url, const char* post_data, const char* content_type)
 *   event:        void(const char* json)
 *   async_result: void(int result, const char* error_message)
 */
EXPORT void ra_glue_set_java_bridge(uintptr_t server_call, uintptr_t event, uintptr_t async_result);

/* Emulation loop hooks (called from the emu thread). */
EXPORT void ra_glue_do_frame(void);
EXPORT void ra_glue_idle(void);
EXPORT void ra_glue_reset(void);

/* Hardcore mode. Set before ra_glue_load_game() (or at session start): rcheevos
 * defers a mid-session change until the next reset (main_reset -> ra_glue_reset),
 * so enabling it later stalls achievement processing until a reset occurs. */
EXPORT void ra_glue_set_hardcore(int enabled);
EXPORT int ra_glue_get_hardcore(void);

/* Session management. Returns 1 if the async operation was started. */
EXPORT int ra_glue_login_password(const char* username, const char* password);
EXPORT int ra_glue_login_token(const char* username, const char* token);
EXPORT int ra_glue_logout(void);
EXPORT int ra_glue_load_game(const char* md5);

EXPORT int ra_glue_is_logged_in(void);
EXPORT int ra_glue_is_game_loaded(void);
EXPORT int ra_glue_get_load_game_state(void);

/* State getters (return 1 if filled). */
EXPORT int ra_glue_get_user_name(char* buffer, int buffer_size);

/* Session token returned by a successful password login, for token-only re-login. */
EXPORT int ra_glue_get_user_token(char* buffer, int buffer_size);

EXPORT int ra_glue_get_game_name(char* buffer, int buffer_size);
EXPORT int ra_glue_get_summary_json(char* buffer, int buffer_size);
EXPORT int ra_glue_get_achievements_json(char* buffer, int buffer_size);

/* Achieved-progress persistence (per game, driven by Java).
 * serialize: returns bytes written (> 0), or -1 on failure / buffer too small.
 * deserialize: returns 1 on success, 0 on failure. */
EXPORT int ra_glue_serialize_progress(uint8_t* buffer, int buffer_size);
EXPORT int ra_glue_deserialize_progress(const uint8_t* buffer, int buffer_size);

/* Called from the Java HTTP worker when a response arrives. */
EXPORT void ra_glue_dispatch_server_response(int request_id, const char* body, int body_length, int http_status);

EXPORT const char* ra_glue_get_last_error(void);

#endif /* RA_GLUE_H */
