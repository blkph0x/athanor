/*
 * COMPILE-ONLY STUB. Not the Samsung SDK. Throws if executed.
 * Replace by vendor/knox/knoxsdk.jar (DEC-0015).
 * Spec: docs.samsungknox.com EnterpriseDeviceManager.getInstance
 */
package com.samsung.android.knox;

import android.content.Context;
import com.samsung.android.knox.restriction.RestrictionPolicy;
import com.samsung.android.knox.devicesecurity.PasswordPolicy;

public class EnterpriseDeviceManager {
    private EnterpriseDeviceManager() {}

    public static EnterpriseDeviceManager getInstance(Context context) {
        throw new UnsupportedOperationException(
                "ATN stub: drop knoxsdk.jar in vendor/knox/");
    }

    public RestrictionPolicy getRestrictionPolicy() {
        throw new UnsupportedOperationException("ATN stub");
    }

    public PasswordPolicy getPasswordPolicy() {
        throw new UnsupportedOperationException("ATN stub");
    }
}
