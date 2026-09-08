package com.athanor.daemon;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * Mesh update over tunnel DATA only (DEC-0048).
 * Wire: 'U' + announce text, or 'U''C' + be32 id/off + be16 len + bytes.
 * No HTTP/URL/cleartext download — chunks arrive inside PQ/AEAD frames.
 * After APK stage (kind=1), triggers lab self-update via PackageInstaller
 * (preferred) or ACTION_INSTALL_PACKAGE + AtnApkProvider.
 */
public final class AtnUpdate {
    private static final String TAG = "atn-upd";
    private static final int CHUNK_HDR = 12; /* 'U''C' + id4 + off4 + len2 */

    public int updateId;
    public int kind; /* 1=apk 2=site 3=hub */
    public String version = "";
    public String sha256Hex = "";
    public int size;
    public String action = "";

    private static volatile int rxId;
    private static volatile int rxKind;
    private static volatile int rxSize;
    private static volatile int rxGot;
    private static volatile String rxVer = "";
    private static volatile String rxSha = "";
    private static volatile String rxPath;
    private static final Object LOCK = new Object();

    private static volatile String statusText = "update: idle";
    private static volatile long statusAtMs;

    private AtnUpdate() {}

    public static void setStatus(String s) {
        if (s == null) {
            s = "";
        }
        statusText = s;
        statusAtMs = System.currentTimeMillis();
    }

    /** Lab UI line: receiving / staged / installing / idle. */
    public static String statusLine() {
        synchronized (LOCK) {
            if (rxId != 0 && rxSize > 0) {
                return "Update receiving id=" + rxId
                        + " " + rxGot + "/" + rxSize
                        + (rxVer.length() > 0 ? (" ver=" + rxVer) : "");
            }
        }
        String s = statusText;
        if (s == null || s.length() == 0) {
            return "update: idle";
        }
        return s;
    }

    public static AtnUpdate parse(String text) {
        if (text == null) {
            return null;
        }
        AtnUpdate u = new AtnUpdate();
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
                if ("update_id".equals(k)) {
                    u.updateId = Integer.parseInt(v);
                } else if ("kind".equals(k)) {
                    if ("apk".equals(v)) {
                        u.kind = 1;
                    } else if ("site".equals(v)) {
                        u.kind = 2;
                    } else if ("hub".equals(v)) {
                        u.kind = 3;
                    } else {
                        return null;
                    }
                } else if ("version".equals(k)) {
                    if (v.length() == 0 || v.length() >= 64) {
                        return null;
                    }
                    u.version = v;
                } else if ("sha256".equals(k)) {
                    if (v.length() != 64) {
                        return null;
                    }
                    u.sha256Hex = v.toLowerCase();
                } else if ("size".equals(k)) {
                    u.size = Integer.parseInt(v);
                } else if ("chunk_size".equals(k) || "payload_path".equals(k)) {
                    /* hub-local; phone ignores */
                } else if ("action".equals(k)) {
                    u.action = v;
                } else {
                    return null;
                }
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return u;
    }

    private static int be32(byte[] m, int o) {
        return ((m[o] & 0xff) << 24) | ((m[o + 1] & 0xff) << 16)
                | ((m[o + 2] & 0xff) << 8) | (m[o + 3] & 0xff);
    }

    private static int be16(byte[] m, int o) {
        return ((m[o] & 0xff) << 8) | (m[o + 1] & 0xff);
    }

    private static void wipeRxLocked(Context ctx) {
        if (rxPath != null) {
            try {
                new File(rxPath).delete();
            } catch (Exception ignored) {
            }
        }
        rxId = 0;
        rxKind = 0;
        rxSize = 0;
        rxGot = 0;
        rxVer = "";
        rxSha = "";
        rxPath = null;
    }

    private static String stageName(int kind) {
        if (kind == 2) {
            return "atn-update-site.bin";
        }
        if (kind == 3) {
            return "atn-update-hub.bin";
        }
        return "atn-update.apk";
    }

