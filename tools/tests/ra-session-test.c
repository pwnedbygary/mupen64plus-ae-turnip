/*
 * Host fixture for RetroAchievements sessions run through the glue.
 *
 * A fake server answers the requests rcheevos sends (sign-in, the game's
 * achievement sets, session start, unlock, leaderboard entry) with responses
 * in the format the vendored rcheevos parses (src/rapi). The game has one
 * achievement that is true when the N64 RAM byte at rcheevos address
 * ACHIEVEMENT_ADDRESS is 1, and one leaderboard that starts and submits in
 * the frame the byte at LEADERBOARD_ADDRESS becomes 1. Once the emulator runs
 * frames, the achievement must be active, and it must unlock when its byte
 * changes from 0 to 1 (rcheevos lets an achievement trigger only after a
 * frame in which it was false).
 *
 * The unlock runs twice: with the core's memory created before the game
 * loads, and with it created after the server's last answer, as when the
 * emulator starts later than the sign-in. CoreStartup creates that memory
 * with init_mem_base() (frontend.c); until then g_mem_base is NULL.
 *
 * Three more runs check what reaches Java when the server does not record a
 * result. A refused unlock, and a refused leaderboard entry in a hardcore
 * session (rcheevos submits entries only in hardcore), must arrive as a
 * server_error event that names the achievement or leaderboard, and the
 * refused unlock is not retried, so no "disconnected" follows. An unlock
 * whose first two answers are HTTP 503, which rcheevos retries, must raise
 * "disconnected" on the next frame, and "reconnected" on the frame after a
 * retry succeeds. rcheevos schedules retries on its clock, which the fixture
 * replaces, so the runs need no waiting.
 *
 * ra_glue.c and memory.c are included from the production sources, and
 * tools/test-ra-session.sh links the rcheevos sources the core builds.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "main/main.h"
#include "rc_api_runtime.h"
#include "rc_consoles.h"

void* g_mem_base;

void DebugMessage(int level, const char* message, ...)
{
    (void)level;
    (void)message;
}

/* Production memory base layout. The debug-only paths in memory.c leave
 * parameters unused when DBG is not defined; silence those host-compile
 * warnings around the include. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "device/memory/memory.c"
#pragma GCC diagnostic pop

#include "api/ra_glue.c"

/* Collect every failed check instead of aborting on the first one. */
static int g_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define ACHIEVEMENT_ID 5501
#define ACHIEVEMENT_ADDRESS 0x10
#define LEADERBOARD_ID 7701
#define LEADERBOARD_ADDRESS 0x20
#define GAME_HASH "0123456789abcdef0123456789abcdef"
#define MAX_REQUESTS 16
#define REFUSAL "refused by the test server"

static const char LOGIN_RESPONSE[] =
    "{\"Success\":true,\"User\":\"tester\",\"Token\":\"token\","
    "\"Score\":0,\"SoftcoreScore\":0,\"Messages\":0}";

static const char START_SESSION_RESPONSE[] =
    "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[],\"ServerNow\":1700000000}";

static const char AWARD_RESPONSE[] =
    "{\"Success\":true,\"Score\":5,\"SoftcoreScore\":5,\"AchievementsRemaining\":0}";

static const char REFUSAL_RESPONSE[] = "{\"Success\":false,\"Error\":\"" REFUSAL "\"}";

static struct {
    int id;
    char post_data[512];
} g_requests[MAX_REQUESTS];
static int g_num_requests;
static int g_num_answered;
static int g_award_requested;
static int g_unlock_events;
static int g_results[4];
static int g_num_results;
/* How the fake server answers: this many unlock requests get HTTP 503
 * before one succeeds, unless the unlock or leaderboard entry is refused. */
static int g_unavailable_awards;
static int g_refuse_award;
static int g_refuse_entry;
static int g_server_errors;
static char g_server_error[512];
static int g_disconnected_events;
static int g_reconnected_events;
static rc_clock_t g_now;

static void fake_server_call(int request_id, const char* url, const char* post_data,
                             const char* content_type)
{
    (void)url;
    (void)content_type;

    CHECK(g_num_requests < MAX_REQUESTS);
    if (g_num_requests < MAX_REQUESTS) {
        g_requests[g_num_requests].id = request_id;
        snprintf(g_requests[g_num_requests].post_data, sizeof(g_requests[0].post_data), "%s",
                 post_data != NULL ? post_data : "");
        ++g_num_requests;
    }
}

static void fake_event(const char* json)
{
    char id[32];

    snprintf(id, sizeof(id), "\"id\":%d,", ACHIEVEMENT_ID);
    if (strstr(json, "\"type\":\"achievement\"") != NULL && strstr(json, id) != NULL) {
        ++g_unlock_events;
    } else if (strstr(json, "\"type\":\"server_error\"") != NULL) {
        ++g_server_errors;
        snprintf(g_server_error, sizeof(g_server_error), "%s", json);
    } else if (strcmp(json, "{\"type\":\"disconnected\"}") == 0) {
        ++g_disconnected_events;
    } else if (strcmp(json, "{\"type\":\"reconnected\"}") == 0) {
        ++g_reconnected_events;
    }
}

