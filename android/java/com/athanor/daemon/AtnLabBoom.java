package com.athanor.daemon;

/**
 * Lab-only BOOM state (DEC-0039). Not a production Faraday brick.
 * With diag log_only, keys are not wiped — UI says dead for soak.
 */
public final class AtnLabBoom {
    public static final long SILENCE_MS = 30L * 1000L;
    public static final int FAIL_MAX = 5;
    public static final int ERR_LOCKOUT = 8;
    /* Exactly ATN_2FA_ID_LEN (32). */
    public static final byte[] LAB_ID = {
        'a', 't', 'h', 'a', 'n', 'o', 'r', '-',
        'l', 'a', 'b', '-', '2', 'f', 'a', '-',
        'v', '1', '!', '!', '!', '!', '!', '!',
        '!', '!', '!', '!', '!', '!', '!', '!'
    };

    private static volatile boolean dead;
    private static volatile String reason = "";
    private static volatile long lastHubMs;
    private static volatile boolean sawEstablished;
    private static volatile int pinFails;
    private static volatile int deviceUnlockFails;
    private static volatile boolean enrolled;

    private AtnLabBoom() {}

    public static synchronized void reset() {
        dead = false;
        reason = "";
        lastHubMs = 0L;
        sawEstablished = false;
        pinFails = 0;
        deviceUnlockFails = 0;
    }

    public static boolean isDead() {
        return dead;
    }

    public static String reason() {
        return reason;
    }

    public static int pinFails() {
        return pinFails;
    }

    public static int deviceUnlockFails() {
        return deviceUnlockFails;
    }

    public static synchronized void noteDeviceUnlockFail(int n) {
        deviceUnlockFails = n;
    }

    public static long lastHubMs() {
        return lastHubMs;
    }

    public static boolean sawEstablished() {
        return sawEstablished;
    }

    public static synchronized void noteHubContact() {
        lastHubMs = System.currentTimeMillis();
        sawEstablished = true;
    }

    /**
     * Tunnel shows ESTABLISHED. Only starts the silence clock on a fresh
     * CLOSED/HANDSHAKE → ESTABLISHED transition (caller passes true).
     * Soft mark (fromReconnect) arms mesh-up UI without starting the timer
     * until a real hub DATA echo (DEC-0039).
     */
    public static synchronized void noteEstablished(boolean startSilenceClock) {
        sawEstablished = true;
        if (startSilenceClock && lastHubMs <= 0L) {
            lastHubMs = System.currentTimeMillis();
        }
    }

    public static synchronized boolean maybeSilenceBoom(boolean netUp) {
        if (dead || !sawEstablished || lastHubMs <= 0L) {
            return false;
        }
        long quiet = System.currentTimeMillis() - lastHubMs;
        if (quiet < SILENCE_MS) {
            return false;
        }
        String why = netUp
                ? "hub silence >30s (lab)"
                : "no net/airplane + hub silence >30s (lab)";
        return trigger(why);
    }

    public static synchronized int noteWrongCode() {
        if (dead) {
            return pinFails;
        }
        pinFails++;
        if (pinFails >= FAIL_MAX) {
            trigger("wrong code x" + FAIL_MAX + " (lab)");
        }
        return pinFails;
    }

    public static synchronized boolean trigger(String why) {
        if (dead) {
            return false;
        }
        dead = true;
        reason = why;
        return true;
    }

    public static synchronized boolean ensureEnrolled() {
        if (enrolled) {
            return true;
        }
        byte[] key = new byte[32];
        int rc = AtnNative.dmon2faEnroll(LAB_ID, key);
        for (int i = 0; i < key.length; i++) {
            key[i] = 0;
        }
        if (rc == 0 || rc == 7) {
            /* 7 = ATN_ERR_STATE already enrolled */
            enrolled = true;
            return true;
        }
        return false;
    }

    /** Lab reconnect: drop locked slot so another x5 soak works. */
    public static synchronized void reenrollFresh() {
        AtnNative.dmon2faRevoke(LAB_ID);
        enrolled = false;
        ensureEnrolled();
    }

    /** Force re-enroll after native flush wiped the 2FA store. */
    public static synchronized void clearEnrolled() {
        enrolled = false;
    }
}