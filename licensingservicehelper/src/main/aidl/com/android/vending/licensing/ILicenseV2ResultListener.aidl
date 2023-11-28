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

package com.android.vending.licensing;

/**
 * Callback interface used to asynchronously notify calling app about licensing
 * results.
 */
oneway interface ILicenseV2ResultListener {
    /**
     * Callback that is used to provide the licensing result to the calling app.
     * Response consists of a response code indicating whether the user is
     * licensed to use the app along with a payload Bundle. Depending on the
     * response code, the bundle can contain signed license data or a pending
     * intent that developers can use to direct users to a paywall page for
     * regaining access.
     *
     * Response code set:
     *  LICENSED = 0 - bundle contains signed data without a nonce
     *  LICENSED_WITH_NONCE = 1 - bundle contains signed data with a nonce
     *  NOT_LICENSED = 2  - bundle contains pending intent to a paywall
     *  ERROR_UID_MISMATCH = 3 - application uid doesn't match uid of requestor
     *  ERROR_PACKAGE_NAME_NOT_FOUND = 4 - requested package not found on device
     *
     * If the responseCode is LICENSED, the returned Bundle will contain
     * additional information for the developer to use to verify the response:
     * LICENSE_DATA
     *     JSON Web Signature (https://tools.ietf.org/html/rfc7515) string with
     *     the following claims in the JSON payload:
     *       "packageName" (string, ex. "com.example.app")
     *       "userId" (string, ex. "ANlOHQPVHWc0mN6y1uYjSTnUw==")
     *       "iat" (long, ex. 1559944254, issued at time in seconds since epoch)
     *
     * If the responseCode is NOT_LICENSED, the returned Bundle will include a
     * pending intent that can be used for winback purposes.
     * PAYWALL_INTENT
     *     PendingIntent specifying an intent that can be started with
     *     Activity#startIntentSender to take the user to a paywall page that
     *     enables them to restore access.
     */
    void verifyLicense(int responseCode, in Bundle responsePayload) = 0;
}
