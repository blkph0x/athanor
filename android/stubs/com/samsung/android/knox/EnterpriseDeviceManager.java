/*
 * COMPILE-ONLY STUB (DEC-0015 / 0030). Not the Samsung SDK.
 *
 * Same package/class names as knoxsdk.jar so product Java keeps real imports.
 * Drop the Partner jar at: vendor/knox/knoxsdk.jar
 * Then: make android-java  (Makefile omits this file; uses -classpath jar)
 *
 * Spec citation for the real API: Samsung Knox docs —
 * EnterpriseDeviceManager.getInstance(Context).
 */
package com.samsung.android.knox;

import android.content.Context;
import com.samsung.android.knox.restriction.RestrictionPolicy;
import com.samsung.android.knox.devicesecurity.PasswordPolicy;

public class EnterpriseDeviceManager {
    /* Marker the real knoxsdk.jar does not have. Daemon fail-closes. */
    public static final boolean ATN_STUB = true;

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
