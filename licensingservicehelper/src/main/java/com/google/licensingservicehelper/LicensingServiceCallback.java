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

/**
 * Callback for returning licensing service v2 results to callers.
 */
public interface LicensingServiceCallback {
    /**
     * User is entitled to access the app
     *
     * @param payloadJson JSON response payload from the service
     */
    public void allow(String payloadJson);

    /**
     * User is not entitled to access the app
     *
     * @param paywallIntent intent to Play store paywall that can be launched
     *                      via {@link LicensingServiceHelper#showPaywall(PendingIntent)}
     */
    public void dontAllow(PendingIntent paywallIntent);

    /**
     * Error in application code
     *
     * @param errorMessage describes the problem
     */
    public void applicationError(String errorMessage);
}
