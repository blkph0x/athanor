package com.athanor.daemon;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Arrays;

/**
 * Mesh messaging + file share (DEC-0055). Family 'M' over the same PQ/AEAD
 * tunnel as voice/update — no HTTP/cleartext side channel. Only peers that
 * share an ESTABLISHED mesh session (same network/app floor) can exchange.
 * Inbox + received files sealed in AtnVault (Keystore AES-GCM).
 */
public final class AtnMesh {
    private static final String TAG = "atn-mesh";

    public static final byte WIRE = (byte) 'M';
    public static final byte TEXT = (byte) 'T';
    public static final byte FILE = (byte) 'F';
    public static final byte CHUNK = (byte) 'C';

    public static final int CHUNK_MAX = 900;
    public static final int NAME_MAX = 64;
    public static final int FROM_MAX = 64;
    public static final int BODY_MAX = 800;
    public static final int SHA_LEN = 32;
    public static final int FILE_MAX = 200 * 1024; /* vault-sized lab share */
    public static final String VAULT_INBOX = "mesh-inbox";
    public static final String VAULT_FILE_PREFIX = "meshfile-";

    private static final Object LOCK = new Object();
    private static String lastStatus = "mesh-msg: idle";
    private static Context appCtx;
    private static String localFrom = "phone";

    /* In-flight receive (one file at a time for lab). */
    private static int rxId;
    private static int rxSize;
    private static int rxGot;
    private static String rxName = "";
    private static byte[] rxSha;
    private static File rxPath;

    private AtnMesh() {}

    public static void setContext(Context ctx) {
        if (ctx != null) {
            appCtx = ctx.getApplicationContext();
        }
    }

    public static void setLocalFrom(String from) {
        if (from != null && from.length() > 0 && from.length() < FROM_MAX) {
            localFrom = from;
        }
    }

    public static String statusLine() {
        synchronized (LOCK) {
            return lastStatus;
        }
    }

    private static void setStatus(String s) {
        synchronized (LOCK) {
            lastStatus = s;
        }
    }

    /** Vault-sealed transcript (newest lines at end). Caller must wipe. */
    public static String loadInbox(Context ctx) {
        byte[] raw = AtnVault.get(ctx, VAULT_INBOX);
        if (raw == null) {
            return "";
        }
        try {
            return new String(raw, StandardCharsets.UTF_8);
        } finally {
            Arrays.fill(raw, (byte) 0);
        }
    }

    private static void appendInbox(Context ctx, String line) {
        if (ctx == null || line == null || line.length() == 0) {
            return;
        }
        String prev = loadInbox(ctx);
        String next = prev;
        if (next.length() > 0 && !next.endsWith("\n")) {
            next = next + "\n";
        }
        next = next + line + "\n";
        /* Cap transcript ~32 KiB. */
        if (next.length() > 32 * 1024) {
            next = next.substring(next.length() - 24 * 1024);
            int nl = next.indexOf('\n');
            if (nl > 0) {
                next = next.substring(nl + 1);
            }
        }
        byte[] raw = next.getBytes(StandardCharsets.UTF_8);
        AtnVault.put(ctx, VAULT_INBOX, raw);
        Arrays.fill(raw, (byte) 0);
    }

