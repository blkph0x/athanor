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
 * Standalone app-scoped BOOM (DEC-0054). Works with or without Knox:
 * tear down mesh, zeroize native keys, destroy Keystore wrap keys, and
 * overwrite+delete app files so ciphertext cannot be decoded. Knox
 * wipeData remains an extra factory path when jar + Device Admin active.
 */
public final class AtnAppShred {
    private static final String TAG = "atn-shred";
    private static final Object LOCK = new Object();
    private static volatile boolean shredded;

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

    /**
     * Irreversible app-realm destroy. Safe to call repeatedly.
     * @return true if this call performed shred (or already shredded)
     */
    public static boolean execute(Context ctx, String why) {
        synchronized (LOCK) {
            AtnLabBoom.trigger(why != null ? why : "app shred");
            if (shredded) {
                /* Still re-flush network path. */
                killNetwork();
                return true;
            }
            Log.w(TAG, "SHRED begin: " + why);
            killNetwork();
            try {
                AtnVoice.hangup();
            } catch (Exception e) {
                Log.w(TAG, "voice hangup: " + e.getMessage());
            }
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
                shredDir(new File(app.getCacheDir(), ""), rng);
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
            Log.w(TAG, "SHRED done — app data cryptographically dead");
            return true;
        }
    }

    private static void killNetwork() {
        /* dmonFlush closes + wipes tunnel; called by execute(). */
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

    /** Overwrite with random then delete. Package-visible for AtnVault. */
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
            Log.i(TAG, "standalone shred complete (no Knox factory wipe)");
            return;
        }
        try {
            DevicePolicyManager dpm = (DevicePolicyManager)
                    ctx.getSystemService(Context.DEVICE_POLICY_SERVICE);
            ComponentName admin = AtnDeviceAdminReceiver.component(ctx);
            if (dpm != null) {
                dpm.wipeData(0);
            }
            Log.w(TAG, "Knox wipeData after app shred admin=" + admin);
        } catch (SecurityException e) {
            Log.w(TAG, "wipeData SecurityException", e);
        } catch (UnsupportedOperationException e) {
            Log.w(TAG, "wipeData unavailable", e);
        }
    }
}
