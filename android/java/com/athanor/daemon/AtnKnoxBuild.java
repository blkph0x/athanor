package com.athanor.daemon;

/**
 * Distinguishes lab stub APK from Partner-jar builds (DEC-0019 / 0030 / 0038).
 *
 * On real Samsung devices the framework already provides
 * {@code com.samsung.android.knox.EnterpriseDeviceManager}, so probing
 * {@code ATN_STUB} on that class is unreliable at runtime. Lab vs release is
 * therefore a <b>compile-time</b> flag set by {@code make android-java}.
 */
public final class AtnKnoxBuild {
    /**
     * true  = stub lab APK (no vendor/knox/knoxsdk.jar at compile).
     * false = REAL knoxsdk.jar was on the javac classpath.
     * Generated: see {@link AtnKnoxBuildFlags}.
     */
    public static boolean isStub() {
        return AtnKnoxBuildFlags.STUB_BUILD;
    }

    private AtnKnoxBuild() {}
}
