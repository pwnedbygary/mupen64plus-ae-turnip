/*
 * Host fixture for RetroAchievements sessions run through the glue.
 *
 * A fake server answers the requests rcheevos sends (sign-in, the game's
 * achievement sets, session start, unlock, leaderboard entry, ping) with
 * responses in the format the vendored rcheevos parses (src/rapi). The
 * game's first achievement is true when the N64 RAM byte at rcheevos address
 * ACHIEVEMENT_ADDRESS is 1, and its first leaderboard starts and submits in
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
 * The next run checks the indicators. The game's second achievement has a
 * Trigger condition that stays false, so it is primed, and its challenge
 * indicator shown, while its other condition (the byte at CHALLENGE_ADDRESS
 * is 1) holds. Its third achievement measures the byte at MEASURED_ADDRESS
 * against MEASURED_TARGET: rcheevos shows the progress when that value
 * changes and hides it two seconds after the last change. The events and
 * the achievement list must carry the challenge state and the progress text.
 *
 * Every run checks that the game load waits for the emulator thread, as the
 * glue turns rcheevos's background memory reads off. While emulation is
 * paused the core runs no frames and calls only ra_glue_idle(), so the seventh
 * run finishes the load that way, and ra_glue_idle() must not unlock the
 * achievement. After an unlock whose first two answers are HTTP 503,
 * ra_glue_idle() alone must then raise "disconnected", send the scheduled
 * retry and raise "reconnected". rcheevos pings 30 seconds after the load and
 * then every two minutes, but only if frames ran since its last ping: a pause
 * before any frame sends none, and a pause after frames sends one when the
 * next ping falls due.
 *
 * The eighth run shuts the client down while another thread answers an unlock
 * with HTTP 503. rcheevos sends that unlock again at once, and the run holds
 * the answering thread until ra_glue_shutdown() has dropped the pending
 * requests and waits for it. The new request must not reach Java or keep a
 * request slot, as its answer would reach the destroyed client, and rcheevos
 * must end it for good rather than schedule a retry that would never run. A
 * client created afterwards must sign in and load the game again.
 *
 * The ninth run uses a second leaderboard, which starts, is cancelled and
 * submits on three other bytes and takes its value from a fourth. In a
 * hardcore session an attempt must report its start, its live value and the
 * value's update, and the value it submits, and a cancelled attempt its
 * failure; both, and the end of the session, must hide the value.
 *
 * The tenth and eleventh runs give the game a fifth achievement, at an address
 * past the end of rcheevos's N64 memory map, which the game's load disables.
 * rcheevos checks the addresses outside its mutex, in the frame or, while
 * emulation is paused, the idle work that finishes the load. A log hook holds
 * the emulator thread there while another thread asks for the achievement
 * list: the list must wait until that frame or idle work has ended, then show
 * the achievement as unsupported. The twelfth run holds the emulator thread
 * when a state load restores progress that primes the challenge again, as
 * rcheevos raises the restored state's events outside its mutex too: the list
 * must wait until the restore has ended.
 *
 * The next runs check the progress kept with save states. The game's fourth
 * achievement unlocks after PROGRESS_HITS frames with the byte at
 * PROGRESS_ADDRESS at 1, so its hit count shows when it unlocks. Loading a
 * state must restore the count saved with it, and start it from zero when the
 * RAM differs from the state's. A state loaded before the game has loaded,
 * as when a session resumes from its auto save, must get its count once the
 * game has loaded, unless the core was reset in between or a state with no
 * progress file was loaded after it. A state saved before the game has loaded
 * must keep no progress, even while a state loaded earlier waits to give its
 * count, so loading it later starts from zero. Without a client, a save must
 * remove the progress file of an earlier save of that state.
 *
 * The last runs check the unlocks and leaderboard entries kept in the file
 * named with ra_glue_set_unsent_path() until the server answers them. An unlock
 * that gets the app's answer for a server it cannot reach, and an entry that
 * the server answers with HTTP 503, must be in the file when the session ends;
 * the next sign-in must send them again, one at a time, the first before the
 * app hears of the sign-in, and empty the file once the server records them.
 * Entries from an earlier session must go out with their age, while another
 * player's stay and a line that holds no entry is dropped. An unlock the
 * server refuses, or already has, must be dropped; after an answer rcheevos
 * would retry, the unlock must stay and the next must wait for a later
 * sign-in. Unlocks and entries the server records or refuses during the
 * session must not stay in the file.
 *
 * ra_glue.c and memory.c are included from the production sources, and
 * tools/test-ra-session.sh links the rcheevos sources the core builds.
 */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
#define TIMER_ID 7702
#define TIMER_START_ADDRESS 0x50
#define TIMER_CANCEL_ADDRESS 0x51
#define TIMER_SUBMIT_ADDRESS 0x52
#define TIMER_VALUE_ADDRESS 0x53
#define CHALLENGE_ID 5502
#define CHALLENGE_ADDRESS 0x30
#define CHALLENGE_TRIGGER_ADDRESS 0x31
#define MEASURED_ID 5503
#define MEASURED_ADDRESS 0x32
#define MEASURED_TARGET 10
#define PROGRESS_ID 5504
#define PROGRESS_ADDRESS 0x40
#define PROGRESS_HITS 5
/* Past the end of rcheevos's N64 memory map (0x7FFFFF), so the game's load disables it. */
#define BAD_ADDRESS_ID 5505
#define BAD_ADDRESS 0x800010
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
/* The server's answer to an unlock the player already has (rc_api_runtime.c:707-716). */
static const char ALREADY_UNLOCKED_RESPONSE[] =
    "{\"Success\":false,\"Error\":\"User already has this achievement awarded.\"}";
/* What RetroAchievementsManager.request answers when it cannot reach the server:
 * RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR, with the exception's text. */
static const char OFFLINE_ANSWER[] =
    "RA network error: Unable to resolve host \"retroachievements.org\"";

static const char PING_RESPONSE[] = "{\"Success\":true}";
/* A recorded leaderboard entry of 5, ranked 3rd of 12. */
static const char SUBMIT_RESPONSE[] =
    "{\"Success\":true,\"Response\":{\"Score\":5,\"BestScore\":5,"
    "\"RankInfo\":{\"Rank\":3,\"NumEntries\":\"12\"},"
    "\"TopEntries\":[{\"User\":\"tester\",\"Rank\":3,\"Score\":5}]}}";

static struct {
    int id;
    char post_data[512];
} g_requests[MAX_REQUESTS];
static int g_num_requests;
static int g_num_answered;
static int g_award_requested;
static int g_unlock_events;
static int g_progress_unlock_events;
static int g_results[4];
static int g_num_results;
/* g_num_requests when the session's first result, the sign-in's, arrived. */
static int g_requests_at_sign_in;
/* How the fake server answers: this many unlock requests, and that many
 * leaderboard entries, get HTTP 503 before one is recorded, unless it is
 * refused or the player already has the unlock. */