    public static boolean sendText(Context ctx, String body) {
        if (ctx == null || body == null) {
            return false;
        }
        byte[] utf = body.getBytes(StandardCharsets.UTF_8);
        if (utf.length == 0 || utf.length > BODY_MAX) {
            setStatus("mesh-msg: body too long/empty");
            return false;
        }
        if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
            setStatus("mesh-msg: tunnel not ESTABLISHED");
            return false;
        }
        byte[] wire = encodeText(localFrom, utf);
        Arrays.fill(utf, (byte) 0);
        if (wire == null) {
            setStatus("mesh-msg: encode fail");
            return false;
        }
        int rc = AtnNative.tunSend(wire);
        Arrays.fill(wire, (byte) 0);
        if (rc != 0) {
            setStatus("mesh-msg: tunSend rc=" + rc);
            return false;
        }
        appendInbox(ctx, "me> " + body);
        setStatus("mesh-msg: sent text (" + body.length() + " chars)");
        Log.i(TAG, "sent text len=" + body.length());
        return true;
    }

    /**
     * Share a small app-scoped demo blob (or caller bytes) over tunnel AEAD.
     * Announce + chunks; peer stores sealed in vault after SHA-256 check.
     */
    public static boolean sendFile(Context ctx, String name, byte[] data) {
        if (ctx == null || name == null || data == null) {
            return false;
        }
        if (data.length == 0 || data.length > FILE_MAX) {
            setStatus("mesh-msg: file size out of range");
            return false;
        }
        if (name.length() == 0 || name.length() >= NAME_MAX) {
            setStatus("mesh-msg: bad file name");
            return false;
        }
        if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
            setStatus("mesh-msg: tunnel not ESTABLISHED");
            return false;
        }
        int fileId = (int) (System.currentTimeMillis() & 0x7fffffff);
        if (fileId == 0) {
            fileId = 1;
        }
        byte[] sha;
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            sha = md.digest(data);
        } catch (Exception e) {
            setStatus("mesh-msg: sha fail");
            return false;
        }
        byte[] announce = encodeFile(fileId, data.length, name, sha);
        if (announce == null) {
            return false;
        }
        int rc = AtnNative.tunSend(announce);
        Arrays.fill(announce, (byte) 0);
        if (rc != 0) {
            setStatus("mesh-msg: file announce tunSend rc=" + rc);
            return false;
        }
        int off = 0;
        while (off < data.length) {
            int n = data.length - off;
            if (n > CHUNK_MAX) {
                n = CHUNK_MAX;
            }
            byte[] chunk = encodeChunk(fileId, off, data, off, n);
            if (chunk == null) {
                setStatus("mesh-msg: chunk encode fail");
                return false;
            }
            rc = AtnNative.tunSend(chunk);
            Arrays.fill(chunk, (byte) 0);
            if (rc != 0) {
                setStatus("mesh-msg: chunk tunSend rc=" + rc + " off=" + off);
                return false;
            }
            off += n;
            try {
                Thread.sleep(15L);
            } catch (InterruptedException ignored) {
            }
        }
        appendInbox(ctx, "me> FILE " + name + " id=" + fileId
                + " size=" + data.length);
        setStatus("mesh-msg: sent file " + name + " (" + data.length + " B)");
        Log.i(TAG, "sent file id=" + fileId + " size=" + data.length);
        return true;
    }

    /** Lab: share a short sealed note as a file over the tunnel. */
    public static boolean sendDemoNote(Context ctx) {
        String note = "athanor mesh file share " + System.currentTimeMillis();
        byte[] data = note.getBytes(StandardCharsets.UTF_8);
        boolean ok = sendFile(ctx, "demo-note.txt", data);
        Arrays.fill(data, (byte) 0);
        return ok;
    }

    /** Daemon drain: handle decrypted 'M' frames. Returns true if consumed. */
    public static boolean onFrame(Context ctx, byte[] msg, int n) {
        if (msg == null || n < 2 || msg[0] != WIRE) {
            return false;
        }
        Context c = ctx != null ? ctx : appCtx;
        byte sub = msg[1];
        if (sub == TEXT) {
            return onText(c, msg, n);
        }
        if (sub == FILE) {
            return onFileAnnounce(c, msg, n);
        }
        if (sub == CHUNK) {
            return onChunk(c, msg, n);
        }
        Log.w(TAG, "unknown subtype=" + (char) (sub & 0xff));
        return true;
    }

    private static boolean onText(Context ctx, byte[] msg, int n) {
        String from = null;
        String body = null;
        try {
            if (n < 5) {
                return false;
            }
            int fromLen = msg[2] & 0xff;
            if (fromLen == 0 || fromLen >= FROM_MAX || n < 3 + fromLen + 2) {
                return false;
            }
            from = new String(msg, 3, fromLen, StandardCharsets.UTF_8);
            int off = 3 + fromLen;
            int blen = be16(msg, off);
            off += 2;
            if (blen <= 0 || blen > BODY_MAX || off + blen != n) {
                return false;
            }
            body = new String(msg, off, blen, StandardCharsets.UTF_8);
            /* Hub opaque echo: skip our own loopback so inbox stays clean. */
            if (from.equals(localFrom)) {
                Log.i(TAG, "text echo skipped from=" + from);
                return true;
            }
            if (ctx != null) {
                appendInbox(ctx, from + "> " + body);
            }
            setStatus("mesh-msg: from " + from + " (" + blen + " B)");
            Log.i(TAG, "recv text from=" + from + " len=" + blen);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "text parse", e);
            return false;
        }
    }

    private static boolean onFileAnnounce(Context ctx, byte[] msg, int n) {
        if (n < 2 + 4 + 4 + 1 + SHA_LEN) {
            return false;
        }
        int id = be32(msg, 2);
        int size = be32(msg, 6);
        int nameLen = msg[10] & 0xff;
        if (nameLen == 0 || nameLen >= NAME_MAX) {
            return false;
        }
        int need = 11 + nameLen + SHA_LEN;
        if (n != need || size <= 0 || size > FILE_MAX) {
            Log.w(TAG, "file announce bad size/name");
            return false;
        }
        String name = new String(msg, 11, nameLen, StandardCharsets.UTF_8);
        byte[] sha = new byte[SHA_LEN];
        System.arraycopy(msg, 11 + nameLen, sha, 0, SHA_LEN);
        synchronized (LOCK) {
            wipeRxLocked();
            if (ctx == null) {
                return false;
            }
            File tmp = new File(ctx.getFilesDir(), "atn-mesh.partial");
            if (tmp.exists()) {
                //noinspection ResultOfMethodCallIgnored
                tmp.delete();
            }
            try {
                RandomAccessFile raf = new RandomAccessFile(tmp, "rw");
                raf.setLength(size);
                raf.close();
            } catch (Exception e) {
                Log.w(TAG, "partial create", e);
                return false;
            }
            rxId = id;
            rxSize = size;
            rxGot = 0;
            rxName = name;
            rxSha = sha;
            rxPath = tmp;
        }
        setStatus("mesh-msg: receiving " + name + " id=" + id
                + " size=" + size);
        Log.i(TAG, "file announce id=" + id + " size=" + size
                + " name=" + name);
        return true;
    }

    private static boolean onChunk(Context ctx, byte[] msg, int n) {
        if (n < 12) {
            return false;
        }
        int id = be32(msg, 2);
        int off = be32(msg, 6);
        int len = be16(msg, 10);
        if (len <= 0 || len > CHUNK_MAX || 12 + len != n) {
            return false;
        }
        synchronized (LOCK) {
            if (rxId == 0 || id != rxId || rxPath == null) {
                Log.w(TAG, "chunk without announce");
                return false;
            }
            if (off != rxGot || off + len > rxSize) {
                Log.w(TAG, "chunk offset bad off=" + off + " got=" + rxGot);
                wipeRxLocked();
                setStatus("mesh-msg: bad chunk (partial wiped)");
                return false;
            }
            RandomAccessFile raf = null;
            try {
                raf = new RandomAccessFile(rxPath, "rw");
                raf.seek(off);
                raf.write(msg, 12, len);
                rxGot = off + len;
                if (rxGot == rxSize) {
                    return finishRxLocked(ctx);
                }
                return true;
            } catch (Exception e) {
                Log.w(TAG, "chunk write", e);
                wipeRxLocked();
                setStatus("mesh-msg: chunk write fail");
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

    private static boolean finishRxLocked(Context ctx) {
        if (rxPath == null || ctx == null) {
            wipeRxLocked();
            return false;
        }
        byte[] data = null;
        try {
            data = new byte[rxSize];
            RandomAccessFile raf = new RandomAccessFile(rxPath, "r");
            int got = raf.read(data);
            raf.close();
            if (got != rxSize) {
                wipeRxLocked();
                return false;
            }
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] dig = md.digest(data);
            if (!Arrays.equals(dig, rxSha)) {
                Log.w(TAG, "sha mismatch — discard");
                wipeRxLocked();
                setStatus("mesh-msg: sha mismatch (discarded)");
                return false;
            }
            String vname = VAULT_FILE_PREFIX + Integer.toHexString(rxId);
            if (!AtnVault.put(ctx, vname, data)) {
                wipeRxLocked();
                setStatus("mesh-msg: vault put fail");
                return false;
            }
            appendInbox(ctx, "peer> FILE " + rxName + " id=" + rxId
                    + " size=" + rxSize + " vault=" + vname);
            setStatus("mesh-msg: stored " + rxName + " → vault/" + vname);
            Log.i(TAG, "file complete id=" + rxId + " vault=" + vname);
            wipeRxLocked();
            return true;
        } catch (Exception e) {
            Log.w(TAG, "finish", e);
            wipeRxLocked();
            return false;
        } finally {
            if (data != null) {
                Arrays.fill(data, (byte) 0);
            }
        }
    }

    private static void wipeRxLocked() {
        if (rxPath != null && rxPath.isFile()) {
            SecureRandom rng = new SecureRandom();
            AtnAppShred.shredFile(rxPath, rng);
        }
        rxId = 0;
        rxSize = 0;
        rxGot = 0;
        rxName = "";
        rxSha = null;
        rxPath = null;
    }

    static byte[] encodeText(String from, byte[] body) {
        if (from == null || body == null) {
            return null;
        }
        byte[] fb = from.getBytes(StandardCharsets.UTF_8);
        if (fb.length == 0 || fb.length >= FROM_MAX
                || body.length == 0 || body.length > BODY_MAX) {
            return null;
        }
        byte[] out = new byte[2 + 1 + fb.length + 2 + body.length];
        out[0] = WIRE;
        out[1] = TEXT;
        out[2] = (byte) fb.length;
        System.arraycopy(fb, 0, out, 3, fb.length);
        putBe16(out, 3 + fb.length, body.length);
        System.arraycopy(body, 0, out, 3 + fb.length + 2, body.length);
        return out;
    }

    static byte[] encodeFile(int fileId, int size, String name, byte[] sha) {
        if (name == null || sha == null || sha.length != SHA_LEN) {
            return null;
        }
        byte[] nb = name.getBytes(StandardCharsets.UTF_8);
        if (nb.length == 0 || nb.length >= NAME_MAX || size <= 0) {
            return null;
        }
        byte[] out = new byte[2 + 4 + 4 + 1 + nb.length + SHA_LEN];
        out[0] = WIRE;
        out[1] = FILE;
        putBe32(out, 2, fileId);
        putBe32(out, 6, size);
        out[10] = (byte) nb.length;
        System.arraycopy(nb, 0, out, 11, nb.length);
        System.arraycopy(sha, 0, out, 11 + nb.length, SHA_LEN);
        return out;
    }

    static byte[] encodeChunk(int fileId, int offset, byte[] data, int dataOff,
                              int len) {
        if (data == null || len <= 0 || len > CHUNK_MAX
                || dataOff < 0 || dataOff + len > data.length) {
            return null;
        }
        byte[] out = new byte[12 + len];
        out[0] = WIRE;
        out[1] = CHUNK;
        putBe32(out, 2, fileId);
        putBe32(out, 6, offset);
        putBe16(out, 10, len);
        System.arraycopy(data, dataOff, out, 12, len);
        return out;
    }

    private static void putBe16(byte[] p, int o, int v) {
        p[o] = (byte) ((v >> 8) & 0xff);
        p[o + 1] = (byte) (v & 0xff);
    }

    private static void putBe32(byte[] p, int o, int v) {
        p[o] = (byte) ((v >> 24) & 0xff);
        p[o + 1] = (byte) ((v >> 16) & 0xff);
        p[o + 2] = (byte) ((v >> 8) & 0xff);
        p[o + 3] = (byte) (v & 0xff);
    }

    private static int be16(byte[] p, int o) {
        return ((p[o] & 0xff) << 8) | (p[o + 1] & 0xff);
    }

    private static int be32(byte[] p, int o) {
        return ((p[o] & 0xff) << 24) | ((p[o + 1] & 0xff) << 16)
                | ((p[o + 2] & 0xff) << 8) | (p[o + 3] & 0xff);
    }
}