static void fake_async_result(int result, const char* error_message)
{
    if (result != RC_OK) {
        fprintf(stderr, "request failed: %d %s\n", result, error_message);
    }
    if (g_num_results < (int)(sizeof(g_results) / sizeof(g_results[0]))) {
        g_results[g_num_results] = result;
    }
    ++g_num_results;
}

static rc_clock_t fake_clock(const rc_client_t* client)
{
    (void)client;
    return g_now;
}

/* rcheevos puts the API name first in the POST data: "r=<api>&...". */
static int is_request(const char* post_data, const char* api)
{
    const size_t length = strlen(api);

    return strncmp(post_data, "r=", 2) == 0 && strncmp(post_data + 2, api, length) == 0
        && (post_data[2 + length] == '&' || post_data[2 + length] == '\0');
}

static int has_param(const char* post_data, const char* param)
{
    const size_t length = strlen(param);
    const char* found = post_data;

    while ((found = strstr(found, param)) != NULL) {
        if ((found == post_data || found[-1] == '&')
            && (found[length] == '&' || found[length] == '\0')) {
            return 1;
        }
        ++found;
    }
    return 0;
}

static void answer_requests(void)
{
    char game_sets[2048];
    char award_param[32];

    snprintf(game_sets, sizeof(game_sets),
        "{\"Success\":true,\"GameId\":1001,\"Title\":\"Test Game\",\"ConsoleId\":%d,"
        "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
        "\"RichPresencePatch\":\"\","
        "\"Sets\":[{\"AchievementSetId\":1,\"GameId\":1001,\"Title\":\"\",\"Type\":\"core\","
        "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
        "\"Achievements\":[{\"ID\":%d,\"Title\":\"Byte\",\"Description\":\"Set the byte\","
        "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"0xH%06x=1\",\"Author\":\"tester\","
        "\"BadgeName\":\"00001\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"}],"
        "\"Leaderboards\":[{\"ID\":%d,\"Title\":\"Fastest byte\","
        "\"Description\":\"Set the other byte\","
        "\"Mem\":\"STA:0xH%06x=1::CAN:0xH%06x=2::SUB:0xH%06x=1::VAL:0xH%06x\","
        "\"Format\":\"VALUE\",\"LowerIsBetter\":false,\"Hidden\":false}]}]}",
        RC_CONSOLE_NINTENDO_64, ACHIEVEMENT_ID, RC_ACHIEVEMENT_CATEGORY_CORE, ACHIEVEMENT_ADDRESS,
        LEADERBOARD_ID, LEADERBOARD_ADDRESS, LEADERBOARD_ADDRESS, LEADERBOARD_ADDRESS,
        LEADERBOARD_ADDRESS + 1);
    snprintf(award_param, sizeof(award_param), "a=%d", ACHIEVEMENT_ID);

    /* Answering a request can send the next one, which this loop answers too. */
    while (g_num_answered < g_num_requests) {
        const int id = g_requests[g_num_answered].id;
        const char* post_data = g_requests[g_num_answered].post_data;
        const char* body;
        int status = 200;

        ++g_num_answered;
        if (is_request(post_data, "login2")) {
            body = LOGIN_RESPONSE;
        } else if (is_request(post_data, "achievementsets")) {
            body = game_sets;
        } else if (is_request(post_data, "startsession")) {
            body = START_SESSION_RESPONSE;
        } else if (is_request(post_data, "awardachievement")) {
            g_award_requested |= has_param(post_data, award_param);
            if (g_unavailable_awards > 0) {
                --g_unavailable_awards;
                body = "";
                status = 503;
            } else {
                body = g_refuse_award ? REFUSAL_RESPONSE : AWARD_RESPONSE;
            }
        } else if (is_request(post_data, "submitlbentry") && g_refuse_entry) {
            body = REFUSAL_RESPONSE;
        } else {
            fprintf(stderr, "FAIL unexpected request: %s\n", post_data);
            ++g_failures;
            body = "{\"Success\":false,\"Error\":\"unexpected request\"}";
        }
        ra_glue_dispatch_server_response(id, body, (int)strlen(body), status);
    }
}

static void* create_memory(uint8_t** rdram)
{
    void* base = init_mem_base();

    if (base == NULL) {
        fprintf(stderr, "init_mem_base failed\n");
        exit(1);
    }
    *rdram = (uint8_t*)mem_base_u32(base, MM_RDRAM_DRAM);
    (*rdram)[ACHIEVEMENT_ADDRESS] = 0;
    (*rdram)[LEADERBOARD_ADDRESS] = 0;
    (*rdram)[LEADERBOARD_ADDRESS + 1] = 42;
    g_mem_base = base;
    return base;
}

