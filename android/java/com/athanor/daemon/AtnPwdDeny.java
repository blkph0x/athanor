package com.athanor.daemon;

import android.content.Context;
import android.util.Log;

import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * SHA-256 password deny set (DEC-0046). Asset {@code atn_pwd_deny.bin}:
 * big-endian count + sorted 32-byte digests. Never logs candidate passwords.
 */
public final class AtnPwdDeny {
    private static final String TAG = "atn-pwd-deny";
    private static final String ASSET = "atn_pwd_deny.bin";

    private static volatile byte[] table; /* count*32 sorted digests */
    private static volatile int count;

    private AtnPwdDeny() {}

    public static synchronized boolean ensureLoaded(Context ctx) {
        if (table != null) {
            return count >= 0;
        }
        count = 0;
        table = new byte[0];
        if (ctx == null) {
            return false;
        }
        InputStream in = null;
        try {
            in = ctx.getAssets().open(ASSET);
            byte[] hdr = new byte[4];
            if (in.read(hdr) != 4) {
                return false;
            }
            int n = ((hdr[0] & 0xff) << 24) | ((hdr[1] & 0xff) << 16)
                    | ((hdr[2] & 0xff) << 8) | (hdr[3] & 0xff);
            if (n < 0 || n > 5_000_000) {
                return false;
            }
            long need = (long) n * 32L;
            if (need > Integer.MAX_VALUE) {
                return false;
            }
            byte[] body = new byte[(int) need];
            int off = 0;
            while (off < body.length) {
                int r = in.read(body, off, body.length - off);
                if (r <= 0) {
                    return false;
                }
                off += r;
            }
            table = body;
            count = n;
            Log.i(TAG, "deny set loaded n=" + n);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "deny set missing/unloadable", e);
            return false;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception e) {
                    /* ignore */
                }
            }
        }
    }

    /**
     * @return true if password SHA-256 is in the deny set (must reject).
     *         Candidate bytes are wiped before return.
     */
    public static boolean isDenied(Context ctx, char[] password) {
        if (password == null || password.length == 0) {
            return true;
        }
        if (!ensureLoaded(ctx) || count <= 0 || table == null) {
            wipe(password);
            return false; /* fail-open only if asset absent — lab */
        }
        byte[] utf = null;
        byte[] dig = null;
        try {
            utf = new String(password).getBytes("UTF-8");
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            dig = md.digest(utf);
            return binarySearch(dig) >= 0;
        } catch (Exception e) {
            Log.w(TAG, "hash failed", e);
            return true; /* fail-closed on hash error */
        } finally {
            wipe(password);
            wipe(utf);
            wipe(dig);
        }
    }

    private static int binarySearch(byte[] dig) {
        int lo = 0;
        int hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = compare(dig, mid);
            if (cmp < 0) {
                hi = mid - 1;
            } else if (cmp > 0) {
                lo = mid + 1;
            } else {
                return mid;
            }
        }
        return -1;
    }

    private static int compare(byte[] dig, int index) {
        int off = index * 32;
        for (int i = 0; i < 32; i++) {
            int a = dig[i] & 0xff;
            int b = table[off + i] & 0xff;
            if (a != b) {
                return a - b;
            }
        }
        return 0;
    }

    public static void wipe(byte[] b) {
        if (b != null) {
            Arrays.fill(b, (byte) 0);
        }
    }

    public static void wipe(char[] c) {
        if (c != null) {
            Arrays.fill(c, '\0');
        }
    }
}
