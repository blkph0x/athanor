package com.athanor.daemon;

/**
 * Distinguishes in-tree Knox stubs from a Partner knoxsdk.jar (DEC-0019 / 0030).
 *
 * Stub classpath: {@code android/stubs/...} defines {@code ATN_STUB=true}.
 * Real jar path: {@code vendor/knox/knoxsdk.jar} — no {@code ATN_STUB} field.
 * {@code make android-java} switches automatically; product imports stay the same.
 */
public final class AtnKnoxBuild {
    private AtnKnoxBuild() {}

    /**
     * @return true if compiling/running against compile-only stubs (lab only).
     *         false when the Partner jar is on the classpath.
     */
    public static boolean isStub() {
        try {
            return com.samsung.android.knox.EnterpriseDeviceManager.class
                    .getField("ATN_STUB")
                    .getBoolean(null);
        } catch (NoSuchFieldException e) {
            return false;
        } catch (Exception e) {
            return true;
        }
    }
}
