package paulscode.android.mupen64plusae.jni;

import org.junit.Test;

import paulscode.android.mupen64plusae.jni.RaSignInRules.Credential;

import static org.junit.Assert.assertEquals;

public class RaSignInRulesTest {

    // Result codes from rcheevos include/rc_error.h.
    private static final int RC_NO_RESPONSE = -32;
    private static final int RC_ACCESS_DENIED = -33;
    private static final int RC_INVALID_CREDENTIALS = -34;
    private static final int RC_EXPIRED_TOKEN = -35;

    @Test
    public void savedTokenGoesFirst() {
        assertEquals(Credential.TOKEN, RaSignInRules.firstCredential("user", "password", "token"));
        assertEquals(Credential.TOKEN, RaSignInRules.firstCredential("user", "", "token"));
    }

    @Test
    public void passwordGoesFirstWithoutSavedToken() {
        assertEquals(Credential.PASSWORD, RaSignInRules.firstCredential("user", "password", ""));
        assertEquals(Credential.PASSWORD, RaSignInRules.firstCredential("user", "password", null));
    }

    @Test
    public void missingCredentialsStopTheSignIn() {
        assertEquals(Credential.NONE, RaSignInRules.firstCredential("", "password", "token"));
        assertEquals(Credential.NONE, RaSignInRules.firstCredential(null, "password", "token"));
        assertEquals(Credential.NONE, RaSignInRules.firstCredential("user", "", ""));
    }

    @Test
    public void rejectedTokenIsRetriedWithPassword() {
        assertEquals(Credential.PASSWORD,
                RaSignInRules.nextCredential(Credential.TOKEN, RC_INVALID_CREDENTIALS, "password"));
        assertEquals(Credential.PASSWORD,
                RaSignInRules.nextCredential(Credential.TOKEN, RC_EXPIRED_TOKEN, "password"));
    }

    @Test
    public void otherFailuresEndTheSignIn() {
        assertEquals(Credential.NONE,
                RaSignInRules.nextCredential(Credential.TOKEN, RC_NO_RESPONSE, "password"));
        assertEquals(Credential.NONE,
                RaSignInRules.nextCredential(Credential.TOKEN, RC_ACCESS_DENIED, "password"));
        assertEquals(Credential.NONE,
                RaSignInRules.nextCredential(Credential.TOKEN, RC_INVALID_CREDENTIALS, ""));
        assertEquals(Credential.NONE,
                RaSignInRules.nextCredential(Credential.PASSWORD, RC_INVALID_CREDENTIALS, "password"));
    }
}
