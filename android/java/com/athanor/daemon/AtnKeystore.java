package com.athanor.daemon;

import android.content.Context;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * Hardware-backed wrap key. DEC-0016: Android Keystore, not TIMA enable
 * APIs (deprecated API 33; dead on Android 12+ / S24–S26).
 * Wrap format (NIST SP 800-38D 96-bit IV): iv12 || ciphertext||tag.
 */
public final class AtnKeystore {
    private static final String TAG = "atn-ks";
    public static final String ALIAS = "atn-device";
    public static final String WRAP_FILE = "atn-wrap.bin";
    public static final int IV_LEN = 12;
    public static final int GCM_TAG_BITS = 128;
    public static final int PLAIN_LEN = 32 + 32 + 8; /* device||cluster||hb id */

    private AtnKeystore() {}

    public static boolean ensureKey() {
        try {
            KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
            ks.load(null);
            if (ks.containsAlias(ALIAS)) {
                return ks.getKey(ALIAS, null) instanceof SecretKey;
            }
            KeyGenerator kg = KeyGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            KeyGenParameterSpec.Builder b = new KeyGenParameterSpec.Builder(
                    ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setKeySize(256)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE);
            if (Build.VERSION.SDK_INT >= 28) {
                try {
                    b.setIsStrongBoxBacked(true);
                    kg.init(b.build());
                    kg.generateKey();
                    Log.i(TAG, "StrongBox AES-256 alias=" + ALIAS);
                    return true;
                } catch (Exception e) {
                    Log.w(TAG, "StrongBox unavailable, TEE Keystore", e);
                    b.setIsStrongBoxBacked(false);
                }
            }
            kg.init(b.build());
            kg.generateKey();
            Log.i(TAG, "TEE AES-256 alias=" + ALIAS);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "ensureKey failed", e);
            return false;
        }
    }

    public static boolean keyExists() {
        try {
            KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
            ks.load(null);
            return ks.containsAlias(ALIAS);
        } catch (Exception e) {
            return false;
        }
    }

    public static byte[] wrap(byte[] pt) {
        if (pt == null || pt.length != PLAIN_LEN) {
            return null;
        }
        if (!ensureKey()) {
            return null;
        }
        try {
            KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
            ks.load(null);
            SecretKey key = (SecretKey) ks.getKey(ALIAS, null);
            if (key == null) {
                return null;
            }
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.ENCRYPT_MODE, key);
            byte[] iv = c.getIV();
            if (iv == null || iv.length != IV_LEN) {
                return null;
            }
            byte[] ct = c.doFinal(pt);
            byte[] out = new byte[IV_LEN + ct.length];
            System.arraycopy(iv, 0, out, 0, IV_LEN);
            System.arraycopy(ct, 0, out, IV_LEN, ct.length);
            return out;
        } catch (Exception e) {
            Log.e(TAG, "wrap failed", e);
            return null;
        }
    }

    public static byte[] unwrap(byte[] blob) {
        if (blob == null || blob.length <= IV_LEN) {
            return null;
        }
        try {
            KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
            ks.load(null);
            SecretKey key = (SecretKey) ks.getKey(ALIAS, null);
            if (key == null) {
                return null;
            }
            byte[] iv = new byte[IV_LEN];
            byte[] ct = new byte[blob.length - IV_LEN];
            System.arraycopy(blob, 0, iv, 0, IV_LEN);
            System.arraycopy(blob, IV_LEN, ct, 0, ct.length);
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(GCM_TAG_BITS, iv));
            byte[] pt = c.doFinal(ct);
            if (pt == null || pt.length != PLAIN_LEN) {
                return null;
            }
            return pt;
        } catch (Exception e) {
            Log.e(TAG, "unwrap failed", e);
            return null;
        }
    }

    public static boolean storeWrap(Context ctx, byte[] blob) {
        if (ctx == null || blob == null) {
            return false;
        }
        FileOutputStream fos = null;
        try {
            fos = ctx.openFileOutput(WRAP_FILE, Context.MODE_PRIVATE);
            fos.write(blob);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "storeWrap failed", e);
            return false;
        } finally {
            if (fos != null) {
                try {
                    fos.close();
                } catch (Exception e) {
                    /* ignore */
                }
            }
        }
    }

    public static byte[] loadWrap(Context ctx) {
        if (ctx == null) {
            return null;
        }
        FileInputStream fis = null;
        try {
            fis = ctx.openFileInput(WRAP_FILE);
            int n = fis.available();
            if (n <= IV_LEN) {
                return null;
            }
            byte[] blob = new byte[n];
            int got = fis.read(blob);
            if (got != n) {
                return null;
            }
            return blob;
        } catch (Exception e) {
            return null;
        } finally {
            if (fis != null) {
                try {
                    fis.close();
                } catch (Exception e) {
                    /* ignore */
                }
            }
        }
    }

    public static void deleteWrap(Context ctx) {
        if (ctx == null) {
            return;
        }
        ctx.deleteFile(WRAP_FILE);
    }
}
