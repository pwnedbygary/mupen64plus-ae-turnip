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

#include <stddef.h>
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

/* Emulation hooks: do_frame and idle run on the emu thread; reset runs on the
 * thread that resets the core (the app's main thread), waiting while either runs. */
EXPORT void ra_glue_do_frame(void);
EXPORT void ra_glue_idle(void);
EXPORT void ra_glue_reset(void);

/* Save-state hooks: savestates.c calls them on the emu thread when the core's
 * save or load function returns success (M64P and PJ64 zip saves return it
 * even when writing the file fails), with the console RAM at that moment, so
 * that each state keeps rcheevos's progress (see ra_glue.c). Java never calls
 * them, so they need no EXPORT. */
void ra_glue_state_saved(const char* state_path, const uint32_t* rdram, size_t rdram_size);
void ra_glue_state_loaded(const char* state_path, const uint32_t* rdram, size_t rdram_size);

/* Hardcore mode. Enable it before ra_glue_load_game() (or at session start):
 * once a game is loaded, rcheevos stops processing achievements after it is
 * enabled until the next reset (main_reset -> ra_glue_reset). Disabling takes
 * effect at once, before or after the game has loaded. */
EXPORT void ra_glue_set_hardcore(int enabled);
EXPORT int ra_glue_get_hardcore(void);

/* The cheat codes that the ROM database lists for the game (its Cheat keys),
 * which main_run() adds when emulation starts. The app turns them off, before
 * it starts emulation, for a session that runs in hardcore; the requested mode
 * alone does not, as it stays on after a sign-in or game load has failed.
 * ra_glue_shutdown() turns them on again. Java never calls
 * ra_glue_get_database_codes(), so it needs no EXPORT. */
EXPORT void ra_glue_set_database_codes(int enabled);
int ra_glue_get_database_codes(void);

/* Returns 1 if the ROM database lists such codes for the open ROM, which the app
 * asks between opening the ROM and starting emulation. It is defined in rom.c,
 * beside ROM_PARAMS, so the host tests that build ra_glue.c without rom.c need
 * nothing more. */
EXPORT int ra_glue_has_database_codes(void);

/* Keeps the unlocks and leaderboard entries the server has not answered in the
 * file at path, and sends those of the signed-in player again after each
 * sign-in (see ra_glue.c). NULL or "" keeps none. Set it before signing in,
 * and from one process only. Returns 0 when out of memory. */
EXPORT int ra_glue_set_unsent_path(const char* path);

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
/* Waits for a frame, idle work, a reset or a state's progress restore that is
 * running, so it must not be called from a callback the glue makes during one. */
EXPORT int ra_glue_get_achievements_json(char* buffer, int buffer_size);

/* The loaded game's progress as one blob. The app does not use these: it keeps
 * progress with each save state (ra_glue_state_saved).
 * serialize: returns bytes written (> 0), or -1 on failure / buffer too small.
 * deserialize: returns 1 on success, 0 on failure. Unlike ra_glue_state_loaded
 * it does not take the lock that frames, idle work, resets and the achievement
 * list take, so it must not run beside them on another thread. */
EXPORT int ra_glue_serialize_progress(uint8_t* buffer, int buffer_size);
EXPORT int ra_glue_deserialize_progress(const uint8_t* buffer, int buffer_size);

/* Called from the Java HTTP worker when a response arrives. */
EXPORT void ra_glue_dispatch_server_response(int request_id, const char* body, int body_length, int http_status);

EXPORT const char* ra_glue_get_last_error(void);

#endif /* RA_GLUE_H */
