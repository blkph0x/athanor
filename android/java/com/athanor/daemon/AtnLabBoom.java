package com.athanor.daemon;

/**
 * Lab-only BOOM state (DEC-0039/0041). Not a production Faraday brick.
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
    private static volatile boolean soakArmed;
    private static volatile long noNetSinceMs;
    private static volatile long noHubSinceMs;
    private static volatile int pinFails;
    private static volatile int deviceUnlockFails;
    private static volatile boolean enrolled;

    private AtnLabBoom() {}

    public static synchronized void reset() {
        dead = false;
        reason = "";
        lastHubMs = 0L;
        sawEstablished = false;
        soakArmed = false;
        noNetSinceMs = 0L;
        noHubSinceMs = 0L;
        pinFails = 0;
        deviceUnlockFails = 0;
    }

    /** Call when lab mesh attempt starts (tunnel bind/HS or reconnect). */
    public static synchronized void armSoak() {
        soakArmed = true;
        long now = System.currentTimeMillis();
        if (noHubSinceMs <= 0L) {
            noHubSinceMs = now;
        }
    }

    public static boolean isDead() {
        return dead;
    }

    public static boolean isSoakArmed() {
        return soakArmed;
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

    /** Seconds into unreachable watch (only after join). */
    public static synchronized long watchSeconds(boolean netUp, int tunState) {
        if (dead || !soakArmed || !sawEstablished) {
            return 0L;
        }
        long now = System.currentTimeMillis();
        long best = 0L;
        if (!netUp && noNetSinceMs > 0L) {
            best = Math.max(best, (now - noNetSinceMs) / 1000L);
        }
        if (lastHubMs > 0L) {
            best = Math.max(best, (now - lastHubMs) / 1000L);
        }
        return best;
    }

    public static synchronized void noteHubContact() {
        lastHubMs = System.currentTimeMillis();
        sawEstablished = true;
        noHubSinceMs = 0L;
    }

    public static synchronized void noteEstablished(boolean startSilenceClock) {
        sawEstablished = true;
        if (startSilenceClock && lastHubMs <= 0L) {
            lastHubMs = System.currentTimeMillis();
        }
        noHubSinceMs = 0L;
    }

    /**
     * DEC-0041 narrowed: unreachable BOOM only after the phone has
     * joined (sawEstablished). Airplane/handshake before join does not
     * BOOM. Fully enrolled production path is separate (Knox / !stub).
     * Lab stub: Device Admin + prior ESTABLISHED = soak-enrolled watch.
     */
    public static synchronized boolean maybeUnreachableBoom(boolean netUp,
                                                           int tunState) {
        if (dead || !soakArmed) {
            return false;
        }
        long now = System.currentTimeMillis();

        if (!netUp) {
            if (noNetSinceMs <= 0L) {
                noNetSinceMs = now;
            }
        } else {
            noNetSinceMs = 0L;
        }

        if (tunState != AtnNative.TUN_ESTABLISHED) {
            if (noHubSinceMs <= 0L) {
                noHubSinceMs = now;
            }
        } else {
            noHubSinceMs = 0L;
        }

        /* Not joined yet → no connection BOOM (user: only after join /
         * full enroll). Lock-screen K=5 is independent. */
        if (!sawEstablished) {
            return false;
        }

        if (noNetSinceMs > 0L && (now - noNetSinceMs) >= SILENCE_MS) {
            return trigger("airplane/no net >30s (lab)");
        }
        if (lastHubMs > 0L && (now - lastHubMs) >= SILENCE_MS) {
            String why = netUp
                    ? "hub silence >30s (lab)"
                    : "no net/airplane + hub silence >30s (lab)";
            return trigger(why);
        }
        return false;
    }

    /** @deprecated use {@link #maybeUnreachableBoom} */
    public static synchronized boolean maybeSilenceBoom(boolean netUp) {
        return maybeUnreachableBoom(netUp, AtnNative.TUN_CLOSED);
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
            enrolled = true;
            return true;
        }
        return false;
    }

    public static synchronized void reenrollFresh() {
        AtnNative.dmon2faRevoke(LAB_ID);
        enrolled = false;
        ensureEnrolled();
    }

    public static synchronized void clearEnrolled() {
        enrolled = false;
    }
}
