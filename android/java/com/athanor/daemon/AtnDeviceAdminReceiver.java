package com.athanor.daemon;

import android.app.admin.DeviceAdminReceiver;
import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

/**
 * Device admin receiver required to hold DPM/Knox policy. REQ-4.1 / 4.2.
 * Password-fail K (DEC-0017). Lab stub: watch-login → BOOM (DEC-0040).
 */
public class AtnDeviceAdminReceiver extends DeviceAdminReceiver {
    private static final String TAG = "atn-admin";

    public static ComponentName component(Context context) {
        return new ComponentName(context, AtnDeviceAdminReceiver.class);
    }

    public static boolean isAdminActive(Context context) {
        DevicePolicyManager dpm = (DevicePolicyManager)
                context.getSystemService(Context.DEVICE_POLICY_SERVICE);
        return dpm != null && dpm.isAdminActive(component(context));
    }

    public static int failedUnlockAttempts(Context context) {
        if (!isAdminActive(context)) {
            return -1;
        }
        DevicePolicyManager dpm = (DevicePolicyManager)
                context.getSystemService(Context.DEVICE_POLICY_SERVICE);
        if (dpm == null) {
            return -1;
        }
        return dpm.getCurrentFailedPasswordAttempts();
    }

    @Override
    public void onEnabled(Context context, Intent intent) {
        super.onEnabled(context, intent);
        /* DEC-0040: stub lab activates watch-login only — no USB / no
         * forced 12-char password (keeps adb + existing phone PIN). */
        if (AtnKnoxBuild.isStub()) {
            Log.i(TAG, "lab admin enabled: watch-login only (DEC-0040)");
            return;
        }
        ComponentName admin = component(context);
        boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(context);
        boolean pw = AtnKnoxPolicy.applyPasswordPolicy(context, admin);
        Log.i(TAG, "enabled usb=" + usb + " password=" + pw);
    }

    @Override
    public void onPasswordFailed(Context context, Intent intent) {
        super.onPasswordFailed(context, intent);
        DevicePolicyManager dpm = getManager(context);
        int n = dpm.getCurrentFailedPasswordAttempts();
        Log.w(TAG, "password failed count=" + n);
        AtnLabBoom.noteDeviceUnlockFail(n);

        if (AtnKnoxBuild.isStub()) {
            /* DEC-0040 lab: BOOM at K=5, keep keys (log_only soak). */
            if (n >= AtnKnoxPolicy.PASSWORD_FAIL_FLUSH) {
                AtnLabBoom.trigger("device unlock fail x" + n + " (lab)");
                Log.w(TAG, "LAB BOOM: device unlock fail x" + n);
                Intent svc = new Intent(context, AtnDaemonService.class);
                svc.setAction(AtnDaemonService.ACTION_LAB_BOOM);
                if (Build.VERSION.SDK_INT >= 26) {
                    context.startForegroundService(svc);
                } else {
                    context.startService(svc);
                }
            }
            return;
        }

        if (n >= AtnKnoxPolicy.PASSWORD_FAIL_FLUSH) {
            AtnNative.dmonFlush();
            AtnKeystore.deleteWrap(context);
            dpm.lockNow();
        }
    }

    @Override
    public void onPasswordSucceeded(Context context, Intent intent) {
        super.onPasswordSucceeded(context, intent);
        AtnLabBoom.noteDeviceUnlockFail(0);
        Log.i(TAG, "password succeeded; fail count cleared");
    }
}
