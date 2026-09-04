package com.athanor.daemon;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

/**
 * Foreground mesh daemon. REQ-4.1. No Firebase, no Play services.
 */
public class AtnDaemonService extends Service {
    private static final String TAG = "atn-daemon";
    private static final String CH = "atn-mesh";

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel c = new NotificationChannel(
                    CH, "Athanor mesh", NotificationManager.IMPORTANCE_LOW);
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(c);
            }
            Notification n = new Notification.Builder(this, CH)
                    .setContentTitle("Athanor")
                    .setContentText("mesh member")
                    .setSmallIcon(android.R.drawable.ic_lock_lock)
                    .build();
            startForeground(1, n);
        }
        Log.i(TAG, "platform=" + AtnNative.platformId());
        ComponentName admin = new ComponentName(this, AtnDeviceAdminReceiver.class);
        boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(this);
        boolean pw = AtnKnoxPolicy.applyPasswordPolicy(this, admin);
        Log.i(TAG, "usb=" + usb + " password=" + pw);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
