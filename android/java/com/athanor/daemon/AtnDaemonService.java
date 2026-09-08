package com.athanor.daemon;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

/**
 * Foreground mesh daemon. REQ-4.1. No Firebase, no Play services.
 * Loads Keystore-wrapped secrets into native dmon (DEC-0016/0017).
 * DEC-0039: lab 30s hub-silence BOOM (diag/log_only, not a brick).
 * Auto-reconnect: CLOSED / stuck HANDSHAKE / network change → retry.
 */
public class AtnDaemonService extends Service {
    private static final String TAG = "atn-daemon";
    private static final String CH = "atn-mesh";
    public static final String ACTION_RECONNECT = "com.athanor.daemon.RECONNECT";
    public static final String ACTION_LAB_BOOM = "com.athanor.daemon.LAB_BOOM";
    /* DEC-0017: hb bucket 60s. DEC-0022: 1s pump, 15s KA so IPv4 NAT lives. */
    private static final long BUCKET_MS = 60L * 1000L;
    private static final long TICK_MS = 1000L;
    private static final int KA_TICKS = 15;
    /* Cellular CGNAT: probe every 3s so return path stays warm. */
    private static final int PROBE_TICKS = 3;
    /* Stuck HANDSHAKE → full reconnect (~15–20s). */
    private static final int HS_STUCK_TICKS = 18;
    private static final long RECONNECT_BACKOFF_MIN_MS = 1000L;
    private static final long RECONNECT_BACKOFF_CAP_MS = 60L * 1000L;

