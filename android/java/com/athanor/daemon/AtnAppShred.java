package com.athanor.daemon;

import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.RandomAccessFile;
import java.security.SecureRandom;
import java.util.Arrays;

/**
 * App-scoped BOOM (DEC-0054). Default is <b>test mode</b>: UI dead + hangup
 * + stop mesh use, keys and vault kept (lab soak). Kill mode (real shred)
 * only when {@code wipe_armed=1} from org policy / conf.
 */
public final class AtnAppShred {
    private static final String TAG = "atn-shred";
    private static final Object LOCK = new Object();
    private static volatile boolean shredded;
    /* Default OFF — DEC-0054 lab: prove boom without destroying data. */
    private static volatile boolean killMode;

    private static final String[] SENSITIVE_NAMES = {
            "atn-node.conf",
            "atn-contacts.conf",
            "atn-wrap.bin",
            "atn-policy.bin",
            "atn-policy.conf",
            "atn-update.apk",
            "atn-update.site",
            "atn-update.hub",
            "atn-update.tmp",
            "payload.recv",
            "update.stage"
    };

    private AtnAppShred() {}

    public static boolean isShredded() {
        return shredded;
    }

    public static boolean isKillMode() {
        return killMode;
    }

    /** From org policy / conf wipe_armed (1 = real crypto-shred). */
    public static void setKillMode(boolean kill) {
        killMode = kill;
        Log.i(TAG, kill ? "KILL mode armed (wipe_armed=1)"
                : "TEST mode (boom UI only; keys kept)");
    }

    /**
     * BOOM entry. Test mode: mark dead + hangup, keep keys/files.
     * Kill mode: irreversible shred (+ Knox wipeData when available).
     */
    public static boolean execute(Context ctx, String why) {
        synchronized (LOCK) {
            String reason = why != null ? why : "app boom";
            AtnLabBoom.trigger(reason);
            try {
                AtnVoice.hangup();
            } catch (Exception e) {
                Log.w(TAG, "voice hangup: " + e.getMessage());
            }
            if (!killMode) {
                Log.w(TAG, "TEST BOOM (no shred): " + reason);
                return true;
            }
            if (shredded) {
                try {
                    AtnNative.dmonFlush();
                } catch (Exception ignored) {
                }
                return true;
            }
            Log.w(TAG, "KILL SHRED begin: " + reason);
            try {
                AtnNative.dmonFlush();
            } catch (Exception e) {
                Log.w(TAG, "dmonFlush: " + e.getMessage());
            }
            SecureRandom rng = new SecureRandom();
            if (ctx != null) {
                Context app = ctx.getApplicationContext();
                AtnVault.shredAll(app);
                shredNamed(app, rng);
                shredDir(new File(app.getFilesDir(), "updates"), rng);
                shredDir(app.getCacheDir(), rng);
                try {
                    app.getSharedPreferences("atn", Context.MODE_PRIVATE)
                            .edit().clear().commit();
                } catch (Exception ignored) {
                }
                AtnKeystore.deleteWrap(app);
                AtnKeystore.destroyKey();
                maybeKnoxWipe(app);
            } else {
                AtnKeystore.destroyKey();
            }
            shredded = true;
            Log.w(TAG, "KILL SHRED done — app data cryptographically dead");
            return true;
        }
    }

    private static void shredNamed(Context ctx, SecureRandom rng) {
        File base = ctx.getFilesDir();
        for (int i = 0; i < SENSITIVE_NAMES.length; i++) {
            shredFile(new File(base, SENSITIVE_NAMES[i]), rng);
            try {
                ctx.deleteFile(SENSITIVE_NAMES[i]);
            } catch (Exception ignored) {
            }
        }
    }

    private static void shredDir(File dir, SecureRandom rng) {
        if (dir == null || !dir.isDirectory()) {
            return;
        }
        File[] kids = dir.listFiles();
        if (kids != null) {
            for (int i = 0; i < kids.length; i++) {
                if (kids[i].isDirectory()) {
                    shredDir(kids[i], rng);
                } else {
                    shredFile(kids[i], rng);
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        dir.delete();
    }

    static void shredFile(File f, SecureRandom rng) {
        if (f == null || !f.isFile()) {
            return;
        }
        RandomAccessFile raf = null;
        try {
            long len = f.length();
            if (len > 0L && len < 64L * 1024L * 1024L) {
                raf = new RandomAccessFile(f, "rw");
                byte[] buf = new byte[4096];
                long left = len;
                raf.seek(0L);
                while (left > 0L) {
                    int n = (int) Math.min(buf.length, left);
                    rng.nextBytes(buf);
                    raf.write(buf, 0, n);
                    left -= n;
                }
                raf.getFD().sync();
                Arrays.fill(buf, (byte) 0);
            }
        } catch (Exception e) {
            Log.w(TAG, "shred overwrite " + f.getName() + ": " + e.getMessage());
        } finally {
            if (raf != null) {
                try {
                    raf.close();
                } catch (Exception ignored) {
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    private static void maybeKnoxWipe(Context ctx) {
        if (AtnKnoxBuild.isStub() || !AtnDeviceAdminReceiver.isAdminActive(ctx)) {
            Log.i(TAG, "kill shred complete (no Knox factory wipe)");
            return;
        }
        try {
            DevicePolicyManager dpm = (DevicePolicyManager)
                    ctx.getSystemService(Context.DEVICE_POLICY_SERVICE);
            ComponentName admin = AtnDeviceAdminReceiver.component(ctx);
            if (dpm != null) {
                dpm.wipeData(0);
            }
            Log.w(TAG, "Knox wipeData after kill shred admin=" + admin);
        } catch (SecurityException e) {
            Log.w(TAG, "wipeData SecurityException", e);
        } catch (UnsupportedOperationException e) {
            Log.w(TAG, "wipeData unavailable", e);
        }
    }
}
