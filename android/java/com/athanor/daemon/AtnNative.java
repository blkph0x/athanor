package com.athanor.daemon;

/**
 * JNI to libatn.so (our C99 tree). REQ-4.1 / DEC-0015.
 */
public final class AtnNative {
    static {
        System.loadLibrary("atn");
    }
    private AtnNative() {}
    public static native String platformId();
    public static native int randomBytes(byte[] out);
}
