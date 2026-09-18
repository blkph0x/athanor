package com.athanor.daemon;

import android.content.Context;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.provider.Settings;
import android.util.Log;

import java.util.HashMap;

/**
 * USB / ADB posture (DEC-0056). Detect always; enforce enroll-block and
 * runtime BOOM only when wipe_armed=1 and the matching policy flags are on.
 * Stub does not fake Knox charge-only — best-effort USB data hint only.
 */
public final class AtnUsbPosture {
    private static final String TAG = "atn-usb-posture";
    private static boolean lastAdb = false;
    private static boolean lastUsb = false;
    private static boolean loggedOnce;

    public boolean adbOn;
    public boolean usbDataLikely;
    public boolean chargeOnlyKnown;
    public boolean chargeOnlyOk;
    public String detail = "";

    private AtnUsbPosture() {}

    public static AtnUsbPosture check(Context ctx) {
        AtnUsbPosture p = new AtnUsbPosture();
        if (ctx == null) {
            p.detail = "no context";
            return p;
        }
        try {
            p.adbOn = Settings.Global.getInt(ctx.getContentResolver(),
                    Settings.Global.ADB_ENABLED, 0) == 1;
        } catch (Exception e) {
            /* Fail closed for kill gates: treat as ADB on if unreadable. */
            p.adbOn = true;
            p.detail = "adb read fail";
            Log.w(TAG, "ADB_ENABLED read failed", e);
        }
        try {
            UsbManager um = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
            if (um != null) {
                HashMap<String, UsbDevice> map = um.getDeviceList();
                p.usbDataLikely = map != null && !map.isEmpty();
            }
        } catch (Exception e) {
            Log.w(TAG, "UsbManager probe failed", e);
        }
        if (!AtnKnoxBuild.isStub()) {
            p.chargeOnlyKnown = true;
            p.chargeOnlyOk = !p.adbOn && !p.usbDataLikely;
        } else {
            p.chargeOnlyKnown = false;
            p.chargeOnlyOk = !p.adbOn;
            if (p.detail.length() == 0) {
                p.detail = "stub: charge-only best-effort (no Knox)";
            }
        }
        if (!loggedOnce || p.adbOn != lastAdb || p.usbDataLikely != lastUsb) {
            Log.i(TAG, "adbOn=" + p.adbOn + " usbDataLikely=" + p.usbDataLikely
                    + " chargeOnlyOk=" + p.chargeOnlyOk + " " + p.detail);
            lastAdb = p.adbOn;
            lastUsb = p.usbDataLikely;
            loggedOnce = true;
        }
        return p;
    }

    /**
     * True if posture violates kill-mode USB policy.
     * No breach when wipe_armed=0 (lab/test).
     */
    public boolean breachesKillPolicy(AtnOrgPolicy pol) {
        if (pol == null || pol.wipeArmed != 1) {
            return false;
        }
        if (pol.requireAdbOff == 1 && adbOn) {
            return true;
        }
        if (pol.requireUsbChargeOnly == 1) {
            if (chargeOnlyKnown && !chargeOnlyOk) {
                return true;
            }
            /* Stub / unknown: ADB on implies data path — breach. */
            if (adbOn) {
                return true;
            }
        }
        return false;
    }

    /** Runtime BOOM only when boom_on_usb_breach + wipe_armed + breach. */
    public boolean shouldBoom(AtnOrgPolicy pol) {
        if (pol == null || pol.wipeArmed != 1 || pol.boomOnUsbBreach != 1) {
            return false;
        }
        return breachesKillPolicy(pol);
    }

    public String boomReason(AtnOrgPolicy pol) {
        if (pol != null && pol.requireAdbOff == 1 && adbOn) {
            return "USB posture: ADB enabled (kill policy require_adb_off)";
        }
        if (pol != null && pol.requireUsbChargeOnly == 1) {
            return "USB posture: not charge-only (kill policy)";
        }
        return "USB posture breach (kill policy)";
    }
}
