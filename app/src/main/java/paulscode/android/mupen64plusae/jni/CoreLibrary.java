package paulscode.android.mupen64plusae.jni;

import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.Pointer;

/**
 * Core library
 */
@SuppressWarnings({"unused", "UnusedReturnValue"})
public interface CoreLibrary extends Library {

    int coreAPIVersion = 0x20001;

    interface DebugCallback extends Callback {
        void invoke(Pointer Context, int level, String message);
    }

    interface StateCallback extends Callback {
        void invoke(Pointer Context, int param_type, int new_value);
    }

    /* CoreStartup()
     *
     * This function initializes libmupen64plus for use by allocating memory,
     * creating data structures, and loading the configuration file.
     */
    int CoreStartup(int APIVersion, String ConfigPath, String DataPath, Pointer Context, DebugCallback debugCallBack,
                    Pointer Context2, StateCallback stateCallback);

    /* CoreShutdown()
     *
     * This function saves the configuration file, then destroys data structures
     * and releases memory allocated by the core library.
     */
    int CoreShutdown();

    /* CoreAttachPlugin()
     *
     * This function attaches the given plugin to the emulator core. There can only
     * be one plugin of each type attached to the core at any given time.
     */
    int CoreAttachPlugin(int PluginType, Pointer PluginLibHandle);

    /* CoreDetachPlugin()
     *
     * This function detaches the given plugin from the emulator core, and re-attaches
     * the 'dummy' plugin functions.
     */
    int CoreDetachPlugin(int PluginType);

    /* CoreDoCommand()
     *
     * This function sends a command to the emulator core.
     */
    int CoreDoCommand(int Command, int ParamInt, Pointer ParamPtr);

    /* CoreAddCheat()
     *
     * This function will add a Cheat Function to a list of currently active cheats
     * which are applied to the open ROM.
     */
    int CoreAddCheat(String CheatName, CoreTypes.m64p_cheat_code[] CodeList, int NumCodes);

    /* CoreCheatEnabled()
     *
     * This function will enable or disable a Cheat Function which is in the list of
     * currently active cheats.
     */
    int CoreCheatEnabled(String CheatName, int Enabled);

    /* ---- RetroAchievements (rcheevos glue, see api/ra_glue.h) ----
     *
     * These symbols are exported by the core .so (api_export.ver). The bridge
     * callbacks are JNA trampolines whose function pointers are handed to the
     * native side via ra_glue_set_java_bridge(). Keep references to the callback
     * instances alive for as long as the glue client lives, or JNA may collect
     * the trampoline and the native side will call freed code.
     */

    interface RaServerCallCallback extends Callback {
        void invoke(int requestId, String url, String postData, String contentType);
    }

    interface RaEventCallback extends Callback {
        void invoke(String json);
    }

    interface RaAsyncResultCallback extends Callback {
        void invoke(int result, String errorMessage);
    }

    int ra_glue_create();

    void ra_glue_shutdown();

    void ra_glue_set_java_bridge(RaServerCallCallback serverCall, RaEventCallback event,
                                 RaAsyncResultCallback asyncResult);

    void ra_glue_do_frame();

    void ra_glue_idle();

    void ra_glue_reset();

    void ra_glue_set_hardcore(int enabled);

    int ra_glue_get_hardcore();

    int ra_glue_login_password(String username, String password);

    int ra_glue_login_token(String username, String token);

    int ra_glue_logout();

    int ra_glue_load_game(String md5);

    int ra_glue_is_logged_in();

    int ra_glue_is_game_loaded();

    int ra_glue_get_load_game_state();

    int ra_glue_get_user_name(byte[] buffer, int bufferSize);

    int ra_glue_get_game_name(byte[] buffer, int bufferSize);

    int ra_glue_get_summary_json(byte[] buffer, int bufferSize);

    int ra_glue_get_achievements_json(byte[] buffer, int bufferSize);

    int ra_glue_serialize_progress(byte[] buffer, int bufferSize);

    int ra_glue_deserialize_progress(byte[] buffer, int bufferSize);

    void ra_glue_dispatch_server_response(int requestId, byte[] body, int bodyLength, int httpStatus);

    String ra_glue_get_last_error();
}
