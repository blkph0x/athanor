/*
 * COMPILE-ONLY STUB (DEC-0030). Real class comes from vendor/knox/knoxsdk.jar.
 * Cited: PasswordPolicy.setBiometricAuthenticationEnabled (Knox docs).
 */
package com.samsung.android.knox.devicesecurity;

public class PasswordPolicy {
    public static final int BIOMETRIC_AUTHENTICATION_FINGERPRINT = 1;
    public static final int BIOMETRIC_AUTHENTICATION_IRIS = 2;
    public static final int BIOMETRIC_AUTHENTICATION_FACE = 4;

    public boolean setBiometricAuthenticationEnabled(int type, boolean enable) {
        throw new UnsupportedOperationException("ATN stub");
    }
    public boolean isBiometricAuthenticationEnabled(int type) {
        throw new UnsupportedOperationException("ATN stub");
    }
}