    private static boolean finishLocked(Context ctx) {
        if (ctx == null || rxPath == null || rxGot != rxSize || rxSize <= 0) {
            wipeRxLocked(ctx);
            setStatus("update: stage aborted (partial wiped)");
            return false;
        }
        RandomAccessFile raf = null;
        MessageDigest md;
        int kindDone = rxKind;
        String verDone = rxVer;
        int idDone = rxId;
        try {
            md = MessageDigest.getInstance("SHA-256");
            raf = new RandomAccessFile(rxPath, "r");
            byte[] buf = new byte[4096];
            int n;
            while ((n = raf.read(buf)) > 0) {
                md.update(buf, 0, n);
            }
            raf.close();
            raf = null;
            byte[] dig = md.digest();
            StringBuilder sb = new StringBuilder(64);
            for (int i = 0; i < dig.length; i++) {
                sb.append(String.format("%02x", dig[i] & 0xff));
            }
            Arrays.fill(dig, (byte) 0);
            Arrays.fill(buf, (byte) 0);
            String got = sb.toString();
            if (!got.equalsIgnoreCase(rxSha)) {
                Log.w(TAG, "sha256 mismatch");
                wipeRxLocked(ctx);
                setStatus("update: sha256 mismatch (partial wiped)");
                return false;
            }
            File staged = new File(rxPath);
            File finalF = new File(ctx.getFilesDir(), stageName(rxKind));
            if (finalF.exists() && !finalF.delete()) {
                Log.w(TAG, "could not replace staged file");
            }
            if (!staged.renameTo(finalF)) {
                Log.w(TAG, "rename failed");
                wipeRxLocked(ctx);
                setStatus("update: rename failed (partial wiped)");
                return false;
            }
            Log.i(TAG, "update staged id=" + idDone + " kind=" + kindDone
                    + " ver=" + verDone + " path=" + finalF.getName()
                    + " (tunnel-only)");
            setStatus("Update staged kind=" + kindDone
                    + (verDone.length() > 0 ? (" ver=" + verDone) : ""));
            rxId = 0;
            rxKind = 0;
            rxSize = 0;
            rxGot = 0;
            rxVer = "";
            rxSha = "";
            rxPath = null;
            if (kindDone == 1) {
                final Context app = ctx.getApplicationContext();
                final File apk = finalF;
                final String ver = verDone;
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        triggerApkInstall(app, apk, ver);
                    }
                }, "atn-upd-install").start();
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "finish", e);
            wipeRxLocked(ctx);
            setStatus("update: finish error (partial wiped)");
            return false;
        } finally {
            if (raf != null) {
                try {
                    raf.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    /**
     * After successful APK stage: PackageInstaller session, else
     * ACTION_INSTALL_PACKAGE via AtnApkProvider. Never logs payload bytes.
     * On failure keeps staged file for a later user confirm / unknown-sources.
     */
    static void triggerApkInstall(Context ctx, File apk, String ver) {
        if (ctx == null || apk == null || !apk.isFile()) {
            setStatus("install: staged apk missing");
            Log.w(TAG, "install skip: staged apk missing");
            return;
        }
        setStatus("Update staged/installing…"
                + (ver != null && ver.length() > 0 ? (" ver=" + ver) : ""));
        Log.i(TAG, "install start size=" + apk.length()
                + (ver != null && ver.length() > 0 ? (" ver=" + ver) : ""));
        if (Build.VERSION.SDK_INT >= 26) {
            try {
                PackageManager pm = ctx.getPackageManager();
                if (!pm.canRequestPackageInstalls()) {
                    Log.w(TAG, "install: REQUEST_INSTALL_PACKAGES not granted"
                            + " - opening settings; staged APK kept");
                    setStatus("install: enable unknown apps, then retry");
                    Intent settings = new Intent(
                            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + ctx.getPackageName()));
                    settings.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    try {
                        ctx.startActivity(settings);
                    } catch (Exception e) {
                        Log.w(TAG, "open unknown-sources settings", e);
                    }
                    /* Still try PackageInstaller — may prompt. */
                }
            } catch (Exception e) {
                Log.w(TAG, "canRequestPackageInstalls", e);
            }
        }
        if (tryPackageInstaller(ctx, apk)) {
            return;
        }
        if (tryInstallPackageIntent(ctx)) {
            return;
        }
        setStatus("install: failed (staged APK kept)");
        Log.w(TAG, "install failed; staged APK kept at "
                + apk.getName());
    }

    private static boolean tryPackageInstaller(Context ctx, File apk) {
        PackageInstaller.Session session = null;
        InputStream in = null;
        OutputStream out = null;
        try {
            PackageInstaller installer =
                    ctx.getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params =
                    new PackageInstaller.SessionParams(
                            PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setAppPackageName(ctx.getPackageName());
            int sessionId = installer.createSession(params);
            session = installer.openSession(sessionId);
            in = new FileInputStream(apk);
            out = session.openWrite("atn-update.apk", 0, apk.length());
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            session.fsync(out);
            out.close();
            out = null;
            in.close();
            in = null;
            Arrays.fill(buf, (byte) 0);
            Intent callback = new Intent(ctx, AtnInstallReceiver.class);
            callback.setAction(AtnInstallReceiver.ACTION);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) {
                flags |= PendingIntent.FLAG_MUTABLE;
            }
            PendingIntent pi = PendingIntent.getBroadcast(ctx, sessionId,
                    callback, flags);
            session.commit(pi.getIntentSender());
            session.close();
            session = null;
            Log.i(TAG, "install PackageInstaller session committed id="
                    + sessionId);
            setStatus("Update staged/installing… (PackageInstaller)");
            return true;
        } catch (Exception e) {
            Log.w(TAG, "PackageInstaller failed", e);
            if (session != null) {
                try {
                    session.abandon();
                } catch (Exception ignored) {
                }
            }
            return false;
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (Exception ignored) {
                }
            }
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
            if (session != null) {
                try {
                    session.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private static boolean tryInstallPackageIntent(Context ctx) {
        try {
            Uri uri = AtnApkProvider.apkUri();
            Intent intent = new Intent(Intent.ACTION_INSTALL_PACKAGE);
            intent.setData(uri);
            intent.setFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.putExtra(Intent.EXTRA_NOT_UNKNOWN_SOURCE, true);
            intent.putExtra(Intent.EXTRA_RETURN_RESULT, true);
            ctx.startActivity(intent);
            Log.i(TAG, "install ACTION_INSTALL_PACKAGE launched");
            setStatus("Update staged/installing… (user confirm)");
            return true;
        } catch (Exception e) {
            Log.w(TAG, "ACTION_INSTALL_PACKAGE failed", e);
            return false;
        }
    }

    /**
     * Apply hub 'U' wire from tunRecv. Returns true if frame handled.
     */
    public static boolean applyFromWire(Context ctx, byte[] msg, int n) {
        if (msg == null || n < 1 || msg[0] != (byte) 'U') {
            return false;
        }
        if (n >= 2 && msg[1] == (byte) 'C') {
            if (n < CHUNK_HDR) {
                return false;
            }
            int id = be32(msg, 2);
            int off = be32(msg, 6);
            int len = be16(msg, 10);
            if (len < 0 || CHUNK_HDR + len != n) {
                return false;
            }
            synchronized (LOCK) {
                if (rxId == 0 || id != rxId || rxPath == null) {
                    Log.w(TAG, "chunk without announce");
                    return false;
                }
                if (off != rxGot || off + len > rxSize) {
                    Log.w(TAG, "chunk offset bad off=" + off + " got=" + rxGot);
                    wipeRxLocked(ctx);
                    setStatus("update: bad chunk (partial wiped)");
                    return false;
                }
                RandomAccessFile raf = null;
                try {
                    raf = new RandomAccessFile(rxPath, "rw");
                    raf.seek(off);
                    raf.write(msg, CHUNK_HDR, len);
                    rxGot = off + len;
                    if (rxGot == rxSize) {
                        return finishLocked(ctx);
                    }
                    return true;
                } catch (Exception e) {
                    Log.w(TAG, "chunk write", e);
                    wipeRxLocked(ctx);
                    setStatus("update: chunk write fail (partial wiped)");
                    return false;
                } finally {
                    if (raf != null) {
                        try {
                            raf.close();
                        } catch (Exception ignored) {
                        }
                    }
                }
            }
        }
        /* Announce text after 'U' */
        byte[] body = new byte[n - 1];
        try {
            System.arraycopy(msg, 1, body, 0, n - 1);
            AtnUpdate u = parse(new String(body, "UTF-8"));
            if (u == null || u.updateId <= 0 || u.size <= 0
                    || u.sha256Hex.length() != 64) {
                Log.w(TAG, "announce parse fail");
                return false;
            }
            synchronized (LOCK) {
                wipeRxLocked(ctx);
                File tmp = new File(ctx.getFilesDir(),
                        "atn-update.partial");
                if (tmp.exists()) {
                    tmp.delete();
                }
                RandomAccessFile raf = new RandomAccessFile(tmp, "rw");
                raf.setLength(u.size);
                raf.close();
                rxId = u.updateId;
                rxKind = u.kind == 0 ? 1 : u.kind;
                rxSize = u.size;
                rxGot = 0;
                rxVer = u.version;
                rxSha = u.sha256Hex;
                rxPath = tmp.getAbsolutePath();
                Log.i(TAG, "announce id=" + rxId + " size=" + rxSize
                        + " kind=" + rxKind + " (tunnel AEAD)");
                setStatus("Update receiving id=" + rxId
                        + " size=" + rxSize
                        + (rxVer.length() > 0 ? (" ver=" + rxVer) : ""));
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "announce", e);
            return false;
        } finally {
            Arrays.fill(body, (byte) 0);
        }
    }

    /** Request current update from hub over tunnel (U?). */
    public static boolean requestFromHub() {
        try {
            byte[] wire = new byte[] { (byte) 'U', (byte) '?' };
            int rc = AtnNative.tunSend(wire);
            Arrays.fill(wire, (byte) 0);
            if (rc == 0) {
                setStatus("Update request sent (U?)");
                Log.i(TAG, "requestFromHub U? sent");
            } else {
                setStatus("Update request failed rc=" + rc);
                Log.w(TAG, "requestFromHub tunSend rc=" + rc);
            }
            return rc == 0;
        } catch (Exception e) {
            Log.w(TAG, "requestFromHub", e);
            setStatus("Update request error");
            return false;
        }
    }
}