static int g_unavailable_awards;
static int g_unavailable_entries;
/* Before those, this many unlock requests get OFFLINE_ANSWER. */
static int g_offline_awards;
static int g_refuse_award;
static int g_award_already_unlocked;
static int g_refuse_entry;
static int g_unsent_events;
static char g_unsent_event[128];
/* Whether the game has a fifth achievement, at BAD_ADDRESS. */
static int g_bad_address;
/* Whether fake_event holds the emulator thread when the challenge indicator shows. */
static int g_hold_on_challenge;
static int g_server_errors;
static char g_server_error[512];
static int g_disconnected_events;
static int g_reconnected_events;
static int g_indicator_events;
static char g_indicator_event[256];
#define MAX_LEADERBOARD_EVENTS 16
static int g_leaderboard_events;
static char g_leaderboard_event[MAX_LEADERBOARD_EVENTS][256];
static int g_timer_entries;
static rc_clock_t g_now;
/* For the shutdown run: the ping left unanswered, and the hold on the thread
 * that answers the unlock. */
static int g_ping_id;
static _Atomic int g_hold_retry;
static _Atomic int g_retry_held;
static _Atomic int g_hold_timed_out;
static _Atomic int g_answer_done;
/* The folder for the save-state runs. The hooks only use a state's path, so
 * the state files themselves are never written. */
static char g_state_dir[] = "/tmp/ra-session-test-XXXXXX";

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

static void hold_for_list(void);

static void fake_event(const char* json)
{
    char id[32];
    char progress_id[32];

    if (g_hold_on_challenge && strstr(json, "\"type\":\"challenge_show\"") != NULL) {
        hold_for_list();
    }
    snprintf(id, sizeof(id), "\"id\":%d,", ACHIEVEMENT_ID);
    snprintf(progress_id, sizeof(progress_id), "\"id\":%d,", PROGRESS_ID);
    if (strstr(json, "\"type\":\"achievement\"") != NULL && strstr(json, id) != NULL) {
        ++g_unlock_events;
    } else if (strstr(json, "\"type\":\"achievement\"") != NULL
               && strstr(json, progress_id) != NULL) {
        ++g_progress_unlock_events;
    } else if (strstr(json, "\"type\":\"server_error\"") != NULL) {
        ++g_server_errors;
        snprintf(g_server_error, sizeof(g_server_error), "%s", json);
    } else if (strstr(json, "\"type\":\"unsent_done\"") != NULL) {
        ++g_unsent_events;
        snprintf(g_unsent_event, sizeof(g_unsent_event), "%s", json);
    } else if (strcmp(json, "{\"type\":\"disconnected\"}") == 0) {
        ++g_disconnected_events;
    } else if (strcmp(json, "{\"type\":\"reconnected\"}") == 0) {
        ++g_reconnected_events;
    } else if (strstr(json, "\"type\":\"challenge_") != NULL
               || strstr(json, "\"type\":\"progress_") != NULL) {
        ++g_indicator_events;
        snprintf(g_indicator_event, sizeof(g_indicator_event), "%s", json);
    } else if (strstr(json, "\"type\":\"leaderboard_") != NULL
               || strstr(json, "\"type\":\"tracker_") != NULL) {
        if (g_leaderboard_events < MAX_LEADERBOARD_EVENTS) {
            snprintf(g_leaderboard_event[g_leaderboard_events], sizeof(g_leaderboard_event[0]),
                     "%s", json);
        }
        ++g_leaderboard_events;
    }
}

static void fake_async_result(int result, const char* error_message)
{
    if (result != RC_OK) {
        fprintf(stderr, "request failed: %d %s\n", result, error_message);
    }
    if (g_num_results == 0) {
        g_requests_at_sign_in = g_num_requests;
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
    char game_sets[4096];
    char bad_achievement[512] = "";
    char award_param[32];
    char timer_param[32];
    int length;

    if (g_bad_address) {
        snprintf(bad_achievement, sizeof(bad_achievement),
            ",{\"ID\":%d,\"Title\":\"Far byte\",\"Description\":\"Set a byte past the RAM\","
            "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"0xH%06x=1\",\"Author\":\"tester\","
            "\"BadgeName\":\"00005\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"}",
            BAD_ADDRESS_ID, RC_ACHIEVEMENT_CATEGORY_CORE, BAD_ADDRESS);
    }
    length = snprintf(game_sets, sizeof(game_sets),
        "{\"Success\":true,\"GameId\":1001,\"Title\":\"Test Game\",\"ConsoleId\":%d,"
        "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
        "\"RichPresencePatch\":\"\","
        "\"Sets\":[{\"AchievementSetId\":1,\"GameId\":1001,\"Title\":\"\",\"Type\":\"core\","
        "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
        "\"Achievements\":[{\"ID\":%d,\"Title\":\"Byte\",\"Description\":\"Set the byte\","
        "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"0xH%06x=1\",\"Author\":\"tester\","
        "\"BadgeName\":\"00001\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"},"
        "{\"ID\":%d,\"Title\":\"Challenge\",\"Description\":\"Hold the byte\","
        "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"0xH%06x=1_T:0xH%06x=1\",\"Author\":\"tester\","
        "\"BadgeName\":\"00002\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"},"
        "{\"ID\":%d,\"Title\":\"Measured\",\"Description\":\"Count to the target\","
        "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"M:0xH%06x=%d\",\"Author\":\"tester\","
        "\"BadgeName\":\"00003\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"},"
        "{\"ID\":%d,\"Title\":\"Hits\",\"Description\":\"Hold the byte for a while\","
        "\"Flags\":%d,\"Points\":5,\"MemAddr\":\"0xH%06x=1.%d.\",\"Author\":\"tester\","
        "\"BadgeName\":\"00004\",\"Created\":1700000000,\"Modified\":1700000000,\"Type\":\"\"}%s],"
        "\"Leaderboards\":[{\"ID\":%d,\"Title\":\"Fastest byte\","
        "\"Description\":\"Set the other byte\","
        "\"Mem\":\"STA:0xH%06x=1::CAN:0xH%06x=2::SUB:0xH%06x=1::VAL:0xH%06x\","
        "\"Format\":\"VALUE\",\"LowerIsBetter\":false,\"Hidden\":false},"
        "{\"ID\":%d,\"Title\":\"Timer\",\"Description\":\"Hold the value\","
        "\"Mem\":\"STA:0xH%06x=1::CAN:0xH%06x=1::SUB:0xH%06x=1::VAL:0xH%06x\","
        "\"Format\":\"VALUE\",\"LowerIsBetter\":false,\"Hidden\":false}]}]}",
        RC_CONSOLE_NINTENDO_64, ACHIEVEMENT_ID, RC_ACHIEVEMENT_CATEGORY_CORE, ACHIEVEMENT_ADDRESS,
        CHALLENGE_ID, RC_ACHIEVEMENT_CATEGORY_CORE, CHALLENGE_ADDRESS, CHALLENGE_TRIGGER_ADDRESS,
        MEASURED_ID, RC_ACHIEVEMENT_CATEGORY_CORE, MEASURED_ADDRESS, MEASURED_TARGET,
        PROGRESS_ID, RC_ACHIEVEMENT_CATEGORY_CORE, PROGRESS_ADDRESS, PROGRESS_HITS, bad_achievement,
        LEADERBOARD_ID, LEADERBOARD_ADDRESS, LEADERBOARD_ADDRESS, LEADERBOARD_ADDRESS,
        LEADERBOARD_ADDRESS + 1, TIMER_ID, TIMER_START_ADDRESS, TIMER_CANCEL_ADDRESS,
        TIMER_SUBMIT_ADDRESS, TIMER_VALUE_ADDRESS);
    CHECK(length > 0 && length < (int)sizeof(game_sets));
    snprintf(award_param, sizeof(award_param), "a=%d", ACHIEVEMENT_ID);
    snprintf(timer_param, sizeof(timer_param), "i=%d", TIMER_ID);

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
            if (g_offline_awards > 0) {
                --g_offline_awards;
                body = OFFLINE_ANSWER;
                status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            } else if (g_unavailable_awards > 0) {
                --g_unavailable_awards;
                body = "";
                status = 503;
            } else if (g_award_already_unlocked) {
                body = ALREADY_UNLOCKED_RESPONSE;
            } else {
                body = g_refuse_award ? REFUSAL_RESPONSE : AWARD_RESPONSE;
            }
        } else if (is_request(post_data, "submitlbentry") && g_unavailable_entries > 0) {
            --g_unavailable_entries;
            body = "";
            status = 503;
        } else if (is_request(post_data, "submitlbentry") && g_refuse_entry) {
            body = REFUSAL_RESPONSE;
        } else if (is_request(post_data, "submitlbentry")) {
            g_timer_entries += has_param(post_data, timer_param)
                               && has_param(post_data, "s=5");
            body = SUBMIT_RESPONSE;
        } else if (is_request(post_data, "ping")) {
            body = PING_RESPONSE;
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
    /* The same RAM in every session, so a state saved in one matches the next. */
    memset(*rdram, 0, RDRAM_8MB_SIZE);
    (*rdram)[ACHIEVEMENT_ADDRESS] = 0;
    (*rdram)[LEADERBOARD_ADDRESS] = 0;
    (*rdram)[LEADERBOARD_ADDRESS + 1] = 42;
    (*rdram)[CHALLENGE_ADDRESS] = 0;
    (*rdram)[CHALLENGE_TRIGGER_ADDRESS] = 0;
    (*rdram)[MEASURED_ADDRESS] = 0;
    g_mem_base = base;
    return base;
}

