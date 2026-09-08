package com.athanor.daemon;

import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.util.Log;

import java.util.Arrays;

/**
 * Compromise vote wire + local boom (DEC-0047).
 * Wire: 'C' + key=value over tunnel AEAD (no parallel HMAC; DEC-0025 reuse).
 */
public final class AtnCompromise {
    private static final String TAG = "atn-comp";

    public int voteId;
    public String targetLabel = "";
    public int timeoutS = 300;
    public int quorum = 1;
    public int yes;
    public String state = "none";
    public String action = "";

    private static volatile int openVoteId;
    private static volatile String openTarget = "";

    private AtnCompromise() {}

    public static AtnCompromise parse(String text) {
        if (text == null) {
            return null;
        }
        AtnCompromise c = new AtnCompromise();
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
                if ("vote_id".equals(k)) {
                    c.voteId = Integer.parseInt(v);
                } else if ("target_label".equals(k)) {
                    if (v.length() == 0 || v.length() >= 64) {
                        return null;
                    }
                    c.targetLabel = v;
                } else if ("timeout_s".equals(k)) {
                    int n = Integer.parseInt(v);
                    if (n < 30 || n > 86400) {
                        return null;
                    }
                    c.timeoutS = n;
                } else if ("quorum".equals(k)) {
                    int n = Integer.parseInt(v);
                    if (n < 1 || n > 64) {
                        return null;
                    }
                    c.quorum = n;
                } else if ("yes".equals(k)) {
                    c.yes = Integer.parseInt(v);
                } else if ("no".equals(k)) {
                    /* tallied on hub; phone may ignore */
                } else if ("state".equals(k)) {
                    c.state = v;
                } else if ("action".equals(k)) {
                    c.action = v;
                } else if ("opened_unix".equals(k)) {
                    /* hub-only */
                } else {
                    return null;
                }
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return c;
    }

    public static int openVoteId() {
        return openVoteId;
    }

    public static String openTarget() {
        return openTarget;
    }

    /**
     * Apply hub 'C' wire. Boom → flush + wrap delete + wipe trip (real Knox)
     * or lab boom proof (stub / T-0400).
     */
    public static boolean applyFromWire(Context ctx, byte[] msg, int n) {
        if (msg == null || n < 2 || msg[0] != (byte) 'C') {
            return false;
        }
        byte[] body = new byte[n - 1];
        try {
            System.arraycopy(msg, 1, body, 0, n - 1);
            AtnCompromise c = parse(new String(body, "UTF-8"));
            if (c == null) {
                Log.w(TAG, "compromise parse fail");
                return false;
            }
            if ("open".equals(c.action) || "open".equals(c.state)) {
                openVoteId = c.voteId;
                openTarget = c.targetLabel == null ? "" : c.targetLabel;
                Log.i(TAG, "vote open id=" + c.voteId + " target=" + openTarget);
                return true;
            }
            if ("clear".equals(c.action) || "cleared".equals(c.state)) {
                openVoteId = 0;
                openTarget = "";
                Log.i(TAG, "vote cleared");
                return true;
            }
            if ("boom".equals(c.action) || "boom_pending".equals(c.state)
                    || "done".equals(c.state)) {
                executeBoom(ctx, "compromise vote boom id=" + c.voteId
                        + " target=" + c.targetLabel);
                openVoteId = 0;
                openTarget = "";
                return true;
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "applyFromWire", e);
            return false;
        } finally {
            Arrays.fill(body, (byte) 0);
        }
    }

    /** Lab / operator: send YES or NO over tunnel for open vote. */
    public static boolean sendVote(boolean yes) {
        if (openVoteId <= 0) {
            return false;
        }
        String act = yes ? "vote_yes" : "vote_no";
        String text = "action=" + act + "\n"
                + "vote_id=" + openVoteId + "\n"
                + "target_label=" + openTarget + "\n"
                + "timeout_s=300\n"
                + "quorum=1\n"
                + "yes=0\n"
                + "state=open\n";
        try {
            byte[] body = text.getBytes("UTF-8");
            byte[] wire = new byte[1 + body.length];
            wire[0] = (byte) 'C';
            System.arraycopy(body, 0, wire, 1, body.length);
            int rc = AtnNative.tunSend(wire);
            Arrays.fill(wire, (byte) 0);
            Arrays.fill(body, (byte) 0);
            return rc == 0;
        } catch (Exception e) {
            Log.w(TAG, "sendVote", e);
            return false;
        }
    }

    public static void executeBoom(Context ctx, String why) {
        if (!AtnLabBoom.trigger(why)) {
            /* already dead — still flush */
        }
        AtnNative.dmonFlush();
        if (ctx != null) {
            AtnKeystore.deleteWrap(ctx);
        }
        Log.w(TAG, "BOOM: " + why);
        if (ctx == null) {
            return;
        }
        if (AtnKnoxBuild.isStub() || !AtnDeviceAdminReceiver.isAdminActive(ctx)) {
            Log.i(TAG, "stub/lab: boom signal proven; Knox wipe waits T-0400");
            return;
        }
        try {
            DevicePolicyManager dpm = (DevicePolicyManager)
                    ctx.getSystemService(Context.DEVICE_POLICY_SERVICE);
            ComponentName admin = AtnDeviceAdminReceiver.component(ctx);
            if (dpm != null) {
                /* Production: factory reset path when DO/Knox present. */
                dpm.wipeData(0);
            }
            Log.w(TAG, "wipeData tripped admin=" + admin);
        } catch (SecurityException e) {
            Log.w(TAG, "wipeData SecurityException", e);
        } catch (UnsupportedOperationException e) {
            Log.w(TAG, "wipeData unavailable", e);
        }
    }
}
