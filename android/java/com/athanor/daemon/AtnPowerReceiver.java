package com.athanor.daemon;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Re-assert USB charge-only when a power source attaches. REQ-4.3
 * cause/effect: policy on boot and on USB connect. ACTION_POWER_CONNECTED
 * is the documented Android intent for charger/USB attach.
 */
public class AtnPowerReceiver extends BroadcastReceiver {
    private static final String TAG = "atn-pwr";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) {
            return;
        }
        if (!Intent.ACTION_POWER_CONNECTED.equals(intent.getAction())) {
            return;
        }
        /* DEC-0038: never assert USB charge-only on stub lab (keeps adb). */
        if (AtnKnoxBuild.isStub()) {
            Log.i(TAG, "power connected: lab stub skips USB policy");
            return;
        }
        boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(context);
        Log.i(TAG, "power connected usb=" + usb);
    }
}