static void reset_fixture(void)
{
    g_num_requests = 0;
    g_num_answered = 0;
    g_award_requested = 0;
    g_unlock_events = 0;
    g_progress_unlock_events = 0;
    g_num_results = 0;
    g_requests_at_sign_in = 0;
    g_unavailable_awards = 0;
    g_unavailable_entries = 0;
    g_offline_awards = 0;
    g_refuse_award = 0;
    g_award_already_unlocked = 0;
    g_refuse_entry = 0;
    g_unsent_events = 0;
    g_unsent_event[0] = '\0';
    g_server_errors = 0;
    g_server_error[0] = '\0';
    g_disconnected_events = 0;
    g_reconnected_events = 0;
    g_indicator_events = 0;
    g_indicator_event[0] = '\0';
    g_leaderboard_events = 0;
    g_timer_entries = 0;
    g_now = 1000000;
}

/* Signs in and asks for the game. */
static void sign_in(int hardcore)
{
    CHECK(ra_glue_create() == 1);
    rc_client_set_get_time_millisecs_function(s_client, fake_clock);
    ra_glue_set_java_bridge((uintptr_t)fake_server_call, (uintptr_t)fake_event,
                            (uintptr_t)fake_async_result);
    ra_glue_set_hardcore(hardcore);
    CHECK(ra_glue_login_token("tester", "token") == 1);
    answer_requests();
    CHECK(ra_glue_load_game(GAME_HASH) == 1);
    answer_requests();
    /* Only the sign-in has reported: the load finishes on the emulator thread. */
    CHECK(g_num_results == 1);
}

/* Resets the fixture, signs in and asks for the game. */
static void begin_session(int hardcore)
{
    reset_fixture();
    sign_in(hardcore);
}

/* Signs in, loads the game and runs two frames, or, while emulation is paused,
 * calls ra_glue_idle() twice; the achievement is then active. */
