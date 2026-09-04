package com.athanor.daemon;

import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.util.Log;

import com.samsung.android.knox.EnterpriseDeviceManager;
import com.samsung.android.knox.devicesecurity.PasswordPolicy;
import com.samsung.android.knox.restriction.RestrictionPolicy;

/**
 * Apply cited Knox/DPM policies. REQ-4.2 / 4.3. Spec: docs/KNOX.md, DEC-0015,
 * DEC-0017. Stub jar throws UnsupportedOperationException — caller must not
 * pretend the policy stuck.
 */
public final class AtnKnoxPolicy {
    private static final String TAG = "atn-knox";
    public static final int PASSWORD_MIN = 12;
    /* DEC-0017: K=5 matches ATN_2FA_FAIL_MAX. Flush + lockNow, no factory wipe. */
    public static final int PASSWORD_FAIL_FLUSH = 5;

    private AtnKnoxPolicy() {}

    public static boolean applyUsbChargeOnly(Context ctx) {
        try {
            RestrictionPolicy rp = EnterpriseDeviceManager.getInstance(ctx)
                    .getRestrictionPolicy();
            boolean ok = true;
            ok &= rp.setUsbMediaPlayerAvailability(false);
            ok &= rp.setUsbDebuggingEnabled(false);
            ok &= rp.allowUsbHostStorage(false);
            ok &= rp.setUsbTethering(false);
            return ok;
        } catch (SecurityException e) {
            Log.w(TAG, "USB policy SecurityException", e);
            return false;
        } catch (UnsupportedOperationException e) {
            Log.w(TAG, "USB policy: knoxsdk.jar not installed");
            return false;
        }
    }

    public static boolean applyPasswordPolicy(Context ctx, ComponentName admin) {
        DevicePolicyManager dpm =
                (DevicePolicyManager) ctx.getSystemService(Context.DEVICE_POLICY_SERVICE);
        if (dpm == null) {
            return false;
        }
        try {
            dpm.setPasswordQuality(admin,
                    DevicePolicyManager.PASSWORD_QUALITY_ALPHANUMERIC);
            dpm.setPasswordMinimumLength(admin, PASSWORD_MIN);
            /* AOSP DPM; Knox DA-deprecation page lists these as the mirrored APIs. */
            dpm.setPasswordMinimumLetters(admin, 1);
            dpm.setPasswordMinimumNumeric(admin, 1);
        } catch (SecurityException e) {
            Log.w(TAG, "DPM password SecurityException", e);
            return false;
        }
        try {
            PasswordPolicy pp = EnterpriseDeviceManager.getInstance(ctx)
                    .getPasswordPolicy();
            /* Convenience biometric only after quality is set (Knox docs). */
            pp.setBiometricAuthenticationEnabled(
                    PasswordPolicy.BIOMETRIC_AUTHENTICATION_FINGERPRINT
                            | PasswordPolicy.BIOMETRIC_AUTHENTICATION_IRIS,
                    true);
        } catch (UnsupportedOperationException e) {
            Log.w(TAG, "PasswordPolicy: knoxsdk.jar not installed");
            return false;
        } catch (SecurityException e) {
            Log.w(TAG, "PasswordPolicy SecurityException", e);
            return false;
        }
        return true;
    }
}
