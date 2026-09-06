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
    public static native int dmonHbEmit(long bucket);
    public static native int dmonHbTick(long bucket);
    public static native int dmonHbState();
    public static native int dmon2faEnroll(byte[] id, byte[] keyOut);
    public static native int dmon2faRevoke(byte[] id);
    public static native int dmon2faChallenge(byte[] id, byte[] chalOut);
    public static native int dmon2faVerify(byte[] id, byte[] chal, byte[] resp);
    /* DEC-0027/0038: apply diag/flush/outage before lab tunnel. */
    public static native int dmonSetPolicy(int diag, int flushMode,
                                          int wipeArmed, int outageClass);
    /* DEC-0020/0022: DEC-0007 IPv4 tunnel. tunRecv returns n>=0 or -ATN_ERR_*. */
    public static final int TUN_CLOSED = 0;
    public static final int TUN_HANDSHAKE = 1;
    public static final int TUN_ESTABLISHED = 2;
    /* atn_crypto.h ATN_ERR_LOCKOUT — lab BOOM path (DEC-0039). */
    public static final int ERR_LOCKOUT = 8;
    public static native int tunInitiator(byte[] peerEk);
    public static native int tunResponder(byte[] ownDk);
    public static native int tunBind(int port);
    public static native int tunSetPeer(int ipv4Host, int port);
    public static native int tunHsSend();
    public static native int tunHsRetry();
    public static native int tunKeepalive();
    public static native int tunPump(int timeoutMs);
    public static native int tunSend(byte[] pt);
    public static native int tunRecv(byte[] out, int timeoutMs);
    public static native int tunState();
    public static native int tunPort();
}
