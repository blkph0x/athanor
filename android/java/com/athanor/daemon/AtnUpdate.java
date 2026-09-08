package com.athanor.daemon;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.RandomAccessFile;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * Mesh update over tunnel DATA only (DEC-0048).
 * Wire: 'U' + announce text, or 'U''C' + be32 id/off + be16 len + bytes.
 * No HTTP/URL/cleartext download — chunks arrive inside PQ/AEAD frames.
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

    private AtnUpdate() {}

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
            return false;
        }
        RandomAccessFile raf = null;
        MessageDigest md;
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
                return false;
            }
            Log.i(TAG, "update staged id=" + rxId + " kind=" + rxKind
                    + " ver=" + rxVer + " path=" + finalF.getName()
                    + " (tunnel-only; install may need USB/PackageInstaller)");
            rxId = 0;
            rxKind = 0;
            rxSize = 0;
            rxGot = 0;
            rxVer = "";
            rxSha = "";
            rxPath = null;
            return true;
        } catch (Exception e) {
            Log.w(TAG, "finish", e);
            wipeRxLocked(ctx);
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
            return rc == 0;
        } catch (Exception e) {
            Log.w(TAG, "requestFromHub", e);
            return false;
        }
    }
}
