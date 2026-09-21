/*
 * ra_glue.c - RetroAchievements (rcheevos) integration glue.
 *
 * Wraps the vendored rcheevos v12.5.0 client. The N64 achievement
 * memory references use full console addresses (0x80000000 + offset);
 * mupen's internal memory map places RDRAM at offset 0, so reads are
 * resolved with mem_base_u32(g_mem_base, offset), which is correct in
 * both full and compressed memory base modes.
 *
 * All client API entry points used here are internally mutex-protected
 * by rcheevos, so it is safe to call them from the emu thread (do_frame)
 * and from Java worker threads (idle, dispatch, getters).
 */

#include "ra_glue.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main/main.h"
#include "device/memory/memory.h"
#include "rc_client.h"
#include "rc_api_request.h"

#define RA_GLUE_MAX_PENDING 16
#define RA_GLUE_N64_RAM_BASE 0x80000000u

/* C -> Java trampolined functions (JNA Function pointers). */
typedef void (*ra_java_server_call_t)(int request_id, const char* url, const char* post_data, const char* content_type);
typedef void (*ra_java_event_t)(const char* json);
typedef void (*ra_java_async_result_t)(int result, const char* error_message);

/*
 * Threading: the Java bridge pointers and s_client are installed by
 * ra_glue_create() on the frontend init thread before any other entry point
 * is used, and are cleared by ra_glue_shutdown() only after emulation has
 * stopped (see ra_glue.h). Pending-request and dispatch state is guarded by
 * s_pending_lock; s_hardcore and s_last_error are best-effort diagnostics.
 */
static rc_client_t* s_client = NULL;
static pthread_mutex_t s_pending_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_next_request_id = 1;
static int s_hardcore = 0;
static char s_last_error[512];

static ra_java_server_call_t s_java_server_call = NULL;
static ra_java_event_t s_java_event = NULL;
static ra_java_async_result_t s_java_async_result = NULL;

typedef struct {
  int in_use;
  int id;
  rc_client_server_callback_t callback;
  void* callback_data;
} ra_pending_t;

static ra_pending_t s_pending[RA_GLUE_MAX_PENDING];

/* Count of dispatch callbacks currently running against s_client, so
 * ra_glue_shutdown() can wait for them before destroying the client. */
static int s_dispatch_in_flight = 0;
static pthread_cond_t s_dispatch_cond = PTHREAD_COND_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void ra_glue_set_error(const char* message)
{
  if (message == NULL)
    message = "unknown error";
  snprintf(s_last_error, sizeof(s_last_error), "%s", message);
}

const char* ra_glue_get_last_error(void)
{
  return s_last_error;
}

/*
 * rcheevos memory read callback.
 * rcheevos passes full N64 memory-mapped addresses (0x80000000+).
 * mupen maps RDRAM at MM address 0, so the buffer offset is
 * address - 0x80000000; mem_base_u32() resolves that in both
 * full and compressed memory base modes.
 */
static uint32_t ra_glue_read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* client)
{
  (void)client;

  if (g_mem_base == NULL || num_bytes == 0)
    return 0;

  if (address < RA_GLUE_N64_RAM_BASE)
    return 0;

  uint32_t offset = address - RA_GLUE_N64_RAM_BASE;
  if (offset >= RDRAM_16MB_SIZE)
    return 0;

  if (num_bytes > RDRAM_16MB_SIZE - offset)
    num_bytes = RDRAM_16MB_SIZE - offset;

  uint32_t* mem = mem_base_u32(g_mem_base, offset);
  if (mem == NULL)
    return 0;

  memcpy(buffer, (uint8_t*)mem, num_bytes);
  return num_bytes;
}

