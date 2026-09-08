package com.athanor.daemon;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.util.Log;

/**
 * PackageInstaller session status for tunnel APK self-update (DEC-0048).
 */
public final class AtnInstallReceiver extends BroadcastReceiver {
    private static final String TAG = "atn-upd";
    public static final String ACTION =
            "com.athanor.daemon.UPDATE_INSTALL_STATUS";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null) {
            return;
        }
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                PackageInstaller.STATUS_FAILURE);
        String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        if (msg == null) {
            msg = "";
        }
        switch (status) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION:
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm != null) {
                    confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    try {
                        context.startActivity(confirm);
                        AtnUpdate.setStatus("install: awaiting user confirm");
                        Log.i(TAG, "install pending user confirm");
                    } catch (Exception e) {
                        AtnUpdate.setStatus("install: confirm UI failed");
                        Log.w(TAG, "install confirm start", e);
                    }
                } else {
                    AtnUpdate.setStatus("install: pending confirm (no intent)");
                    Log.w(TAG, "STATUS_PENDING_USER_ACTION without EXTRA_INTENT");
                }
                break;
            case PackageInstaller.STATUS_SUCCESS:
                AtnUpdate.setStatus("install: success");
                Log.i(TAG, "install success (PackageInstaller)");
                break;
            default:
                AtnUpdate.setStatus("install: failed status=" + status
                        + (msg.length() > 0 ? " " + msg : ""));
                Log.w(TAG, "install failed status=" + status
                        + (msg.length() > 0 ? (" msg=" + msg) : "")
                        + " (staged APK kept)");
                break;
        }
    }
}