static void* start_session(int memory_before_load, int hardcore, int paused, uint8_t** rdram)
{
    const rc_client_achievement_t* achievement;
    void* base = NULL;
    int step;

    g_mem_base = NULL;
    if (memory_before_load) {
        base = create_memory(rdram);
    }
    begin_session(hardcore);
    if (!memory_before_load) {
        base = create_memory(rdram);
    }

    for (step = 0; step < 2; ++step) {
        if (paused) {
            ra_glue_idle();
        } else {
            ra_glue_do_frame();
        }
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
    void* base = start_session(memory_before_load, 0, 0, &rdram);

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
    void* base = start_session(1, 0, 0, &rdram);

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
    void* base = start_session(1, 1, 0, &rdram);

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
    void* base = start_session(1, 0, 0, &rdram);
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

/* Checks that the achievement list's entry for the achievement contains the field text. */
static int list_entry_has(int id, const char* field)
{
    static char list[65536];
    char start[32];
    char entry[1024];
    const char* found;
    size_t length;

    if (!ra_glue_get_achievements_json(list, (int)sizeof(list))) {
        return 0;
    }
    snprintf(start, sizeof(start), "{\"id\":%d,", id);
    found = strstr(list, start);
    if (found == NULL) {
        return 0;
    }
    length = strcspn(found, "}");
    if (length >= sizeof(entry)) {
        return 0;
    }
    memcpy(entry, found, length);
    entry[length] = '\0';
    return strstr(entry, field) != NULL;
}

/* Runs one frame, which must raise exactly the indicator event given. */
static void check_indicator_frame(const char* expected)
{
    const int events_before = g_indicator_events;

    ra_glue_do_frame();
    CHECK(g_indicator_events == events_before + 1);
    CHECK(strcmp(g_indicator_event, expected) == 0);
}

static void run_indicators(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    const int requests = g_num_requests;
    char expected[160];

    CHECK(list_entry_has(CHALLENGE_ID, "\"challenge\":0"));
    CHECK(list_entry_has(MEASURED_ID, "\"progress\":\"\""));

    rdram[CHALLENGE_ADDRESS] = 1;
    snprintf(expected, sizeof(expected),
             "{\"type\":\"challenge_show\",\"id\":%d,\"title\":\"Challenge\"}", CHALLENGE_ID);
    check_indicator_frame(expected);
    CHECK(list_entry_has(CHALLENGE_ID, "\"challenge\":1"));

    rdram[CHALLENGE_ADDRESS] = 0;
    snprintf(expected, sizeof(expected),
             "{\"type\":\"challenge_hide\",\"id\":%d,\"title\":\"Challenge\"}", CHALLENGE_ID);
    check_indicator_frame(expected);
    CHECK(list_entry_has(CHALLENGE_ID, "\"challenge\":0"));

    rdram[MEASURED_ADDRESS] = 3;
    snprintf(expected, sizeof(expected),
             "{\"type\":\"progress_show\",\"id\":%d,\"title\":\"Measured\",\"progress\":\"3/%d\"}",
             MEASURED_ID, MEASURED_TARGET);
    check_indicator_frame(expected);

    rdram[MEASURED_ADDRESS] = 4;
    snprintf(expected, sizeof(expected),
             "{\"type\":\"progress_update\",\"id\":%d,\"title\":\"Measured\",\"progress\":\"4/%d\"}",
             MEASURED_ID, MEASURED_TARGET);
    check_indicator_frame(expected);
    snprintf(expected, sizeof(expected), "\"progress\":\"4/%d\"", MEASURED_TARGET);
    CHECK(list_entry_has(MEASURED_ID, expected));

    g_now += 2000;
    check_indicator_frame("{\"type\":\"progress_hide\"}");

    /* Neither achievement unlocked, so nothing was sent. */
    CHECK(g_num_requests == requests);

    end_session(base, "challenge and progress indicators", failures_before);
}

static void run_paused(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 1, &rdram);
    int requests = g_num_requests;

    /* The first ping falls due, but no frame has run since the load. */
    g_now += 150 * 1000;
    ra_glue_idle();
    CHECK(g_num_requests == requests);

    /* Running again: the unlock takes a frame in which the achievement is
     * false and one in which it is true; ra_glue_idle() in between must not
     * evaluate it. rcheevos sends it again at once after the first failure
     * and schedules the next try one second later. */
    g_unavailable_awards = 2;
    ra_glue_do_frame();
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    ra_glue_idle();
    CHECK(g_unlock_events == 0);
    CHECK(g_num_requests == requests);
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_unlock_events == 1);
    CHECK(g_unavailable_awards == 0);

    /* Paused again: no frames, only ra_glue_idle(). */
    CHECK(g_disconnected_events == 0);
    ra_glue_idle();
    CHECK(g_disconnected_events == 1);
    requests = g_num_requests;
    g_now += 1000;
    ra_glue_idle();
    CHECK(g_num_requests == requests + 1);
    answer_requests();
    CHECK(g_reconnected_events == 0);
    ra_glue_idle();
    CHECK(g_reconnected_events == 1);
    CHECK(g_disconnected_events == 1);
    CHECK(g_server_errors == 0);

    /* Frames ran since the last ping, so the next one is sent, and the one
     * after it is not. */
    requests = g_num_requests;
    g_now += 120 * 1000;
    ra_glue_idle();
    CHECK(g_num_requests == requests + 1);
    CHECK(g_num_requests > requests && is_request(g_requests[requests].post_data, "ping"));
    answer_requests();
    requests = g_num_requests;
    g_now += 120 * 1000;
    ra_glue_idle();
    CHECK(g_num_requests == requests);

    end_session(base, "game load, unlock retry and pings while paused", failures_before);
}

/* Counts the glue's request slots in use, for one request or, with 0, for all. */
static int pending_slots(int request_id)
{
    int count = 0;

    for (int i = 0; i < RA_GLUE_MAX_PENDING; ++i) {
        if (s_pending[i].in_use && (request_id == 0 || s_pending[i].id == request_id)) {
            ++count;
        }
    }
    return count;
}

/* rcheevos logs this just before it sends a failed unlock again at once. */
static void hold_retry(const char* message, const rc_client_t* client)
{
    const time_t deadline = time(NULL) + 10;

    (void)client;
    if (!g_hold_retry || strstr(message, "retrying immediately") == NULL) {
        return;
    }
    g_retry_held = 1;
    /* ra_glue_shutdown() drops every pending request, the ping too, while it
     * holds the lock, and then waits for this thread. */
    while (time(NULL) <= deadline) {
        if (pthread_mutex_trylock(&s_pending_lock) == 0) {
            const int ping_pending = pending_slots(g_ping_id);

            pthread_mutex_unlock(&s_pending_lock);
            if (!ping_pending) {
                return;
            }
        }
        sched_yield();
    }
    g_hold_timed_out = 1;
}

static void* answer_unlock_unavailable(void* request_id)
{
    ra_glue_dispatch_server_response(*(const int*)request_id, "", 0, 503);
    g_answer_done = 1;
    return NULL;
}

static void* shut_down(void* unused)
{
    (void)unused;
    ra_glue_shutdown();
    return NULL;
}

static void run_shutdown_during_retry(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    pthread_t answer_thread;
    pthread_t shutdown_thread;
    int award_id = 0;
    int requests;
    char expected[64];

    /* The unlock and, 30 seconds after the load, a ping go out unanswered. */
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    ra_glue_do_frame();
    g_now += 30 * 1000;
    ra_glue_do_frame();
    CHECK(g_unlock_events == 1);
    CHECK(g_num_requests == g_num_answered + 2);
    if (g_num_requests == g_num_answered + 2) {
        CHECK(is_request(g_requests[g_num_answered].post_data, "awardachievement"));
        CHECK(is_request(g_requests[g_num_answered + 1].post_data, "ping"));
        award_id = g_requests[g_num_answered].id;
        g_ping_id = g_requests[g_num_answered + 1].id;
    }
    requests = g_num_requests;

    if (award_id != 0) {
        rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_ERROR, hold_retry);
        g_retry_held = 0;
        g_hold_timed_out = 0;
        g_answer_done = 0;
        g_hold_retry = 1;
        pthread_create(&answer_thread, NULL, answer_unlock_unavailable, &award_id);
        while (!g_retry_held && !g_answer_done) {
            sched_yield();
        }
        CHECK(g_retry_held);
        pthread_create(&shutdown_thread, NULL, shut_down, NULL);
        pthread_join(answer_thread, NULL);
        pthread_join(shutdown_thread, NULL);
        g_hold_retry = 0;
        CHECK(!g_hold_timed_out);

        /* The unlock went out again after shutdown began. rcheevos raises a
         * server error for a request it does not retry (rc_client.h), and this
         * one must name that unlock. */
        CHECK(g_num_requests == requests);
        pthread_mutex_lock(&s_pending_lock);
        CHECK(pending_slots(0) == 0);
        pthread_mutex_unlock(&s_pending_lock);
        CHECK(g_server_errors == 1);
        CHECK(strstr(g_server_error, "\"api\":\"award_achievement\"") != NULL);
        snprintf(expected, sizeof(expected), "\"related_id\":%d,", ACHIEVEMENT_ID);
        CHECK(strstr(g_server_error, expected) != NULL);
    }

    /* A client created afterwards signs in and loads the game again. */
    g_mem_base = NULL;
    release_mem_base(base);
    base = start_session(1, 0, 0, &rdram);
    end_session(base, "unlock sent again while the client shuts down", failures_before);
}

