package com.athanor.daemon;

import android.content.ComponentName;
import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Arrays;

/**
 * Org policy apply + Keystore-wrapped persistence (DEC-0045 / DEC-0046).
 * Plaintext exists only during parse/apply; then wiped. Disk = AES-GCM blob.
 */
public final class AtnOrgPolicy {
    private static final String TAG = "atn-org-pol";
    public static final String WRAP_FILE = "atn-policy.bin";

    public int ver;
    public int diag;
    public int flushMode;
    public int wipeArmed;
    public int outageClass;
    public int boomSilenceS = 30;
    public int passwordFailMax = 5;
    public int biometricAllowed; /* 0 = off */
    public int passwordMinLen = 12;
    public int usbDataBlock = 1;
    public int pwdDenyCheck = 1;

    private AtnOrgPolicy() {}

    public static AtnOrgPolicy defaults() {
        return new AtnOrgPolicy();
    }

    /** Parse key=value body (no leading 'P'). Fail-closed on unknown keys. */
    public static AtnOrgPolicy parse(String text) {
        if (text == null) {
            return null;
        }
        AtnOrgPolicy p = defaults();
        String[] lines = text.split("\n");
        for (int i = 0; i < lines.length; i++) {
            String line = lines[i].trim();
            if (line.length() == 0 || line.charAt(0) == '#') {
                continue;
            }
            int eq = line.indexOf('=');
            if (eq <= 0) {
                return null;
            }
            String k = line.substring(0, eq).trim();
            String v = line.substring(eq + 1).trim();
            try {
                if ("policy_ver".equals(k)) {
                    p.ver = Integer.parseInt(v);
                } else if ("diag".equals(k)) {
                    p.diag = oneBit(v);
                } else if ("flush_mode".equals(k)) {
                    if ("zeroize".equals(v)) {
                        p.flushMode = 0;
                    } else if ("log_only".equals(v)) {
                        p.flushMode = 1;
                    } else {
                        return null;
                    }
                } else if ("wipe_armed".equals(k)) {
                    p.wipeArmed = oneBit(v);
                } else if ("outage_class".equals(k)) {
                    p.outageClass = outage(v);
                    if (p.outageClass < 0) {
                        return null;
                    }
                } else if ("boom_silence_s".equals(k)) {
                    int s = Integer.parseInt(v);
                    if (s < 5 || s > 86400) {
                        return null;
                    }
                    p.boomSilenceS = s;
                } else if ("password_fail_max".equals(k)) {
                    int n = Integer.parseInt(v);
                    if (n < 1 || n > 20) {
                        return null;
                    }
                    p.passwordFailMax = n;
                } else if ("biometric_allowed".equals(k)) {
                    p.biometricAllowed = oneBit(v);
                } else if ("password_min_len".equals(k)) {
                    int n = Integer.parseInt(v);
                    if (n < 8 || n > 64) {
                        return null;
                    }
                    p.passwordMinLen = n;
                } else if ("usb_data_block".equals(k)) {
                    p.usbDataBlock = oneBit(v);
                } else if ("pwd_deny_check".equals(k)) {
                    p.pwdDenyCheck = oneBit(v);
                } else {
                    return null;
                }
            } catch (NumberFormatException e) {
                return null;
            }
        }
        if (p.flushMode == 1 && p.diag != 1) {
            return null;
        }
        return p;
    }

    private static int oneBit(String v) {
        if ("0".equals(v)) {
            return 0;
        }
        if ("1".equals(v)) {
            return 1;
        }
        throw new NumberFormatException("bit");
    }

    private static int outage(String v) {
        if ("normal".equals(v)) {
            return 0;
        }
        if ("maintenance".equals(v)) {
            return 1;
        }
        if ("blackout".equals(v)) {
            return 2;
        }
        if ("faraday".equals(v)) {
            return 3;
        }
        if ("capture".equals(v)) {
            return 4;
        }
        return -1;
    }

    public String encode() {
        String flush = flushMode == 1 ? "log_only" : "zeroize";
        String outage;
        switch (outageClass) {
            case 1:
                outage = "maintenance";
                break;
            case 2:
                outage = "blackout";
                break;
            case 3:
                outage = "faraday";
                break;
            case 4:
                outage = "capture";
                break;
            default:
                outage = "normal";
                break;
        }
        return "policy_ver=" + ver + "\n"
                + "diag=" + diag + "\n"
                + "flush_mode=" + flush + "\n"
                + "wipe_armed=" + wipeArmed + "\n"
                + "outage_class=" + outage + "\n"
                + "boom_silence_s=" + boomSilenceS + "\n"
                + "password_fail_max=" + passwordFailMax + "\n"
                + "biometric_allowed=" + biometricAllowed + "\n"
                + "password_min_len=" + passwordMinLen + "\n"
                + "usb_data_block=" + usbDataBlock + "\n"
                + "pwd_deny_check=" + pwdDenyCheck + "\n";
    }

