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
        /* DEC-0046: USB/data attach → re-assert org policy (biometric off, etc.). */
        AtnOrgPolicy.reassert(context);
        if (AtnKnoxBuild.isStub()) {
            Log.i(TAG, "power connected: stub reasserted timers; USB charge-only waits jar");
            return;
        }
        AtnOrgPolicy pol = AtnOrgPolicy.loadWrapped(context);
        if (pol != null && pol.usbDataBlock == 1) {
            boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(context);
            Log.i(TAG, "power connected usb_block=" + usb);
        }
    }
}