/* Signs in, loads the game and runs two frames; the achievement is then active. */
static void* start_session(int memory_before_load, int hardcore, uint8_t** rdram)
{
    const rc_client_achievement_t* achievement;
    void* base = NULL;
    int frame;

    g_num_requests = 0;
    g_num_answered = 0;
    g_award_requested = 0;
    g_unlock_events = 0;
    g_num_results = 0;
    g_unavailable_awards = 0;
    g_refuse_award = 0;
    g_refuse_entry = 0;
    g_server_errors = 0;
    g_server_error[0] = '\0';
    g_disconnected_events = 0;
    g_reconnected_events = 0;
    g_now = 1000000;
    g_mem_base = NULL;
    if (memory_before_load) {
        base = create_memory(rdram);
    }

    CHECK(ra_glue_create() == 1);
    rc_client_set_get_time_millisecs_function(s_client, fake_clock);
    ra_glue_set_java_bridge((uintptr_t)fake_server_call, (uintptr_t)fake_event,
                            (uintptr_t)fake_async_result);
    ra_glue_set_hardcore(hardcore);
    CHECK(ra_glue_login_token("tester", "token") == 1);
    answer_requests();
    CHECK(ra_glue_load_game(GAME_HASH) == 1);
    answer_requests();

    if (!memory_before_load) {
        base = create_memory(rdram);
    }

    for (frame = 0; frame < 2; ++frame) {
        ra_glue_do_frame();
        answer_requests();
    }
    CHECK(g_num_results == 2 && g_results[0] == RC_OK && g_results[1] == RC_OK);
    achievement = rc_client_get_achievement_info(s_client, ACHIEVEMENT_ID);
    CHECK(achievement != NULL);
    if (achievement != NULL) {
        CHECK(achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE);
    }
    return base;
}

static void end_session(void* base, const char* name, int failures_before)
{
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    printf("%s: %s\n", name, g_failures == failures_before ? "ok" : "FAILED");
}

static void run_unlock(int memory_before_load, const char* name)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(memory_before_load, 0, &rdram);

    rdram[ACHIEVEMENT_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_unlock_events == 1);
    CHECK(g_award_requested);
    CHECK(g_server_errors == 0);

    end_session(base, name, failures_before);
}

/* Checks the one server_error event: its API, message, result, related ID and title. */
static void check_server_error(const char* api, int related_id, const char* title)
{
    char expected[128];

    CHECK(g_server_errors == 1);
    snprintf(expected, sizeof(expected), "\"api\":\"%s\"", api);
    CHECK(strstr(g_server_error, expected) != NULL);
    CHECK(strstr(g_server_error, "\"message\":\"" REFUSAL "\"") != NULL);
    /* rcheevos reports RC_API_FAILURE for a JSON answer whose Success is false. */
    snprintf(expected, sizeof(expected), "\"result\":%d,", RC_API_FAILURE);
    CHECK(strstr(g_server_error, expected) != NULL);
    snprintf(expected, sizeof(expected), "\"related_id\":%d", related_id);
    CHECK(strstr(g_server_error, expected) != NULL);
    snprintf(expected, sizeof(expected), "\"title\":\"%s\"", title);
    CHECK(strstr(g_server_error, expected) != NULL);
}

static void run_refused_unlock(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, &rdram);

    g_refuse_award = 1;
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_unlock_events == 1);
    check_server_error("award_achievement", ACHIEVEMENT_ID, "Byte");
    /* rcheevos raises disconnect events at the end of a frame. */
    ra_glue_do_frame();
    CHECK(g_disconnected_events == 0);

    end_session(base, "refused unlock", failures_before);
}

static void run_refused_leaderboard_entry(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 1, &rdram);

    g_refuse_entry = 1;
    rdram[LEADERBOARD_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    check_server_error("submit_lboard_entry", LEADERBOARD_ID, "Fastest byte");

    end_session(base, "refused leaderboard entry in hardcore", failures_before);
}

static void run_unreachable_server(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, &rdram);
    int requests;

    /* rcheevos sends the unlock again at once after the first failure, and
     * again one second after the second failure. */
    g_unavailable_awards = 2;
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_unlock_events == 1);
    CHECK(g_unavailable_awards == 0);
    CHECK(g_disconnected_events == 0);
    ra_glue_do_frame();
    CHECK(g_disconnected_events == 1);

    requests = g_num_requests;
    g_now += 1000;
    ra_glue_do_frame();
    CHECK(g_num_requests == requests + 1);
    answer_requests();
    CHECK(g_reconnected_events == 0);
    ra_glue_do_frame();
    CHECK(g_reconnected_events == 1);
    CHECK(g_disconnected_events == 1);
    CHECK(g_server_errors == 0);

    end_session(base, "unlock retried while the server is unreachable", failures_before);
}

int main(void)
{
    run_unlock(1, "memory created before the game loads");
    run_unlock(0, "memory created after the game loads");
    run_refused_unlock();
    run_refused_leaderboard_entry();
    run_unreachable_server();

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
