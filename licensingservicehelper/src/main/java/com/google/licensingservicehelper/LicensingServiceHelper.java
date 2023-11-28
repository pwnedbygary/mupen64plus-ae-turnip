// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
package com.google.licensingservicehelper;

import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentSender;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.Bundle;
import android.util.Base64;
import android.util.Log;
import com.android.vending.licensing.ILicenseV2ResultListener;
import com.android.vending.licensing.ILicensingService;
import com.google.api.client.json.jackson2.JacksonFactory;
import com.google.api.client.json.webtoken.JsonWebSignature;
import java.security.KeyFactory;
import java.security.NoSuchAlgorithmException;
import java.security.PublicKey;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.X509EncodedKeySpec;

/**
 * Client library for Google Play licensing service v2.
 * <p>
 * Callers mus provide the Base64-encoded RSA public key associated with your developer account. The
 * public key is obtainable from the developer console.
 */
public class LicensingServiceHelper {

    private static final String TAG = "LicensingServiceHelper";

    private final Context context;
    private final String publicKey;
    private LicensingServiceCallback callback;

    private ILicensingService licensingService;
    private ServiceConnection serviceConnection =
            new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder binder) {
                    licensingService = ILicensingService.Stub.asInterface(binder);
                    Log.d(TAG, "Service connected");
                    callLicensingService();
                }
                @Override
                public void onServiceDisconnected(ComponentName name) {
                    licensingService = null;
                    Log.d(TAG, "Service disconnected");
                }
            };

    public LicensingServiceHelper(Context context, String publicKey) {
        this.context = context;
        this.publicKey = publicKey;
    }

    /**
     * Calls licensing service after connecting to service if necessary.
     *
     * If an exception is thrown during the call, the
     * {@link LicensingServiceCallback#applicationError(String)} method will be called.
     *
     * @param callback callback to provide results of licensing check to the calling code
     */
    public void checkLicense(LicensingServiceCallback callback) {
        this.callback = callback;
        if (licensingService == null) {
            // Bind to service, callLicensingService() will get called once service is connected.
            Intent intent = new Intent(ILicensingService.class.getName());
            intent.setPackage("com.android.vending");
            boolean ret = context.bindService(intent, serviceConnection, Service.BIND_AUTO_CREATE);
            Log.d(TAG, "service bound with " + ret);
        } else {
            callLicensingService();
        }
    }

    /**
     * Triggers pending intent, provided as a helper method for clients of class to trigger the
     * intent returned in the dontAllow callback.
     *
     * @param pendingIntent intent provided in {@link LicensingServiceCallback#dontAllow(PendingIntent)}
     * @throws IntentSender.SendIntentException
     */
    public void showPaywall(PendingIntent pendingIntent) throws IntentSender.SendIntentException {
        context.startIntentSender(
            pendingIntent.getIntentSender(),
            /* fillInIntent= */ null,
            /* flagsMask= */ 0,
            /* flagsValue= */ 0,
            /* extraFlags= */ 0);
    }

    /**
     * Sends request to licensing service and provides listener for processing response.
     */
    private void callLicensingService() {
        try {
            licensingService.checkLicenseV2(
                context.getPackageName(),
                new ILicenseV2ResultListener.Stub() {
                    @Override
                    public void verifyLicense(final int responseCode, final Bundle responseBundle) {
                        Log.d(TAG, String.format("responseCode: %d", responseCode));

                        if (responseCode == 0) {
                            // LICENSED response, so verify the payload was signed correctly.
                            String jwsString = responseBundle.getString("LICENSE_DATA");
                            try {
                                String payloadJson =
                                        LicensingServiceHelper.parseAndVerify(publicKey, jwsString);
                                callback.allow(payloadJson);
                            } catch (Exception e) {
                                Log.e(TAG, e.getMessage());
                                callback.applicationError(
                                        "Error verifying payload response signature");
                            }
                        } else if (responseCode == 1) {
                            // LICENSED_WITH_NONCE response is unimplemented but was included in
                            // response set to allow for future support of nonced requests.
                            callback.applicationError(
                                    "Unsupported response code (LICENSED_WITH_NONCE)");
                        } else if (responseCode == 2) {
                            PendingIntent paywallIntent =
                                    responseBundle.getParcelable("PAYWALL_INTENT");
                            callback.dontAllow(paywallIntent);
                        } else if (responseCode == 3) {
                            callback.applicationError(
                                    "Application uid doesn't match uid of requester");
                        } else if (responseCode == 4) {
                            callback.applicationError(
                                    "Requested package not found on device");
                        } else {
                            callback.applicationError(
                                    String.format("Unknown response code: %d", responseCode));
                        }
                    }
                },
                new Bundle());
        } catch (RemoteException e) {
            Log.e(TAG, "RemoteException in checkLicenseV2 call.", e);
            callback.applicationError("RemoteException in checkLicenseV2 call");
        }
    }

    /**
     * Dispose of resources used by class like the service connection.
     */
    public void onDestroy() {
        try {
            if (context != null) {
                context.unbindService(serviceConnection);
            }
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Unable to unbind from licensing service (already unbound)");
        }
        licensingService = null;
        Log.d(TAG, "Destroyed LicenseServiceHelper");
    }

    /**
     * Verifies the signature of the JSON web token (JWT) license data in the response {@link Bundle}
     * and extracts the JSON payload if valid.
     *
     * @param jwtString LICENSE_DATA field from the response {@link Bundle}
     * @return JSON payload from the JWT
     */
    private static String parseAndVerify(String publicKeyString, String jwtString) throws Exception {
        JsonWebSignature jws =
                JsonWebSignature.parser(JacksonFactory.getDefaultInstance()).parse(jwtString);
        PublicKey publicKey = generatePublicKey(publicKeyString);
        boolean verified = jws.verifySignature(publicKey);
        if (!verified) {
            throw new IllegalArgumentException("JWS verification failed");
        }
        Log.i(TAG, "JWS verification succeeded");
        return jws.getPayload().toPrettyString();
    }
    /**
     * Generates a PublicKey instance from a string containing the Base64-encoded public key.
     *
     * @throws IllegalArgumentException if encodedPublicKey is invalid
     */
    private static PublicKey generatePublicKey(String publicKeyString) {
        try {
            byte[] decodedKey = Base64.decode(publicKeyString,/* flags= */ 0);
            KeyFactory keyFactory = KeyFactory.getInstance("RSA");
            return keyFactory.generatePublic(new X509EncodedKeySpec(decodedKey));
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException(e);
        } catch (InvalidKeySpecException e) {
            Log.e(TAG, "Invalid key specification.");
            throw new IllegalArgumentException(e);
        }
    }
}