/* Returns an allocated JSON-escaped, unquoted string, or NULL on OOM. */
static char* ra_json_escape(const char* src)
{
  if (src == NULL)
    src = "";

  size_t len = strlen(src);
  char* out = malloc(len * 6 + 1);
  if (out == NULL)
    return NULL;

  size_t n = 0;
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = (char)c;
    }
    else if (c < 0x20) {
      n += (size_t)sprintf(out + n, "\\u%04x", c);
    }
    else {
      out[n++] = (char)c;
    }
  }
  out[n] = '\0';
  return out;
}

/* ------------------------------------------------------------------ */
/* Java bridge                                                         */
/* ------------------------------------------------------------------ */

void ra_glue_set_java_bridge(uintptr_t server_call, uintptr_t event, uintptr_t async_result)
{
  union { void* p; ra_java_server_call_t fn; } sc = { (void*)(uintptr_t)server_call };
  union { void* p; ra_java_event_t fn; } ev = { (void*)(uintptr_t)event };
  union { void* p; ra_java_async_result_t fn; } ar = { (void*)(uintptr_t)async_result };
  s_java_server_call = sc.fn;
  s_java_event = ev.fn;
  s_java_async_result = ar.fn;
}

/* ------------------------------------------------------------------ */
/* rcheevos callbacks                                                  */
/* ------------------------------------------------------------------ */

static void ra_glue_async_cb(int result, const char* error_message, rc_client_t* client, void* userdata)
{
  (void)client;
  (void)userdata;

  if (s_java_async_result == NULL)
    return;

  char message[512];
  snprintf(message, sizeof(message), "%s", error_message ? error_message : "");
  s_java_async_result(result, message);
}

static void ra_glue_server_call(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                void* callback_data, rc_client_t* client)
{
  (void)client;

  int id = 0;
  pthread_mutex_lock(&s_pending_lock);
  for (int i = 0; i < RA_GLUE_MAX_PENDING; ++i) {
    if (!s_pending[i].in_use) {
      id = ++s_next_request_id;
      s_pending[i].in_use = 1;
      s_pending[i].id = id;
      s_pending[i].callback = callback;
      s_pending[i].callback_data = callback_data;
      break;
    }
  }
  pthread_mutex_unlock(&s_pending_lock);

  if (id == 0) {
    /* No free pending slot: fail the request immediately. */
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = 503;
    response.body = "ra_glue: no pending request slot";
    response.body_length = (int)strlen(response.body);
    if (callback != NULL)
      callback(&response, callback_data);
    return;
  }

  if (s_java_server_call != NULL)
    s_java_server_call(id, request->url, request->post_data, request->content_type);
}

void ra_glue_dispatch_server_response(int request_id, const char* body, int body_length, int http_status)
{
  rc_client_server_callback_t callback = NULL;
  void* callback_data = NULL;

  pthread_mutex_lock(&s_pending_lock);
  for (int i = 0; i < RA_GLUE_MAX_PENDING; ++i) {
    if (s_pending[i].in_use && s_pending[i].id == request_id) {
      s_pending[i].in_use = 0;
      callback = s_pending[i].callback;
      callback_data = s_pending[i].callback_data;
      break;
    }
  }
  if (callback != NULL)
    ++s_dispatch_in_flight;
  pthread_mutex_unlock(&s_pending_lock);

  if (callback == NULL)
    return;

  rc_api_server_response_t response;
  memset(&response, 0, sizeof(response));
  response.body = body;
  response.body_length = body_length;
  response.http_status_code = http_status;
  callback(&response, callback_data);

  /* Tell ra_glue_shutdown() the callback no longer touches the client. */
  pthread_mutex_lock(&s_pending_lock);
  --s_dispatch_in_flight;
  if (s_dispatch_in_flight == 0)
    pthread_cond_broadcast(&s_dispatch_cond);
  pthread_mutex_unlock(&s_pending_lock);
}

