package com.athanor.daemon;

/**
 * JNI to libatn.so (our C99 tree). REQ-4.1 / DEC-0015 / DEC-0017.
 */
public final class AtnNative {
    static {
        System.loadLibrary("atn");
    }
    private AtnNative() {}
    public static native String platformId();
    public static native int randomBytes(byte[] out);
    public static native int dmonLoad(byte[] deviceKey, byte[] cluster);
    public static native int dmonRequire();
    public static native void dmonFlush();
    public static native int dmonHbInit(byte[] id, long epoch, byte[] head);
    public static native int dmonHbAddPeer(byte[] id, byte[] key);
    public static native int dmonHbIngest(byte[] msg);
    public static native int dmonHbTick(long bucket);
    public static native int dmonHbState();
    public static native int dmon2faEnroll(byte[] id, byte[] keyOut);
    public static native int dmon2faChallenge(byte[] id, byte[] chalOut);
    public static native int dmon2faVerify(byte[] id, byte[] chal, byte[] resp);
}
