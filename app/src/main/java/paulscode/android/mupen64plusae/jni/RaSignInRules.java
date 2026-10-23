package paulscode.android.mupen64plusae.jni;

/** Which saved credential a game session signs in to RetroAchievements with. */
final class RaSignInRules {

    enum Credential { TOKEN, PASSWORD, NONE }

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
        boolean tokenRejected = failed == Credential.TOKEN
                && (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN);
        return tokenRejected && !isEmpty(password) ? Credential.PASSWORD : Credential.NONE;
    }

    private static boolean isEmpty(String value) {
        return value == null || value.isEmpty();
    }
}