static char* ra_build_event_json(const rc_client_event_t* event)
{
  char* json = NULL;

  switch (event->type) {
  case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    if (event->achievement != NULL) {
      char* title = ra_json_escape(event->achievement->title);
      char* description = ra_json_escape(event->achievement->description);
      if (title != NULL && description != NULL) {
        asprintf(&json, "{\"type\":\"achievement\",\"id\":%u,\"title\":%s,\"description\":%s,\"points\":%u}",
                 event->achievement->id, title, description, event->achievement->points);
        free(title);
        free(description);
      }
    }
    break;

  case RC_CLIENT_EVENT_SERVER_ERROR:
    if (event->server_error != NULL) {
      char* api = ra_json_escape(event->server_error->api);
      char* message = ra_json_escape(event->server_error->error_message);
      if (api != NULL && message != NULL) {
        asprintf(&json, "{\"type\":\"server_error\",\"api\":%s,\"message\":%s,\"result\":%d,\"related_id\":%u}",
                 api, message, event->server_error->result, event->server_error->related_id);
        free(api);
        free(message);
      }
    }
    break;

  case RC_CLIENT_EVENT_RESET:
    asprintf(&json, "{\"type\":\"reset\"}");
    break;

  case RC_CLIENT_EVENT_GAME_COMPLETED:
    asprintf(&json, "{\"type\":\"game_completed\"}");
    break;

  case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
  case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
  case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
    if (event->leaderboard != NULL) {
      const char* name =
        (event->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED) ? "leaderboard_started" :
        (event->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED) ? "leaderboard_failed" : "leaderboard_submitted";
      char* title = ra_json_escape(event->leaderboard->title);
      if (title != NULL) {
        asprintf(&json, "{\"type\":\"%s\",\"id\":%u,\"title\":%s}", name, event->leaderboard->id, title);
        free(title);
      }
    }
    break;

  default:
    break;
  }

  return json;
}

