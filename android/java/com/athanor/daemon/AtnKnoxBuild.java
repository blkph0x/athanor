package com.athanor.daemon;

/**
 * Distinguishes in-tree Knox stubs from a Partner knoxsdk.jar.
 * Real Samsung classes have no ATN_STUB field (DEC-0019).
 */
public final class AtnKnoxBuild {
    private AtnKnoxBuild() {}

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