    private final Handler tickHandler = new Handler(Looper.getMainLooper());
    private boolean labTun;
    private boolean nativeReady;
    private long lastHbBucket = -1L;
    private int kaTicks;
    private int probeTicks;
    private int lastNotifState = -1;
    private boolean boomNotified;
    private int prevTunState = -1;
    private int hsStuckTicks;
    private long reconnectBackoffMs = RECONNECT_BACKOFF_MIN_MS;
    private boolean reconnectPending;
    private boolean everJoined;
    private boolean autoReconnecting;
    private int lastNetTransport = -1; /* 1=wifi 2=cell 0=other */
    private ConnectivityManager.NetworkCallback netCb;
    private final Runnable reconnectRunnable = new Runnable() {
        @Override
        public void run() {
            reconnectPending = false;
            if (AtnLabBoom.isDead() || !nativeReady) {
                autoReconnecting = false;
                return;
            }
            Log.i(TAG, "auto-reconnect: startLabTunnel backoff was "
                    + reconnectBackoffMs + "ms");
            startLabTunnel();
            maybeUpdateNotif(AtnNative.tunState());
        }
    };
    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            long bucket = System.currentTimeMillis() / BUCKET_MS;
            if (AtnLabBoom.isDead()) {
                labTun = false;
                autoReconnecting = false;
                cancelScheduledReconnect();
                if (!boomNotified) {
                    boomNotified = true;
                    Log.w(TAG, "LAB BOOM: " + AtnLabBoom.reason());
                    pushBoomNotif();
                }
                tickHandler.postDelayed(this, TICK_MS);
                return;
            }
            if (labTun) {
                int st;
                int i;
                st = AtnNative.tunState();
                boolean net = networkUp();
                if (st == AtnNative.TUN_HANDSHAKE) {
                    for (i = 0; i < 8; i++) {
                        if (AtnNative.tunPump(0) != 0) {
                            break;
                        }
                    }
                    st = AtnNative.tunState();
                    /* Still retry while waiting; DEC-0041 times out after join. */
                    if (st == AtnNative.TUN_HANDSHAKE && net) {
                        AtnNative.tunHsRetry();
                    }
                    if (st == AtnNative.TUN_HANDSHAKE) {
                        hsStuckTicks++;
                        if (hsStuckTicks >= HS_STUCK_TICKS) {
                            Log.w(TAG, "HANDSHAKE stuck " + hsStuckTicks
                                    + "s — full reconnect");
                            hsStuckTicks = 0;
                            scheduleAutoReconnect(true);
                            tickHandler.postDelayed(this, TICK_MS);
                            return;
                        }
                    } else {
                        hsStuckTicks = 0;
                    }
                } else {
                    hsStuckTicks = 0;
                }
                if (st == AtnNative.TUN_ESTABLISHED) {
                    boolean fresh = prevTunState != AtnNative.TUN_ESTABLISHED;
                    AtnLabBoom.noteEstablished(fresh);
                    if (fresh) {
                        everJoined = true;
                        reconnectBackoffMs = RECONNECT_BACKOFF_MIN_MS;
                        autoReconnecting = false;
                        cancelScheduledReconnect();
                        /* DEC-0045: ask hub for current network-wide policy. */
                        AtnNative.tunSend(new byte[] { 'P', '?' });
                    }
                    kaTicks++;
                    if (kaTicks >= KA_TICKS) {
                        kaTicks = 0;
                        AtnNative.tunKeepalive();
                    }
                    probeTicks++;
                    if (probeTicks >= PROBE_TICKS || fresh) {
                        probeTicks = 0;
                        byte[] probe = new byte[] { 'L', 'A', 'B' };
                        AtnNative.tunSend(probe);
                    }
                    /*
                     * Drain via tunRecv — atn_dmon_tun_pump on ESTABLISHED
                     * feeds hb_ingest and drops LAB echoes, so liveness
                     * never saw hub contact (false silence BOOM).
                     * Drain deeply: hub APK push blasts many UC chunks;
                     * 8/tick dropped most of a ~100KB update (DEC-0048).
                     */
                    for (i = 0; i < 2048; i++) {
                        byte[] back = new byte[2048];
                        int n = AtnNative.tunRecv(back, 0);
                        if (n <= 0) {
                            break;
                        }
                        AtnLabBoom.noteHubContact();
                        if (n >= 1 && back[0] == (byte) 'P') {
                            AtnOrgPolicy.applyFromWire(AtnDaemonService.this, back, n);
                            continue;
                        }
                        if (n >= 1 && back[0] == (byte) 'C') {
                            boolean boom = AtnCompromise.applyFromWire(
                                    AtnDaemonService.this, back, n);
                            if (AtnLabBoom.isDead()) {
                                labTun = false;
                                boomNotified = true;
                                pushBoomNotif();
                            }
                            if (boom) {
                                continue;
                            }
                        }
                        if (n >= 1 && back[0] == (byte) 'U') {
                            AtnUpdate.applyFromWire(
                                    AtnDaemonService.this, back, n);
                            continue;
                        }
                        if (n >= 1 && back[0] == (byte) 'A') {
                            /* DEC-0049 voice — do not feed hb_ingest. */
                            AtnVoice.onFrame(back, n);
                            continue;
                        }
                        if (n < back.length) {
                            byte[] msg = new byte[n];
                            System.arraycopy(back, 0, msg, 0, n);
                            AtnNative.dmonHbIngest(msg);
                        } else {
                            AtnNative.dmonHbIngest(back);
                        }
                        Log.i(TAG, "lab recv " + n + " bytes");
                    }
                } else if (st == AtnNative.TUN_CLOSED) {
                    Log.w(TAG, "TUN_CLOSED — auto-reconnect");
                    scheduleAutoReconnect(false);
                }
                /*
                 * Skip silence BOOM while reconnecting, handshaking, or
                 * network down — keep retrying forever (backoff). Boom only
                 * via compromise / require / wrong PIN / user Start after dead.
                 */
                if (!autoReconnecting && net
                        && st == AtnNative.TUN_ESTABLISHED
                        && AtnLabBoom.maybeUnreachableBoom(net, st)) {
                    labTun = false;
                    boomNotified = true;
                    autoReconnecting = false;
                    cancelScheduledReconnect();
                    Log.w(TAG, "LAB BOOM: " + AtnLabBoom.reason());
                    pushBoomNotif();
                } else if (autoReconnecting || !net
                        || st == AtnNative.TUN_HANDSHAKE
                        || st == AtnNative.TUN_CLOSED) {
                    AtnLabBoom.pauseUnreachableWatch();
                }
                prevTunState = st;
                maybeUpdateNotif(st);
            } else if (!AtnLabBoom.isDead() && nativeReady
                    && (autoReconnecting || reconnectPending)) {
                AtnLabBoom.pauseUnreachableWatch();
                maybeUpdateNotif(AtnNative.TUN_CLOSED);
            } else if (AtnLabBoom.isSoakArmed() && !AtnLabBoom.isDead()
                    && everJoined && !autoReconnecting) {
                /* Idle after join without active reconnect — should not happen;
                 * start reconnect instead of silence BOOM. */
                scheduleAutoReconnect(false);
            }
            if (bucket != lastHbBucket) {
                lastHbBucket = bucket;
                if (labTun && AtnNative.tunState() == AtnNative.TUN_ESTABLISHED) {
                    AtnNative.dmonHbEmit(bucket);
                }
                AtnNative.dmonHbTick(bucket);
            }
            if (AtnNative.dmonRequire() != 0) {
                labTun = false;
                autoReconnecting = false;
                cancelScheduledReconnect();
                AtnKeystore.deleteWrap(AtnDaemonService.this);
                AtnLabBoom.clearEnrolled();
                AtnLabBoom.trigger("native require failed (flush)");
                Log.w(TAG, "hb UNTRUSTED/DEAD: wrap deleted");
                pushBoomNotif();
            }
            tickHandler.postDelayed(this, TICK_MS);
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
            startForeground(1, buildNotif("starting..."));
        }
        Log.i(TAG, "platform=" + AtnNative.platformId());
        boolean stub = AtnKnoxBuild.isStub();
        Log.i(TAG, "knoxStub=" + stub);
        boolean ks = AtnKeystore.ensureKey();
        Log.i(TAG, "keystore=" + ks);
        /* DEC-0038: lab stub skips USB charge-only + Knox password (release). */
        if (stub) {
            Log.i(TAG, "lab stub: skip USB/password Knox policy");
        } else {
            ComponentName admin = new ComponentName(this, AtnDeviceAdminReceiver.class);
            boolean usb = AtnKnoxPolicy.applyUsbChargeOnly(this);
            boolean pw = AtnKnoxPolicy.applyPasswordPolicy(this, admin);
            Log.i(TAG, "usb=" + usb + " password=" + pw);
        }
        nativeReady = ks && loadNative();
        if (nativeReady) {
            if (!AtnLabBoom.ensureEnrolled()) {
                Log.w(TAG, "lab 2FA enroll failed");
            }
            startLabTunnel();
            if (!labTun && !AtnLabBoom.isDead()) {
                scheduleAutoReconnect(false);
            }
        }
        registerNetCallback();
        tickHandler.postDelayed(ticker, TICK_MS);
    }

    private void registerNetCallback() {
        ConnectivityManager cm =
                (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null || netCb != null) {
            return;
        }
        netCb = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                tickHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        onNetworkEvent("available");
                    }
                });
            }

            @Override
            public void onLost(Network network) {
                tickHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        if (AtnLabBoom.isDead() || !nativeReady) {
                            return;
                        }
                        Log.i(TAG, "network lost — pause boom, reconnect when up");
                        AtnLabBoom.pauseUnreachableWatch();
                        autoReconnecting = true;
                        maybeUpdateNotif(AtnNative.TUN_CLOSED);
                    }
                });
            }

            @Override
            public void onCapabilitiesChanged(Network network,
                                              NetworkCapabilities caps) {
                if (caps == null) {
                    return;
                }
                final int transport;
                if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                    transport = 1;
                } else if (caps.hasTransport(
                        NetworkCapabilities.TRANSPORT_CELLULAR)) {
                    transport = 2;
                } else {
                    transport = 0;
                }
                tickHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        if (transport == lastNetTransport) {
                            return;
                        }
                        int prev = lastNetTransport;
                        lastNetTransport = transport;
                        /* First callback only seeds; WiFi↔cell forces reconnect. */
                        if (prev < 0) {
                            return;
                        }
                        String why = transport == 1 ? "wifi"
                                : (transport == 2 ? "cell" : "other");
                        onNetworkEvent("roam->" + why);
                    }
                });
            }
        };
        try {
            if (Build.VERSION.SDK_INT >= 24) {
                cm.registerDefaultNetworkCallback(netCb);
            } else {
                NetworkRequest req = new NetworkRequest.Builder()
                        .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                        .build();
                cm.registerNetworkCallback(req, netCb);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "net callback register failed", e);
            netCb = null;
        }
    }

    private void onNetworkEvent(String why) {
        if (AtnLabBoom.isDead() || !nativeReady) {
            return;
        }
        int st = AtnNative.tunState();
        /* After first join, or any non-ESTABLISHED: reconnect on net change. */
        if (everJoined || st != AtnNative.TUN_ESTABLISHED) {
            Log.i(TAG, "network " + why + " — reconnect (st=" + st + ")");
            AtnLabBoom.pauseUnreachableWatch();
            scheduleAutoReconnect(true);
        }
    }

    private void unregisterNetCallback() {
        if (netCb == null) {
            return;
        }
        ConnectivityManager cm =
                (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null) {
            try {
                cm.unregisterNetworkCallback(netCb);
            } catch (RuntimeException e) {
                /* ignore */
            }
        }
        netCb = null;
    }

    private void cancelScheduledReconnect() {
        tickHandler.removeCallbacks(reconnectRunnable);
        reconnectPending = false;
    }

    /**
     * Schedule startLabTunnel with exponential backoff (cap ~60s).
     * immediate=true still respects a short delay if already pending, but
     * resets to try sooner after stuck-HS / network events.
     */
    private void scheduleAutoReconnect(boolean immediate) {
        if (AtnLabBoom.isDead() || !nativeReady) {
            return;
        }
        labTun = false;
        autoReconnecting = true;
        hsStuckTicks = 0;
        prevTunState = -1;
        AtnLabBoom.pauseUnreachableWatch();
        maybeUpdateNotif(AtnNative.TUN_CLOSED);
        if (reconnectPending && !immediate) {
            return;
        }
        cancelScheduledReconnect();
        long delay = immediate ? Math.min(reconnectBackoffMs, 2000L)
                : reconnectBackoffMs;
        reconnectPending = true;
        Log.i(TAG, "schedule auto-reconnect in " + delay + "ms");
        tickHandler.postDelayed(reconnectRunnable, delay);
        /* Grow backoff for next failure; ESTABLISHED resets. */
        reconnectBackoffMs = Math.min(reconnectBackoffMs * 2L,
                RECONNECT_BACKOFF_CAP_MS);
    }

    private boolean networkUp() {
        ConnectivityManager cm =
                (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            return false;
        }
        Network n = cm.getActiveNetwork();
        if (n == null) {
            return false;
        }
        NetworkCapabilities caps = cm.getNetworkCapabilities(n);
        if (caps == null) {
            return false;
        }
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    }

    private Notification buildNotif(String body) {
        return new Notification.Builder(this, CH)
                .setContentTitle("Athanor lab")
                .setContentText(body)
                .setSmallIcon(android.R.drawable.ic_lock_lock)
                .build();
    }

    private void pushBoomNotif() {
        if (Build.VERSION.SDK_INT < 26) {
            return;
        }
        lastNotifState = -99;
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.notify(1, buildNotif("BOOM phone is dead now"));
        }
        Intent i = new Intent(ACTION_LAB_BOOM);
        i.setPackage(getPackageName());
        sendBroadcast(i);
    }

    private void maybeUpdateNotif(int st) {
        if (AtnLabBoom.isDead() || Build.VERSION.SDK_INT < 26) {
            return;
        }
        /* Distinct sentinel for reconnect banner vs raw CLOSED. */
        int key = (autoReconnecting && st != AtnNative.TUN_ESTABLISHED
                && st != AtnNative.TUN_HANDSHAKE) ? -2 : st;
        if (key == lastNotifState) {
            return;
        }
        lastNotifState = key;
        String body;
        if (st == AtnNative.TUN_ESTABLISHED) {
            body = "ESTABLISHED - mesh up";
        } else if (st == AtnNative.TUN_HANDSHAKE) {
            body = autoReconnecting ? "reconnect… HANDSHAKE"
                    : "HANDSHAKE - waiting hub";
        } else if (autoReconnecting) {
            body = "reconnect…";
        } else {
            body = "CLOSED - tap Start/reconnect";
        }
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.notify(1, buildNotif(body));
        }
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
        int pol = AtnNative.dmonSetPolicy(c.diag, c.flushMode, c.wipeArmed,
                c.outageClass);
        Log.i(TAG, "lab policy rc=" + pol + " diag=" + c.diag
                + " flush=" + c.flushMode);
        /* DEC-0046: wrapped org overlay overrides enroll seed when present. */
        AtnOrgPolicy wrapped = AtnOrgPolicy.loadWrapped(this);
        if (wrapped != null) {
            wrapped.apply(this);
        } else {
            AtnOrgPolicy seed = AtnOrgPolicy.defaults();
            seed.diag = c.diag;
            seed.flushMode = c.flushMode;
            seed.wipeArmed = c.wipeArmed;
            seed.outageClass = c.outageClass;
            seed.apply(this);
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
        hsStuckTicks = 0;
        if (labTun) {
            AtnLabBoom.armSoak();
        } else if (autoReconnecting && !AtnLabBoom.isDead()) {
            /* Init failed — keep retrying with backoff. */
            scheduleAutoReconnect(false);
        }
        Log.i(TAG, "lab tun rc=" + rc + " port=" + AtnNative.tunPort());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String act = intent != null ? intent.getAction() : null;
        if (ACTION_RECONNECT.equals(act) && nativeReady) {
            /* DEC-0039: Start clears lab BOOM for another soak cycle. */
            AtnLabBoom.reset();
            boomNotified = false;
            prevTunState = -1;
            kaTicks = 0;
            probeTicks = 0;
            hsStuckTicks = 0;
            reconnectBackoffMs = RECONNECT_BACKOFF_MIN_MS;
            autoReconnecting = false;
            cancelScheduledReconnect();
            AtnLabBoom.reenrollFresh();
            AtnLabBoom.armSoak();
            Log.i(TAG, "reconnect: lab BOOM reset; fresh tunnel (WiFi/5G path)");
            /*
             * Always re-bind + HS. Stale ESTABLISHED after WiFi→cellular
             * keeps UDP state while the 5-tuple/NAT path is dead; ping
             * then returns rc=0 with no hub echo → silence BOOM.
             */
            startLabTunnel();
            if (!labTun && !AtnLabBoom.isDead()) {
                scheduleAutoReconnect(false);
            } else {
                maybeUpdateNotif(AtnNative.tunState());
            }
        } else if (ACTION_LAB_BOOM.equals(act)) {
            labTun = false;
            boomNotified = true;
            autoReconnecting = false;
            cancelScheduledReconnect();
            Log.w(TAG, "LAB BOOM: " + AtnLabBoom.reason());
            pushBoomNotif();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        tickHandler.removeCallbacks(ticker);
        cancelScheduledReconnect();
        unregisterNetCallback();
        AtnNative.dmonFlush();
        AtnLabBoom.clearEnrolled();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