static void ra_glue_event_handler(const rc_client_event_t* event, rc_client_t* client)
{
  (void)client;
  if (event == NULL || s_java_event == NULL)
    return;

  char* json = ra_build_event_json(event);
  if (json != NULL) {
    s_java_event(json);
    free(json);
  }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int ra_glue_create(void)
{
  if (s_client != NULL)
    return 1;

  rc_client_t* client = rc_client_create(ra_glue_read_memory, ra_glue_server_call);
  if (client == NULL) {
    ra_glue_set_error("rc_client_create failed");
    return 0;
  }

  s_client = client;
  rc_client_set_event_handler(s_client, ra_glue_event_handler);
  /* Keep the rcheevos default host (https://retroachievements.org). Passing a bare
   * hostname makes rc_api_update_host() prepend "http://", which is blocked by the
   * app's cleartext policy and is not a valid API endpoint. */
  return 1;
}

void ra_glue_shutdown(void)
{
  rc_client_t* client;

  if (s_client == NULL)
    return;

  /* Drop outstanding requests and wait for any callback already running
   * against the client before destroying it, so a concurrent
   * ra_glue_dispatch_server_response() cannot use freed client data. */
  pthread_mutex_lock(&s_pending_lock);
  memset(s_pending, 0, sizeof(s_pending));
  while (s_dispatch_in_flight > 0)
    pthread_cond_wait(&s_dispatch_cond, &s_pending_lock);
  client = s_client;
  s_client = NULL;
  pthread_mutex_unlock(&s_pending_lock);

  rc_client_destroy(client);
  s_hardcore = 0;
  s_last_error[0] = '\0';
}

void ra_glue_do_frame(void)
{
  if (s_client != NULL)
    rc_client_do_frame(s_client);
}

void ra_glue_idle(void)
{
  if (s_client != NULL)
    rc_client_idle(s_client);
}

void ra_glue_reset(void)
{
  if (s_client != NULL)
    rc_client_reset(s_client);
}

void ra_glue_set_hardcore(int enabled)
{
  s_hardcore = enabled ? 1 : 0;
  if (s_client != NULL)
    rc_client_set_hardcore_enabled(s_client, s_hardcore);
}

int ra_glue_get_hardcore(void)
{
  return s_hardcore;
}

int ra_glue_login_password(const char* username, const char* password)
{
  if (s_client == NULL || username == NULL || password == NULL)
    return 0;

  rc_client_async_handle_t* handle =
    rc_client_begin_login_with_password(s_client, username, password, ra_glue_async_cb, NULL);
  return handle != NULL ? 1 : 0;
}

int ra_glue_login_token(const char* username, const char* token)
{
  if (s_client == NULL || username == NULL || token == NULL)
    return 0;

  rc_client_async_handle_t* handle =
    rc_client_begin_login_with_token(s_client, username, token, ra_glue_async_cb, NULL);
  return handle != NULL ? 1 : 0;
}

int ra_glue_logout(void)
{
  if (s_client == NULL)
    return 0;

  rc_client_logout(s_client);
  return 1;
}

int ra_glue_load_game(const char* md5)
{
  if (s_client == NULL || md5 == NULL || md5[0] == '\0')
    return 0;

  if (rc_client_is_game_loaded(s_client))
    rc_client_unload_game(s_client);

  rc_client_async_handle_t* handle =
    rc_client_begin_load_game(s_client, md5, ra_glue_async_cb, NULL);
  return handle != NULL ? 1 : 0;
}

int ra_glue_is_logged_in(void)
{
  return (s_client != NULL && rc_client_get_user_info(s_client) != NULL) ? 1 : 0;
}

int ra_glue_is_game_loaded(void)
{
  return (s_client != NULL && rc_client_is_game_loaded(s_client)) ? 1 : 0;
}

int ra_glue_get_load_game_state(void)
{
  if (s_client == NULL)
    return RC_CLIENT_LOAD_GAME_STATE_NONE;
  return rc_client_get_load_game_state(s_client);
}

int ra_glue_get_user_name(char* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return 0;

  const rc_client_user_t* user = rc_client_get_user_info(s_client);
  if (user == NULL || user->username == NULL) {
    buffer[0] = '\0';
    return 0;
  }

  snprintf(buffer, (size_t)buffer_size, "%s", user->username);
  return 1;
}

int ra_glue_get_user_token(char* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return 0;

  const rc_client_user_t* user = rc_client_get_user_info(s_client);
  if (user == NULL || user->token == NULL) {
    buffer[0] = '\0';
    return 0;
  }

  snprintf(buffer, (size_t)buffer_size, "%s", user->token);
  return 1;
}

int ra_glue_get_game_name(char* buffer, int buffer_size)
{
  if (buffer == NULL || buffer_size <= 0) {
    buffer[0] = '\0';
    return 0;
  }

  const rc_client_game_t* game = (s_client != NULL) ? rc_client_get_game_info(s_client) : NULL;
  if (game == NULL || game->title == NULL) {
    buffer[0] = '\0';
    return 0;
  }

  snprintf(buffer, (size_t)buffer_size, "%s", game->title);
  return 1;
}

int ra_glue_get_summary_json(char* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return 0;

  rc_client_user_game_summary_t summary;
  memset(&summary, 0, sizeof(summary));
  rc_client_get_user_game_summary(s_client, &summary);

  char* user_name = ra_json_escape("");
  char* game_name = ra_json_escape("");
  const rc_client_user_t* user = rc_client_get_user_info(s_client);
  if (user != NULL && user->username != NULL)
    free(user_name), user_name = ra_json_escape(user->username);
  const rc_client_game_t* game = rc_client_get_game_info(s_client);
  if (game != NULL && game->title != NULL)
    free(game_name), game_name = ra_json_escape(game->title);

  const char* empty = "\"\"";
  char* json = NULL;
  asprintf(&json,
           "{\"user\":%s,\"game\":%s,"
           "\"total_achievements\":%u,\"unlocked_achievements\":%u,"
           "\"total_points\":%u,\"unlocked_points\":%u}",
           user_name != NULL ? user_name : empty,
           game_name != NULL ? game_name : empty,
           summary.num_core_achievements, summary.num_unlocked_achievements,
           summary.points_core, summary.points_unlocked);
  free(user_name);
  free(game_name);

  if (json == NULL)
    return 0;

  if ((int)strlen(json) >= buffer_size) {
    free(json);
    return 0;
  }

  memcpy(buffer, json, strlen(json) + 1);
  free(json);
  return 1;
}

int ra_glue_get_achievements_json(char* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return 0;

  rc_client_achievement_list_t* list =
    rc_client_create_achievement_list(s_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                     RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (list == NULL)
    return 0;

  size_t capacity = 4096;
  char* json = malloc(capacity);
  if (json == NULL) {
    rc_client_destroy_achievement_list(list);
    return 0;
  }

  size_t n = (size_t)sprintf(json, "{\"achievements\":[");

  for (uint32_t b = 0; b < list->num_buckets; ++b) {
    const rc_client_achievement_bucket_t* bucket = &list->buckets[b];
    for (uint32_t i = 0; i < bucket->num_achievements; ++i) {
      const rc_client_achievement_t* achievement = bucket->achievements[i];
      if (achievement == NULL)
        continue;

      char* title = ra_json_escape(achievement->title);
      char* description = ra_json_escape(achievement->description);
      if (title == NULL || description == NULL) {
        free(title);
        free(description);
        continue;
      }

      const char* comma = (n > 17) ? "," : ""; /* prefix "{\"achievements\":[" is 17 chars */
      int entry_len = snprintf(NULL, 0,
                               "%s{\"id\":%u,\"title\":%s,\"description\":%s,\"points\":%u,\"unlocked\":%d}",
                               comma,
                               achievement->id, title, description,
                               achievement->points, achievement->unlocked ? 1 : 0);

      if (n + (size_t)entry_len + 2 >= capacity) {
        capacity = (capacity + (size_t)entry_len + 256) * 2;
        char* grown = realloc(json, capacity);
        if (grown == NULL) {
          free(title);
          free(description);
          free(json);
          rc_client_destroy_achievement_list(list);
          return 0;
        }
        json = grown;
      }

      n += (size_t)snprintf(json + n, capacity - n,
                            "%s{\"id\":%u,\"title\":%s,\"description\":%s,\"points\":%u,\"unlocked\":%d}",
                            comma,
                            achievement->id, title, description,
                            achievement->points, achievement->unlocked ? 1 : 0);

      free(title);
      free(description);
    }
  }

  n += (size_t)sprintf(json + n, "]}");

  if ((int)n + 1 > buffer_size) {
    free(json);
    rc_client_destroy_achievement_list(list);
    return 0;
  }

  memcpy(buffer, json, n + 1);
  free(json);
  rc_client_destroy_achievement_list(list);
  return 1;
}

int ra_glue_serialize_progress(uint8_t* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return -1;

  /* rc_client_serialize_progress_sized() returns RC_OK/error, not a length;
   * report the blob size from rc_client_progress_size() so Java can persist
   * the right number of bytes. */
  size_t size = rc_client_progress_size(s_client);
  if (size == 0 || size > (size_t)buffer_size)
    return -1;

  if (rc_client_serialize_progress_sized(s_client, buffer, (size_t)buffer_size) != RC_OK)
    return -1;

  return (int)size;
}

int ra_glue_deserialize_progress(const uint8_t* buffer, int buffer_size)
{
  if (s_client == NULL || buffer == NULL || buffer_size <= 0)
    return 0;

  /* Normalized for the JNA layer: 1 on success, 0 on failure (the underlying
   * rc_client call uses RC_OK == 0 for success). */
  return rc_client_deserialize_progress_sized(s_client, (uint8_t*)buffer,
                                              (size_t)buffer_size) == RC_OK ? 1 : 0;
}