/* Whether a leaderboard event from index `from` on has `type` and contains `text`. */
static int has_leaderboard_event(int from, const char* type, const char* text)
{
    char wanted[64];
    int i;

    snprintf(wanted, sizeof(wanted), "\"type\":\"%s\"", type);
    for (i = from; i < g_leaderboard_events && i < MAX_LEADERBOARD_EVENTS; ++i) {
        if (strstr(g_leaderboard_event[i], wanted) != NULL
            && strstr(g_leaderboard_event[i], text) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void run_leaderboard_attempts(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 1, 0, &rdram);
    char timer[48];
    int from;

    snprintf(timer, sizeof(timer), "\"id\":%d,\"title\":\"Timer\"", TIMER_ID);

    from = g_leaderboard_events;
    rdram[TIMER_VALUE_ADDRESS] = 3;
    rdram[TIMER_START_ADDRESS] = 1;
    ra_glue_do_frame();
    CHECK(g_leaderboard_events == from + 2);
    CHECK(has_leaderboard_event(from, "leaderboard_started", timer));
    CHECK(has_leaderboard_event(from, "tracker_show", "\"display\":\"3\""));

    from = g_leaderboard_events;
    rdram[TIMER_START_ADDRESS] = 0;
    rdram[TIMER_VALUE_ADDRESS] = 4;
    ra_glue_do_frame();
    CHECK(g_leaderboard_events == from + 1);
    CHECK(has_leaderboard_event(from, "tracker_update", "\"display\":\"4\""));

    /* The value changes on the frame that submits: the entry and its notice must both have 5. */
    from = g_leaderboard_events;
    rdram[TIMER_VALUE_ADDRESS] = 5;
    rdram[TIMER_SUBMIT_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    /* Sent once and not refused. At this fixture's status 200, rcheevos sends an entry again
     * when a failed answer gives it no error text (malformed JSON, an empty body), and raises
     * a server error when it does; at a status it retries, it sends the entry again either way. */
    CHECK(g_timer_entries == 1);
    CHECK(g_server_errors == 0);
    CHECK(g_leaderboard_events == from + 2);
    CHECK(has_leaderboard_event(from, "leaderboard_submitted", "\"title\":\"Timer\",\"value\":\"5\""));
    CHECK(has_leaderboard_event(from, "tracker_hide", "\"id\":"));

    /* A second attempt, cancelled. */
    rdram[TIMER_SUBMIT_ADDRESS] = 0;
    ra_glue_do_frame();
    from = g_leaderboard_events;
    rdram[TIMER_START_ADDRESS] = 1;
    ra_glue_do_frame();
    rdram[TIMER_START_ADDRESS] = 0;
    rdram[TIMER_CANCEL_ADDRESS] = 1;
    ra_glue_do_frame();
    CHECK(g_leaderboard_events == from + 4);
    CHECK(has_leaderboard_event(from, "leaderboard_started", timer));
    CHECK(has_leaderboard_event(from, "leaderboard_failed", timer));
    CHECK(has_leaderboard_event(from + 2, "tracker_hide", "\"id\":"));

    /* Ending the session hides the value of an attempt that is still running. */
    rdram[TIMER_CANCEL_ADDRESS] = 0;
    ra_glue_do_frame();
    rdram[TIMER_START_ADDRESS] = 1;
    ra_glue_do_frame();
    CHECK(has_leaderboard_event(from + 4, "tracker_show", "\"id\":"));
    from = g_leaderboard_events;
    ra_glue_shutdown();
    CHECK(g_leaderboard_events == from + 1);
    CHECK(has_leaderboard_event(from, "tracker_hide", "\"id\":"));

    end_session(base, "leaderboard attempts in hardcore", failures_before);
}

/* For the list runs: the emulator thread is held where rcheevos has just changed achievements
 * outside its mutex, while another thread asks for the achievement list. During the hold the
 * threads use only relaxed atomics, so ThreadSanitizer sees no ordering between them but the
 * glue's; g_frame_done, set after the held work, orders the rest. */
static atomic_int g_held;
static atomic_int g_hold_ended;
static atomic_int g_frame_done;
static atomic_int g_list_built;
static atomic_int g_list_during_hold;

static int relaxed(atomic_int* flag)
{
    return atomic_load_explicit(flag, memory_order_relaxed);
}

static void set_relaxed(atomic_int* flag, int value)
{
    atomic_store_explicit(flag, value, memory_order_relaxed);
}

static void reset_hold(void)
{
    set_relaxed(&g_held, 0);
    set_relaxed(&g_hold_ended, 0);
    set_relaxed(&g_frame_done, 0);
    set_relaxed(&g_list_built, 0);
    set_relaxed(&g_list_during_hold, 0);
}

/* Holds the emulator thread once per run until the list is built, for at most 500 ms; with the
 * glue's lock the list waits, so the hold lasts 500 ms. A run passes without the lock if the list
 * thread stalls for longer, as when ThreadSanitizer stalls it to print its first report. */
static void hold_for_list(void)
{
    struct timespec start;
    struct timespec now;

    if (relaxed(&g_held)) {
        return;
    }
    set_relaxed(&g_held, 1);
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        sched_yield();
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (!relaxed(&g_list_built)
             && (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 < 500);
    set_relaxed(&g_hold_ended, 1);
}

/* rcheevos logs this while it checks the addresses of a game that is loading. */
static void hold_validation(const char* message, const rc_client_t* client)
{
    (void)client;
    if (strstr(message, "Disabled achievement") != NULL) {
        hold_for_list();
    }
}

static void* build_list_during_hold(void* unused)
{
    char* list = malloc(65536);

    (void)unused;
    while (!relaxed(&g_held) && !atomic_load_explicit(&g_frame_done, memory_order_acquire)) {
        sched_yield();
    }
    if (list != NULL && relaxed(&g_held) && ra_glue_get_achievements_json(list, 65536)) {
        set_relaxed(&g_list_during_hold, !relaxed(&g_hold_ended));
        set_relaxed(&g_list_built, 1);
    }
    free(list);
    return NULL;
}

/* The achievement list asked for while the game finishes loading, in a frame or, while emulation
 * is paused, in idle work: rcheevos checks the game's addresses there outside its mutex, so the
 * list must wait until that work has ended. */
static void run_list_while_loading(int paused, const char* name)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base;
    pthread_t list_thread;

    g_mem_base = NULL;
    base = create_memory(&rdram);
    g_bad_address = 1;
    begin_session(0);
    rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_WARN, hold_validation);
    reset_hold();
    pthread_create(&list_thread, NULL, build_list_during_hold, NULL);
    if (paused) {
        ra_glue_idle();
    } else {
        ra_glue_do_frame();
    }
    atomic_store_explicit(&g_frame_done, 1, memory_order_release);
    pthread_join(list_thread, NULL);
    answer_requests();

    CHECK(g_num_results == 2 && g_results[1] == RC_OK);
    CHECK(relaxed(&g_held));
    CHECK(relaxed(&g_list_built));
    CHECK(!relaxed(&g_list_during_hold));
    CHECK(list_entry_has(BAD_ADDRESS_ID, "\"unsupported\":1"));

    g_bad_address = 0;
    end_session(base, name, failures_before);
}

static void state_file(char* out, size_t size, const char* name)
{
    snprintf(out, size, "%s/%s", g_state_dir, name);
}

/* Where the glue keeps a state's progress: <state folder>/.ra-progress/<name>.progress. */
static void progress_file(char* out, size_t size, const char* name)
{
    snprintf(out, size, "%s/.ra-progress/%s.progress", g_state_dir, name);
}

static int file_exists(const char* path)
{
    struct stat info;

    return stat(path, &info) == 0;
}

/* The achievement list asked for while a state load restores progress that primes the challenge
 * again: rcheevos raises the restored state's events, which rewrite the achievements, outside its
 * mutex, so the list must wait until the restore has ended. */
static void run_list_while_restoring(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    char state[128];
    pthread_t list_thread;

    state_file(state, sizeof(state), "challenge.st0");
    rdram[CHALLENGE_ADDRESS] = 1;
    ra_glue_do_frame();
    ra_glue_state_saved(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    rdram[CHALLENGE_ADDRESS] = 0;
    ra_glue_do_frame();
    CHECK(list_entry_has(CHALLENGE_ID, "\"challenge\":0"));

    /* The core has put the state's RAM back before it reports the load. */
    rdram[CHALLENGE_ADDRESS] = 1;
    reset_hold();
    g_hold_on_challenge = 1;
    pthread_create(&list_thread, NULL, build_list_during_hold, NULL);
    ra_glue_state_loaded(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    atomic_store_explicit(&g_frame_done, 1, memory_order_release);
    pthread_join(list_thread, NULL);
    g_hold_on_challenge = 0;

    CHECK(relaxed(&g_held));
    CHECK(relaxed(&g_list_built));
    CHECK(!relaxed(&g_list_during_hold));
    CHECK(list_entry_has(CHALLENGE_ID, "\"challenge\":1"));
    end_session(base, "achievement list asked for while a state load restores progress",
                failures_before);
}

/* Loading a state restores the hit count saved with it, and starts the count
 * from zero when the RAM differs from the state's. */
static void run_progress_with_states(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    char state[128];
    char progress[160];
    int frame;

    state_file(state, sizeof(state), "progress.st0");
    progress_file(progress, sizeof(progress), "progress.st0");
    rdram[PROGRESS_ADDRESS] = 1;
    for (frame = 0; frame < 3; ++frame) {
        ra_glue_do_frame();
    }
    ra_glue_state_saved(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    CHECK(file_exists(progress));

    /* A fourth hit; the load goes back to three, so one more is not enough. */
    ra_glue_do_frame();
    ra_glue_state_loaded(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    ra_glue_do_frame();
    CHECK(g_progress_unlock_events == 0);

    /* With other RAM the count starts from zero, and rcheevos waits for a frame
     * in which the achievement is false before it counts again. */
    rdram[PROGRESS_ADDRESS + 1] = 7;
    ra_glue_state_loaded(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    rdram[PROGRESS_ADDRESS] = 0;
    ra_glue_do_frame();
    rdram[PROGRESS_ADDRESS] = 1;
    for (frame = 0; frame < PROGRESS_HITS - 1; ++frame) {
        ra_glue_do_frame();
    }
    CHECK(g_progress_unlock_events == 0);
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_progress_unlock_events == 1);

    end_session(base, "hit counts kept with save states", failures_before);
}

/* A state loaded before the game has loaded, as when a session resumes from
 * its auto save, gets its hit count once the game has loaded, unless the core
 * is reset in between. */
static void run_progress_while_loading(int reset, const char* name)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    char state[128];
    int frame;

    /* The first session saves the state with three hits. */
    state_file(state, sizeof(state), "resume.sav");
    rdram[PROGRESS_ADDRESS] = 1;
    for (frame = 0; frame < 3; ++frame) {
        ra_glue_do_frame();
    }
    ra_glue_state_saved(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    /* The next one loads it before the game has loaded. */
    base = create_memory(&rdram);
    rdram[PROGRESS_ADDRESS] = 1;
    begin_session(0);
    ra_glue_state_loaded(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    if (reset) {
        ra_glue_reset();
    }
    /* The game loads at the end of this frame, which runs none of its achievements. */
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_num_results == 2 && g_results[1] == RC_OK);
    ra_glue_do_frame();
    CHECK(g_progress_unlock_events == 0);
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_progress_unlock_events == (reset ? 0 : 1));

    end_session(base, name, failures_before);
}

/* A second state loaded before the game has loaded replaces the count that
 * the first one left waiting, even with no progress file of its own. */
static void run_second_load_while_loading(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    char first[128];
    char bare[128];
    int frame;

    state_file(first, sizeof(first), "first.st4");
    state_file(bare, sizeof(bare), "bare.st5");

    /* The first session saves a state with three hits. */
    rdram[PROGRESS_ADDRESS] = 1;
    for (frame = 0; frame < 3; ++frame) {
        ra_glue_do_frame();
    }
    ra_glue_state_saved(first, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    /* The next one loads it, then a state with no progress file, before the
     * game has loaded. */
    base = create_memory(&rdram);
    rdram[PROGRESS_ADDRESS] = 1;
    begin_session(0);
    ra_glue_state_loaded(first, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    ra_glue_state_loaded(bare, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    /* The game loads at the end of this frame. With the first state's three
     * hits, the two frames after it would unlock the achievement. */
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_num_results == 2 && g_results[1] == RC_OK);
    ra_glue_do_frame();
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_progress_unlock_events == 0);

    end_session(base, "no hit count after a second load while the game loads", failures_before);
}

/* A state saved before the game has loaded keeps no progress, even while a
 * state loaded earlier waits to give the game its count, as in a session whose
 * sign-in failed and whose game never loads. None of the game's achievements
 * ran on that state's RAM, so loading it later starts the count from zero. */
static void run_save_before_game_loads(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base = start_session(1, 0, 0, &rdram);
    char earlier[128];
    char later[128];
    char later_progress[160];
    int frame;

    state_file(earlier, sizeof(earlier), "earlier.st2");
    state_file(later, sizeof(later), "later.st3");
    progress_file(later_progress, sizeof(later_progress), "later.st3");

    /* The first session saves a state with three hits. */
    rdram[PROGRESS_ADDRESS] = 1;
    for (frame = 0; frame < 3; ++frame) {
        ra_glue_do_frame();
    }
    ra_glue_state_saved(earlier, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    /* The next one has a client but never loads the game. It loads that state,
     * plays on to other RAM and saves another state. */
    base = create_memory(&rdram);
    rdram[PROGRESS_ADDRESS] = 1;
    CHECK(ra_glue_create() == 1);
    ra_glue_state_loaded(earlier, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    rdram[PROGRESS_ADDRESS + 1] = 9;
    ra_glue_do_frame();
    ra_glue_state_saved(later, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    CHECK(!file_exists(later_progress));
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    /* A session whose game loads loads the second state. With the first
     * state's three hits, the remaining frames would unlock the achievement. */
    base = start_session(1, 0, 0, &rdram);
    rdram[PROGRESS_ADDRESS] = 1;
    rdram[PROGRESS_ADDRESS + 1] = 9;
    ra_glue_state_loaded(later, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    for (frame = 0; frame < PROGRESS_HITS - 3; ++frame) {
        ra_glue_do_frame();
    }
    answer_requests();
    CHECK(g_progress_unlock_events == 0);

    end_session(base, "no hit count in a state saved before the game loads", failures_before);
}

/* Without a client, saving a state removes the progress file of an earlier
 * save of that state, and loading one changes nothing. */
static void run_save_without_achievements(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    void* base;
    char state[128];
    char progress[160];
    char folder[128];
    FILE* file;

    g_mem_base = NULL;
    base = create_memory(&rdram);
    state_file(state, sizeof(state), "off.st1");
    progress_file(progress, sizeof(progress), "off.st1");
    snprintf(folder, sizeof(folder), "%s/.ra-progress", g_state_dir);
    mkdir(folder, 0700);
    file = fopen(progress, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("from an earlier save", file);
        fclose(file);
    }
    ra_glue_state_saved(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);
    CHECK(!file_exists(progress));
    ra_glue_state_loaded(state, (const uint32_t*)rdram, RDRAM_8MB_SIZE);

    g_mem_base = NULL;
    release_mem_base(base);
    printf("%s: %s\n", "progress file removed by a save without achievements",
           g_failures == failures_before ? "ok" : "FAILED");
}

/* The file of unlocks and leaderboard entries the server has not answered, for
 * the last runs. */
static void unsent_file(char* out, size_t size)
{
    snprintf(out, size, "%s/unsent.txt", g_state_dir);
}

static void write_text_file(const char* path, const char* text)
{
    FILE* file = fopen(path, "w");

    CHECK(file != NULL);
    if (file != NULL) {
        fputs(text, file);
        fclose(file);
    }
}

static int file_contains(const char* path, const char* text)
{
    char line[256];
    int found = 0;
    FILE* file = fopen(path, "r");

    while (file != NULL && !found && fgets(line, sizeof(line), file) != NULL) {
        found = strstr(line, text) != NULL;
    }
    if (file != NULL) {
        fclose(file);
    }
    return found;
}

/* The entries of the unsent file, in the format ra_glue.c gives, with this kind,
 * id, value and user and the game's hash; with id -1, every entry. */
static int unsent_entries(const char* path, char kind, int id, int value, const char* user)
{
    char line[256];
    int count = 0;
    FILE* file = fopen(path, "r");

    while (file != NULL && fgets(line, sizeof(line), file) != NULL) {
        char line_kind;
        unsigned int line_id;
        int line_value;
        long long line_time;
        char hash[65];
        char line_user[65];

        if (sscanf(line, "%c %u %d %lld %64s %64s", &line_kind, &line_id, &line_value, &line_time,
                   hash, line_user) != 6) {
            continue;
        }
        count += id == -1 || (line_kind == kind && (int)line_id == id && line_value == value
                              && strcmp(hash, GAME_HASH) == 0 && strcmp(line_user, user) == 0);
    }
    if (file != NULL) {
        fclose(file);
    }
    return count;
}

/* A number in POST data, or -1 when it is missing. */
static long param_number(const char* post_data, const char* key)
{
    const size_t length = strlen(key);
    const char* found = post_data;

    while ((found = strstr(found, key)) != NULL) {
        if ((found == post_data || found[-1] == '&') && found[length] == '=') {
            return strtol(found + length + 1, NULL, 10);
        }
        ++found;
    }
    return -1;
}

/* The first request to api whose number key has that value, or -1. */
static int find_request(const char* api, const char* key, long value)
{
    int i;

    for (i = 0; i < g_num_requests; ++i) {
        if (is_request(g_requests[i].post_data, api)
            && param_number(g_requests[i].post_data, key) == value) {
            return i;
        }
    }
    return -1;
}

/* The age, in seconds, that a request gives an unlock or entry sent again;
 * rcheevos leaves it out when it is 0 (rc_api_runtime.c:649-650,758-759). */
static long request_age(int request)
{
    const long age = param_number(g_requests[request].post_data, "o");

    return age < 0 ? 0 : age;
}

static void end_unsent_run(const char* path, void* base, const char* name, int failures_before)
{
    /* As the emulator thread would, finish the game's load: rcheevos does not free a load
     * still queued for that thread when the client is destroyed. */
    ra_glue_idle();
    answer_requests();
    CHECK(ra_glue_set_unsent_path(NULL) == 1);
    remove(path);
    end_session(base, name, failures_before);
}

/* An unlock the app could not send, as when the device is offline, and a
 * leaderboard entry the server answered with HTTP 503 are kept when the
 * session ends, and the next sign-in sends them again. */
static void run_unsent_sent_again(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    char path[160];
    void* base;
    int award;
    int entry;

    unsent_file(path, sizeof(path));
    CHECK(ra_glue_set_unsent_path(path) == 1);
    base = start_session(1, 1, 0, &rdram);
    g_offline_awards = 1000;
    g_unavailable_entries = 1000;
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    rdram[LEADERBOARD_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_award_requested);
    CHECK(g_server_errors == 0);
    CHECK(unsent_entries(path, 'A', ACHIEVEMENT_ID, 1, "tester") == 1);
    CHECK(unsent_entries(path, 'L', LEADERBOARD_ID, 42, "tester") == 1);
    CHECK(unsent_entries(path, 0, -1, 0, NULL) == 2);
    ra_glue_shutdown();
    g_mem_base = NULL;
    release_mem_base(base);

    base = create_memory(&rdram);
    reset_fixture();
    sign_in(0);
    award = find_request("awardachievement", "a", ACHIEVEMENT_ID);
    entry = find_request("submitlbentry", "i", LEADERBOARD_ID);
    /* One at a time, the first before the app hears of the sign-in and asks for the game. */
    CHECK(award >= 0 && award < g_requests_at_sign_in && entry > award);
    if (award >= 0 && entry >= 0) {
        CHECK(has_param(g_requests[award].post_data, "h=1"));
        CHECK(has_param(g_requests[award].post_data, "m=" GAME_HASH));
        CHECK(has_param(g_requests[award].post_data, "u=tester"));
        CHECK(request_age(award) <= 5);
        CHECK(param_number(g_requests[entry].post_data, "s") == 42);
        CHECK(has_param(g_requests[entry].post_data, "m=" GAME_HASH));
        CHECK(request_age(entry) <= 5);
    }
    CHECK(g_unsent_events == 1);
    CHECK(strcmp(g_unsent_event, "{\"type\":\"unsent_done\",\"recorded\":2,\"refused\":0}") == 0);
    CHECK(unsent_entries(path, 0, -1, 0, NULL) == 0);
    CHECK(g_server_errors == 0);
    end_unsent_run(path, base, "unlock and leaderboard entry sent again after the next sign-in",
                   failures_before);
}

/* Entries from an earlier session go out with their age; another player's
 * entry stays, and a line that holds no entry is dropped. */
static void run_unsent_from_file(void)
{
    const int failures_before = g_failures;
    const long long now = (long long)time(NULL);
    uint8_t* rdram = NULL;
    char path[160];
    char text[512];
    void* base;
    int award;
    int entry;

    unsent_file(path, sizeof(path));
    snprintf(text, sizeof(text),
             "# entries\nA %d 0 %lld %s tester\nnot an entry\nL %d 5 %lld %s tester\n"
             "A %d 1 %lld %s someone\n",
             ACHIEVEMENT_ID, now - 3600, GAME_HASH, TIMER_ID, now - 7200, GAME_HASH,
             CHALLENGE_ID, now - 60, GAME_HASH);
    write_text_file(path, text);
    CHECK(ra_glue_set_unsent_path(path) == 1);
    g_mem_base = NULL;
    base = create_memory(&rdram);
    reset_fixture();
    sign_in(0);

    award = find_request("awardachievement", "a", ACHIEVEMENT_ID);
    entry = find_request("submitlbentry", "i", TIMER_ID);
    CHECK(award >= 0 && entry >= 0);
    if (award >= 0 && entry >= 0) {
        CHECK(has_param(g_requests[award].post_data, "h=0"));
        CHECK(request_age(award) >= 3600 && request_age(award) <= 3610);
        CHECK(param_number(g_requests[entry].post_data, "s") == 5);
        CHECK(request_age(entry) >= 7200 && request_age(entry) <= 7210);
    }
    CHECK(find_request("awardachievement", "a", CHALLENGE_ID) < 0);
    CHECK(strcmp(g_unsent_event, "{\"type\":\"unsent_done\",\"recorded\":2,\"refused\":0}") == 0);
    CHECK(unsent_entries(path, 0, -1, 0, NULL) == 1);
    CHECK(unsent_entries(path, 'A', CHALLENGE_ID, 1, "someone") == 1);
    CHECK(!file_contains(path, "not an entry"));
    end_unsent_run(path, base, "entries from an earlier session sent with their age", failures_before);
}

enum { ANSWER_REFUSED, ANSWER_ALREADY_UNLOCKED, ANSWER_UNAVAILABLE };

/* Unlocks from an earlier session that the server refuses, or already has, are
 * dropped. After an answer rcheevos would retry, the unlock stays, and the next
 * waits for a later sign-in. */
static void run_unsent_answer(int answer, const char* name)
{
    const int failures_before = g_failures;
    const long long now = (long long)time(NULL);
    uint8_t* rdram = NULL;
    char path[160];
    char text[256];
    void* base;

    unsent_file(path, sizeof(path));
    snprintf(text, sizeof(text), "A %d 1 %lld %s tester\nA %d 1 %lld %s tester\n",
             ACHIEVEMENT_ID, now - 10, GAME_HASH, MEASURED_ID, now - 10, GAME_HASH);
    write_text_file(path, text);
    CHECK(ra_glue_set_unsent_path(path) == 1);
    g_mem_base = NULL;
    base = create_memory(&rdram);
    reset_fixture();
    g_refuse_award = answer == ANSWER_REFUSED;
    g_award_already_unlocked = answer == ANSWER_ALREADY_UNLOCKED;
    g_unavailable_awards = answer == ANSWER_UNAVAILABLE ? 1000 : 0;
    sign_in(0);

    CHECK(find_request("awardachievement", "a", ACHIEVEMENT_ID) >= 0);
    CHECK(g_server_errors == 0);
    if (answer == ANSWER_UNAVAILABLE) {
        CHECK(find_request("awardachievement", "a", MEASURED_ID) < 0);
        CHECK(g_unsent_events == 0);
        CHECK(unsent_entries(path, 0, -1, 0, NULL) == 2);
    } else {
        CHECK(find_request("awardachievement", "a", MEASURED_ID) >= 0);
        CHECK(g_unsent_events == 1);
        CHECK(strcmp(g_unsent_event, answer == ANSWER_REFUSED
                     ? "{\"type\":\"unsent_done\",\"recorded\":0,\"refused\":2}"
                     : "{\"type\":\"unsent_done\",\"recorded\":2,\"refused\":0}") == 0);
        CHECK(unsent_entries(path, 0, -1, 0, NULL) == 0);
    }
    end_unsent_run(path, base, name, failures_before);
}

/* Unlocks and entries that the server records or refuses during the session
 * are not kept. */
static void run_unsent_answered_in_session(void)
{
    const int failures_before = g_failures;
    uint8_t* rdram = NULL;
    char path[160];
    void* base;

    unsent_file(path, sizeof(path));
    CHECK(ra_glue_set_unsent_path(path) == 1);
    base = start_session(1, 1, 0, &rdram);
    g_refuse_entry = 1;
    rdram[ACHIEVEMENT_ADDRESS] = 1;
    rdram[LEADERBOARD_ADDRESS] = 1;
    ra_glue_do_frame();
    answer_requests();
    CHECK(g_award_requested);
    CHECK(g_server_errors == 1);
    /* Written when they were sent, and emptied when they were answered. */
    CHECK(file_exists(path));
    CHECK(unsent_entries(path, 0, -1, 0, NULL) == 0);
    CHECK(g_unsent_events == 0);
    end_unsent_run(path, base, "unlock recorded and entry refused during the session not kept",
                   failures_before);
}

static void remove_state_dir(void)
{
    static const char* const names[] = { "progress.st0", "resume.sav", "earlier.st2", "later.st3",
                                         "off.st1", "first.st4", "bare.st5", "challenge.st0" };
    char path[160];
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        progress_file(path, sizeof(path), names[i]);
        remove(path);
    }
    snprintf(path, sizeof(path), "%s/.ra-progress", g_state_dir);
    remove(path);
    unsent_file(path, sizeof(path));
    remove(path);
    snprintf(path, sizeof(path), "%s/unsent.txt.tmp", g_state_dir);
    remove(path);
    remove(g_state_dir);
}

int main(void)
{
    if (mkdtemp(g_state_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    run_unlock(1, "memory created before the game loads");
    run_unlock(0, "memory created after the game loads");
    run_refused_unlock();
    run_refused_leaderboard_entry();
    run_unreachable_server();
    run_indicators();
    run_paused();
    run_shutdown_during_retry();
    run_leaderboard_attempts();
    run_list_while_loading(0, "achievement list asked for while a frame loads the game");
    run_list_while_loading(1, "achievement list asked for while idle work loads the game");
    run_list_while_restoring();
    run_progress_with_states();
    run_progress_while_loading(0, "hit count of a state loaded while the game loads");
    run_progress_while_loading(1, "no hit count after a reset while the game loads");
    run_second_load_while_loading();
    run_save_before_game_loads();
    run_save_without_achievements();
    run_unsent_sent_again();
    run_unsent_from_file();
    run_unsent_answer(ANSWER_REFUSED, "unlocks from an earlier session refused by the server dropped");
    run_unsent_answer(ANSWER_ALREADY_UNLOCKED,
                      "unlocks from an earlier session that the server already has dropped");
    run_unsent_answer(ANSWER_UNAVAILABLE,
                      "unlock from an earlier session kept while the server is unavailable");
    run_unsent_answered_in_session();
    remove_state_dir();

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
