package com.athanor.daemon;

import android.app.admin.DeviceAdminReceiver;
import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Device admin receiver required to hold DPM/Knox policy. REQ-4.1 / 4.2.
 * Password-fail K (DEC-0017) flushes RAM + wrap file and lockNow().
 */
public class AtnDeviceAdminReceiver extends DeviceAdminReceiver {
    private static final String TAG = "atn-admin";

    @Override
    public void onEnabled(Context context, Intent intent) {
        super.onEnabled(context, intent);
        ComponentName admin = new ComponentName(context, AtnDeviceAdminReceiver.class);
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
        if (n >= AtnKnoxPolicy.PASSWORD_FAIL_FLUSH) {
            AtnNative.dmonFlush();
            AtnKeystore.deleteWrap(context);
            dpm.lockNow();
        }
    }
}
