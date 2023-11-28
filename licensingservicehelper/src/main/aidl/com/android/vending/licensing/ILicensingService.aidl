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

import com.android.vending.licensing.ILicenseResultListener;
import com.android.vending.licensing.ILicenseV2ResultListener;

oneway interface ILicensingService {
  /**
   * Queries the licensing status for a user and a package, returns a signed
   * response containing the nonce from the server through the provided
   * callback.
   *
   * @param nonce random nonce, contained in signed response from server
   * @param packageName package name of the calling app
   * @param listener callback to return results to caller
   */
  void checkLicense(long nonce, String packageName,
        in ILicenseResultListener listener) = 0;

 /**
   * This API is currently under development.
   *
   * v2 of the checkLicense method above providing a simpler developer
   * onboarding and better offline support.
   *
   * @param packageName package name of the calling app
   * @param listener callback to return results to caller
   * @param extraParams a Bundle with optional keys for future API extensions.
   */
  void checkLicenseV2(String packageName, in ILicenseV2ResultListener listener,
        in Bundle extraParams) = 1;
}