    /**
     * Apply to native dmon + lab boom timers + Knox/DPM. Wipes encode buffer.
     */
    public void apply(Context ctx) {
        int rc = AtnNative.dmonSetPolicy(diag, flushMode, wipeArmed, outageClass);
        AtnLabBoom.setPolicyTimers(boomSilenceS * 1000L, passwordFailMax);
        Log.i(TAG, "applied ver=" + ver + " dmon=" + rc
                + " boom_s=" + boomSilenceS + " fail_k=" + passwordFailMax
                + " bio=" + biometricAllowed + " usb_block=" + usbDataBlock);
        if (ctx != null) {
            AtnPwdDeny.ensureLoaded(ctx);
            ComponentName admin = AtnDeviceAdminReceiver.component(ctx);
            if (!AtnKnoxBuild.isStub() && AtnDeviceAdminReceiver.isAdminActive(ctx)) {
                AtnKnoxPolicy.applyOrgDeviceLock(ctx, admin, this);
                if (usbDataBlock == 1) {
                    AtnKnoxPolicy.applyUsbChargeOnly(ctx);
                }
            } else if (AtnKnoxBuild.isStub()) {
                Log.i(TAG, "stub: device-lock/USB assert deferred (T-0400)");
            }
        }
    }

    public static boolean persistWrapped(Context ctx, AtnOrgPolicy p) {
        if (ctx == null || p == null) {
            return false;
        }
        byte[] pt = null;
        byte[] blob = null;
        try {
            pt = p.encode().getBytes("UTF-8");
            blob = AtnKeystore.wrapData(pt);
            if (blob == null) {
                return false;
            }
            FileOutputStream out = ctx.openFileOutput(WRAP_FILE, Context.MODE_PRIVATE);
            out.write(blob);
            out.close();
            /* remove legacy plaintext if present */
            File legacy = new File(ctx.getFilesDir(), "atn-policy.conf");
            if (legacy.isFile()) {
                //noinspection ResultOfMethodCallIgnored
                legacy.delete();
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "persistWrapped failed", e);
            return false;
        } finally {
            AtnPwdDeny.wipe(pt);
            AtnPwdDeny.wipe(blob);
        }
    }

    public static AtnOrgPolicy loadWrapped(Context ctx) {
        if (ctx == null) {
            return null;
        }
        FileInputStream in = null;
        byte[] blob = null;
        byte[] pt = null;
        try {
            File f = new File(ctx.getFilesDir(), WRAP_FILE);
            if (!f.isFile() || f.length() <= 12L || f.length() > 8192L) {
                return null;
            }
            blob = new byte[(int) f.length()];
            in = new FileInputStream(f);
            if (in.read(blob) != blob.length) {
                return null;
            }
            pt = AtnKeystore.unwrapData(blob);
            if (pt == null) {
                Log.w(TAG, "unwrap failed — possible tamper");
                return null;
            }
            String text = new String(pt, "UTF-8");
            return parse(text);
        } catch (Exception e) {
            Log.w(TAG, "loadWrapped", e);
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception e) {
                    /* ignore */
                }
            }
            AtnPwdDeny.wipe(blob);
            AtnPwdDeny.wipe(pt);
        }
    }

    /** Hub wire 'P'+body → apply + wrap. Wipes message copy internals via parse. */
    public static boolean applyFromWire(Context ctx, byte[] msg, int n) {
        if (msg == null || n < 2 || msg[0] != (byte) 'P') {
            return false;
        }
        if (n == 2 && msg[1] == (byte) '?') {
            return false;
        }
        byte[] body = new byte[n - 1];
        try {
            System.arraycopy(msg, 1, body, 0, n - 1);
            String text = new String(body, "UTF-8");
            AtnOrgPolicy p = parse(text);
            if (p == null) {
                Log.w(TAG, "org policy parse fail (tamper/unknown key)");
                return false;
            }
            p.apply(ctx);
            persistWrapped(ctx, p);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "applyFromWire", e);
            return false;
        } finally {
            Arrays.fill(body, (byte) 0);
        }
    }

    /** Re-assert last wrapped policy (USB attach / suspected tamper). */
    public static void reassert(Context ctx) {
        AtnOrgPolicy p = loadWrapped(ctx);
        if (p == null) {
            Log.w(TAG, "reassert: no wrapped policy");
            return;
        }
        p.apply(ctx);
        Log.i(TAG, "reasserted ver=" + p.ver);
    }
}
