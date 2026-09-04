package com.athanor.daemon;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

/**
 * Foreground mesh daemon. REQ-4.1. No Firebase, no Play services.
 * Loads Keystore-wrapped secrets into native dmon (DEC-0016/0017).
 */
public class AtnDaemonService extends Service {
    private static final String TAG = "atn-daemon";
    private static final String CH = "atn-mesh";
    /* DEC-0017: one hb bucket per 60s. */
    private static final long BUCKET_MS = 60L * 1000L;

    private final Handler tickHandler = new Handler(Looper.getMainLooper());
    private boolean labTun;
    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            long bucket = System.currentTimeMillis() / BUCKET_MS;
            AtnNative.dmonHbTick(bucket);
            if (labTun) {
                AtnNative.tunPump(0);
            }
            if (AtnNative.dmonRequire() != 0) {
                labTun = false;
                AtnKeystore.deleteWrap(AtnDaemonService.this);
                Log.w(TAG, "hb UNTRUSTED/DEAD: wrap deleted");
            }
            tickHandler.postDelayed(this, BUCKET_MS);
        }
    };

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
        boolean stub = AtnKnoxBuild.isStub();
        Log.i(TAG, "knoxStub=" + stub);
        boolean ks = AtnKeystore.ensureKey();
        Log.i(TAG, "keystore=" + ks);
        ComponentName admin = new ComponentName(this, AtnDeviceAdminReceiver.class);
        boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(this);
        boolean pw = AtnKnoxPolicy.applyPasswordPolicy(this, admin);
        Log.i(TAG, "usb=" + usb + " password=" + pw + " (false on stub)");
        if (ks && loadNative()) {
            startLabTunnel();
        }
        tickHandler.postDelayed(ticker, BUCKET_MS);
    }

    private boolean loadNative() {
        byte[] blob = AtnKeystore.loadWrap(this);
        byte[] pt;
        if (blob == null) {
            pt = new byte[AtnKeystore.PLAIN_LEN];
            if (AtnNative.randomBytes(pt) != 0) {
                Log.e(TAG, "rng failed");
                return false;
            }
            byte[] wrapped = AtnKeystore.wrap(pt);
            if (wrapped == null || !AtnKeystore.storeWrap(this, wrapped)) {
                Log.e(TAG, "wrap store failed");
                return false;
            }
        } else {
            pt = AtnKeystore.unwrap(blob);
            if (pt == null) {
                Log.e(TAG, "unwrap failed");
                return false;
            }
        }
        byte[] dk = new byte[32];
        byte[] ck = new byte[32];
        byte[] id = new byte[8];
        byte[] head = new byte[32];
        System.arraycopy(pt, 0, dk, 0, 32);
        System.arraycopy(pt, 32, ck, 0, 32);
        System.arraycopy(pt, 64, id, 0, 8);
        int rc = AtnNative.dmonLoad(dk, ck);
        if (rc == 0) {
            rc = AtnNative.dmonHbInit(id, 1, head);
        }
        Log.i(TAG, "dmon load rc=" + rc);
        for (int i = 0; i < pt.length; i++) {
            pt[i] = 0;
        }
        for (int i = 0; i < dk.length; i++) {
            dk[i] = 0;
        }
        for (int i = 0; i < ck.length; i++) {
            ck[i] = 0;
        }
        return rc == 0;
    }

    /* DEC-0021: filesDir/atn-node.conf. Missing/incomplete = do not connect. */
    private void startLabTunnel() {
        java.io.File f = new java.io.File(getFilesDir(), "atn-node.conf");
        if (!f.isFile()) {
            Log.i(TAG, "no atn-node.conf (do not connect)");
            return;
        }
        long sz = f.length();
        if (sz <= 0L || sz > 8192L) {
            Log.e(TAG, "atn-node.conf size");
            return;
        }
        byte[] raw = new byte[(int) sz];
        java.io.FileInputStream in = null;
        try {
            in = new java.io.FileInputStream(f);
            if (in.read(raw) != raw.length) {
                Log.e(TAG, "atn-node.conf short read");
                return;
            }
        } catch (java.io.IOException e) {
            Log.e(TAG, "atn-node.conf", e);
            return;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (java.io.IOException e) {
                    /* ignore */
                }
            }
        }
        String text;
        try {
            text = new String(raw, "UTF-8");
        } catch (java.io.UnsupportedEncodingException e) {
            return;
        }
        AtnNodeConfig c = AtnNodeConfig.parse(text);
        if (c == null || !c.ready()) {
            Log.e(TAG, "atn-node.conf not ready");
            return;
        }
        int rc = AtnNative.tunInitiator(c.ek);
        if (rc == 0) {
            rc = AtnNative.tunBind(0);
        }
        if (rc == 0) {
            rc = AtnNative.tunSetPeer(c.ipv4Host, c.port);
        }
        if (rc == 0) {
            rc = AtnNative.tunHsSend();
        }
        for (int i = 0; i < c.ek.length; i++) {
            c.ek[i] = 0;
        }
        labTun = rc == 0;
        Log.i(TAG, "lab tun rc=" + rc + " port=" + AtnNative.tunPort());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        tickHandler.removeCallbacks(ticker);
        AtnNative.dmonFlush();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
