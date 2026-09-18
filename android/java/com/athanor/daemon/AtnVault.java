package com.athanor.daemon;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.SecureRandom;
import java.util.Arrays;

/**
 * App-scoped secure blob store (DEC-0054). AES-256-GCM via Android Keystore
 * (same wrap path as AtnKeystore). Ciphertext under filesDir/vault/.
 * Destroying the Keystore alias makes remaining ciphertext unrecoverable
 * without Knox factory wipe — standalone / stub floor.
 */
public final class AtnVault {
    private static final String TAG = "atn-vault";
    public static final String DIR = "vault";
    public static final String CONTACTS = "contacts";
    public static final int MAX_PLAIN = 256 * 1024;

    private AtnVault() {}

    private static File dir(Context ctx) {
        File d = new File(ctx.getFilesDir(), DIR);
        if (!d.isDirectory()) {
            //noinspection ResultOfMethodCallIgnored
            d.mkdirs();
        }
        return d;
    }

    private static File blobFile(Context ctx, String name) {
        /* Sanitize: only [a-z0-9._-] */
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || c == '.' || c == '_' || c == '-') {
                sb.append(c);
            }
        }
        if (sb.length() == 0) {
            sb.append("x");
        }
        return new File(dir(ctx), sb.toString() + ".aead");
    }

    /** Seal plaintext into vault/&lt;name&gt;.aead. Wipes caller's duty. */
    public static boolean put(Context ctx, String name, byte[] plain) {
        if (ctx == null || plain == null || plain.length == 0
                || plain.length > MAX_PLAIN) {
            return false;
        }
        if (!AtnKeystore.ensureKey()) {
            return false;
        }
        byte[] sealed = AtnKeystore.wrapDataFlexible(plain);
        if (sealed == null) {
            return false;
        }
        FileOutputStream fos = null;
        try {
            fos = new FileOutputStream(blobFile(ctx, name));
            fos.write(sealed);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "put failed", e);
            return false;
        } finally {
            Arrays.fill(sealed, (byte) 0);
            if (fos != null) {
                try {
                    fos.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    /** Open vault blob; caller must wipe returned bytes. Null if missing. */
    public static byte[] get(Context ctx, String name) {
        if (ctx == null) {
            return null;
        }
        File f = blobFile(ctx, name);
        if (!f.isFile()) {
            return null;
        }
        FileInputStream fis = null;
        try {
            long len = f.length();
            if (len <= AtnKeystore.IV_LEN || len > MAX_PLAIN + 64) {
                return null;
            }
            byte[] sealed = new byte[(int) len];
            fis = new FileInputStream(f);
            int n = fis.read(sealed);
            if (n != sealed.length) {
                Arrays.fill(sealed, (byte) 0);
                return null;
            }
            byte[] pt = AtnKeystore.unwrapDataFlexible(sealed);
            Arrays.fill(sealed, (byte) 0);
            return pt;
        } catch (Exception e) {
            Log.w(TAG, "get failed", e);
            return null;
        } finally {
            if (fis != null) {
                try {
                    fis.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    /** Overwrite + delete all vault ciphertexts (still shred Keystore after). */
    public static void shredAll(Context ctx) {
        if (ctx == null) {
            return;
        }
        File d = new File(ctx.getFilesDir(), DIR);
        if (!d.isDirectory()) {
            return;
        }
        File[] kids = d.listFiles();
        if (kids == null) {
            return;
        }
        SecureRandom rng = new SecureRandom();
        for (int i = 0; i < kids.length; i++) {
            AtnAppShred.shredFile(kids[i], rng);
        }
        //noinspection ResultOfMethodCallIgnored
        d.delete();
    }
}
