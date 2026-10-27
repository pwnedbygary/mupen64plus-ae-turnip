package paulscode.android.mupen64plusae.jni;

/** Which saved credential a RetroAchievements sign-in uses, and which one it keeps afterwards. */
public final class RaSignInRules {

    public enum Credential { TOKEN, PASSWORD, NONE }

    // Must match rc_error.h. The server answers a token that does not match the user name with
    // RC_INVALID_CREDENTIALS, and an expired token, which it replaces, with RC_EXPIRED_TOKEN.
    private static final int RC_INVALID_CREDENTIALS = -34;
    private static final int RC_EXPIRED_TOKEN = -35;

    private RaSignInRules() {
    }

    /**
     * Picks the credential to sign in with first: the session token saved by an earlier sign-in,
     * or the password when no token is saved.
     *
     * @param username the saved user name
     * @param password the saved password
     * @param token the saved session token
     * @return the credential to try first, or NONE if the user name or both credentials are
     * missing.
     */
    static Credential firstCredential(String username, String password, String token) {
        if (isEmpty(username)) {
            return Credential.NONE;
        }
        if (!isEmpty(token)) {
            return Credential.TOKEN;
        }
        return isEmpty(password) ? Credential.NONE : Credential.PASSWORD;
    }

    /**
     * Picks the credential to try after a failed sign-in. Only a token the server rejected is
     * retried with the password: a network failure, or an account the server refuses whatever
     * the credential, would fail the same way again.
     *
     * @param failed the credential of the sign-in that failed
     * @param result the rc_client result code of that sign-in
     * @param password the saved password
     * @return the credential to try next, or NONE if signing in has failed for this session.
     */
    static Credential nextCredential(Credential failed, int result, String password) {
        return tokenRejected(failed, result) && !isEmpty(password)
                ? Credential.PASSWORD : Credential.NONE;
    }

    /**
     * Whether the server rejected the saved session token, which only a sign-in with the password
     * replaces.
     *
     * @param failed the credential of the sign-in that failed
     * @param result the rc_client result code of that sign-in
     */
    public static boolean tokenRejected(Credential failed, int result) {
        return failed == Credential.TOKEN
                && (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN);
    }

    /**
     * Picks the credential for the Connect check in Settings: the saved password, as Connect is
     * there to check what was typed in Settings, or the session token when no password is saved.
     *
     * @param username the saved user name
     * @param password the saved password
     * @param token the saved session token
     * @return the credential to sign in with, or NONE if the user name or both credentials are
     * missing.
     */
    public static Credential connectCredential(String username, String password, String token) {
        if (isEmpty(username)) {
            return Credential.NONE;
        }
        if (!isEmpty(password)) {
            return Credential.PASSWORD;
        }
        return isEmpty(token) ? Credential.NONE : Credential.TOKEN;
    }

    /**
     * Whether a successful sign-in deletes the saved password. Only a sign-in that returned a
     * session token does: later sign-ins use that token, and need the password again only if the
     * server rejects it.
     *
     * @param newToken the session token the sign-in returned
     * @param password the saved password
     */
    public static boolean forgetsPassword(String newToken, String password) {
        return !isEmpty(newToken) && !isEmpty(password);
    }

    private static boolean isEmpty(String value) {
        return value == null || value.isEmpty();
    }
}
