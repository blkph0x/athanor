package com.athanor.daemon;

import android.app.Activity;
import android.app.admin.DevicePolicyManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Lab launcher (DEC-0038/0039/0040). Live status + BOOM soak controls.
 * Not a production UI.
 */
public class AtnLabActivity extends Activity {
    private static final String TAG = "atn-lab";
    private static final long UI_MS = 500L;
    private static final int REQ_ADMIN = 41;

    private TextView status;
    private TextView boomBanner;
    private TextView logBox;
    private EditText codeBox;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final StringBuilder lines = new StringBuilder();
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            paintStatus();
            ui.postDelayed(this, UI_MS);
        }
    };
    private final BroadcastReceiver boomRx = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            appendLog("BOOM broadcast: " + AtnLabBoom.reason());
            paintStatus();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        boolean stub = AtnKnoxBuild.isStub();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setTextSize(22f);
        title.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        title.setText(stub ? "Athanor LAB (stub)" : "Athanor LAB");
        root.addView(title);

        boomBanner = new TextView(this);
        boomBanner.setTextSize(20f);
        boomBanner.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        boomBanner.setTextColor(Color.RED);
        boomBanner.setVisibility(android.view.View.GONE);
        root.addView(boomBanner);

        status = new TextView(this);
        status.setTextSize(16f);
        status.setTypeface(Typeface.MONOSPACE);
        status.setText("status: starting...");
        root.addView(status);

        TextView note = new TextView(this);
        note.setText("LAB (DEC-0040): enable Device Admin, lock the phone,"
                + " enter wrong PIN/password 5 times => BOOM.\n"
                + "Use real lock screen (not fingerprint-only). "
                + "Hub silence >30s also BOOMs. Keys kept (log_only).");
        root.addView(note);

        Button adminBtn = new Button(this);
        adminBtn.setText("Enable lock-screen watch (Device Admin)");
        adminBtn.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                requestDeviceAdmin();
            }
        });
        root.addView(adminBtn);

        Button start = new Button(this);
        start.setText("Start / reconnect mesh");
        start.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                appendLog("Start/reconnect pressed (clears lab BOOM)");
                startDaemon(true);
            }
        });
        root.addView(start);

        Button ping = new Button(this);
        ping.setText("Send lab ping");
        ping.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                sendPing();
            }
        });
        root.addView(ping);

        codeBox = new EditText(this);
        codeBox.setHint("optional app 2FA soak (not lock screen)");
        codeBox.setSingleLine(true);
        root.addView(codeBox);

        Button submit = new Button(this);
        submit.setText("Submit app code (optional)");
        submit.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                submitCode();
            }
        });
        root.addView(submit);

        logBox = new TextView(this);
        logBox.setTypeface(Typeface.MONOSPACE);
        logBox.setTextSize(12f);
        logBox.setText("events:\n");
        ScrollView scroll = new ScrollView(this);
        scroll.addView(logBox);
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        appendLog("knoxStub=" + stub);
        if (!AtnDeviceAdminReceiver.isAdminActive(this)) {
            appendLog("Device Admin OFF - tap Enable lock-screen watch");
        } else {
            appendLog("Device Admin ON - lock phone and fail PIN x5");
        }
        if (getIntent() != null && getIntent().getBooleanExtra("autostart", false)) {
            startDaemon(true);
        }
        if (getIntent() != null && getIntent().getBooleanExtra("request_admin", false)) {
            if (!AtnDeviceAdminReceiver.isAdminActive(this)) {
                appendLog("enroll requested Device Admin prompt");
                requestDeviceAdmin();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_ADMIN) {
            boolean on = AtnDeviceAdminReceiver.isAdminActive(this);
            appendLog(on ? "Device Admin activated"
                    : "Device Admin NOT activated (user declined)");
            paintStatus();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        IntentFilter f = new IntentFilter(AtnDaemonService.ACTION_LAB_BOOM);
        registerReceiver(boomRx, f);
        int n = AtnDeviceAdminReceiver.failedUnlockAttempts(this);
        if (n >= 0) {
            AtnLabBoom.noteDeviceUnlockFail(n);
        }
        ui.removeCallbacks(refresh);
        ui.post(refresh);
    }

    @Override
    protected void onPause() {
        ui.removeCallbacks(refresh);
        try {
            unregisterReceiver(boomRx);
        } catch (IllegalArgumentException e) {
            /* not registered */
        }
        super.onPause();
    }

    private void requestDeviceAdmin() {
        if (AtnDeviceAdminReceiver.isAdminActive(this)) {
            appendLog("Device Admin already active");
            return;
        }
        Intent intent = new Intent(DevicePolicyManager.ACTION_ADD_DEVICE_ADMIN);
        intent.putExtra(DevicePolicyManager.EXTRA_DEVICE_ADMIN,
                AtnDeviceAdminReceiver.component(this));
        intent.putExtra(DevicePolicyManager.EXTRA_ADD_EXPLANATION,
                "Athanor LAB watches lock-screen unlock fails. "
                        + "After 5 wrong PIN/password attempts the lab "
                        + "shows BOOM (keys kept on diag/log_only).");
        startActivityForResult(intent, REQ_ADMIN);
        appendLog("system Device Admin prompt opened");
    }

    private void startDaemon(boolean reconnect) {
        Intent svc = new Intent(this, AtnDaemonService.class);
        if (reconnect) {
            svc.setAction(AtnDaemonService.ACTION_RECONNECT);
        }
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }
        appendLog("service intent sent");
    }

    private void sendPing() {
        try {
            if (AtnLabBoom.isDead()) {
                appendLog("ping skipped: BOOM");
                return;
            }
            int st = AtnNative.tunState();
            if (st != AtnNative.TUN_ESTABLISHED) {
                appendLog("ping skipped: state=" + stateName(st));
                return;
            }
            byte[] msg = new byte[] { 'p', 'i', 'n', 'g' };
            int rc = AtnNative.tunSend(msg);
            appendLog("tunSend ping rc=" + rc);
        } catch (Throwable t) {
            appendLog("ping error: " + t.getMessage());
        }
    }

    private void submitCode() {
        if (AtnLabBoom.isDead()) {
            appendLog("code ignored: already BOOM");
            return;
        }
        String typed = codeBox.getText() != null
                ? codeBox.getText().toString().trim() : "";
        if (typed.length() == 0) {
            appendLog("empty code ignored");
            return;
        }
        try {
            if (!AtnLabBoom.ensureEnrolled()) {
                appendLog("2FA enroll failed - is daemon up?");
                return;
            }
            byte[] chal = new byte[32];
            int crc = AtnNative.dmon2faChallenge(AtnLabBoom.LAB_ID, chal);
            if (crc != 0) {
                appendLog("challenge rc=" + crc);
                return;
            }
            byte[] resp = new byte[64];
            byte[] raw = typed.getBytes("UTF-8");
            int n = raw.length < 64 ? raw.length : 64;
            System.arraycopy(raw, 0, resp, 0, n);
            int vrc = AtnNative.dmon2faVerify(AtnLabBoom.LAB_ID, chal, resp);
            int fails = AtnLabBoom.noteWrongCode();
            appendLog("app-code fail #" + fails + "/" + AtnLabBoom.FAIL_MAX
                    + " verifyRc=" + vrc);
            if (vrc == AtnNative.ERR_LOCKOUT || fails >= AtnLabBoom.FAIL_MAX) {
                AtnLabBoom.trigger("wrong app code x" + AtnLabBoom.FAIL_MAX
                        + " (lab)");
                appendLog("BOOM phone is dead now");
                Intent svc = new Intent(this, AtnDaemonService.class);
                svc.setAction(AtnDaemonService.ACTION_LAB_BOOM);
                if (Build.VERSION.SDK_INT >= 26) {
                    startForegroundService(svc);
                } else {
                    startService(svc);
                }
            }
            codeBox.setText("");
        } catch (Throwable t) {
            appendLog("code error: " + t.getMessage());
        }
        paintStatus();
    }

    private void paintStatus() {
        if (AtnLabBoom.isDead()) {
            boomBanner.setVisibility(android.view.View.VISIBLE);
            boomBanner.setText("BOOM phone is dead now\n" + AtnLabBoom.reason());
            status.setText("LAB DEAD (diag/log_only - keys kept)\n"
                    + "tap Start/reconnect to reset soak");
            return;
        }
        boomBanner.setVisibility(android.view.View.GONE);
        boolean admin = AtnDeviceAdminReceiver.isAdminActive(this);
        int unlockFails = AtnDeviceAdminReceiver.failedUnlockAttempts(this);
        if (unlockFails < 0) {
            unlockFails = AtnLabBoom.deviceUnlockFails();
        }
        String line;
        try {
            int st = AtnNative.tunState();
            int port = AtnNative.tunPort();
            line = "state=" + stateName(st)
                    + "  localUDP=" + port
                    + "\nknoxStub=" + AtnKnoxBuild.isStub()
                    + "  platform=" + AtnNative.platformId()
                    + "\ndeviceAdmin=" + (admin ? "ON" : "OFF")
                    + "  unlockFails=" + unlockFails + "/"
                    + AtnKnoxPolicy.PASSWORD_FAIL_FLUSH
                    + "\nappCodeFails=" + AtnLabBoom.pinFails()
                    + "/" + AtnLabBoom.FAIL_MAX;
            if (!admin) {
                line += "\nENABLE DEVICE ADMIN then lock + wrong PIN x5";
            } else {
                long watch = AtnLabBoom.watchSeconds(
                        /* net hint unused in display path */ true, st);
                try {
                    android.net.ConnectivityManager cm =
                            (android.net.ConnectivityManager)
                                    getSystemService(CONNECTIVITY_SERVICE);
                    boolean net = false;
                    if (cm != null) {
                        android.net.Network n = cm.getActiveNetwork();
                        if (n != null) {
                            android.net.NetworkCapabilities caps =
                                    cm.getNetworkCapabilities(n);
                            net = caps != null && caps.hasCapability(
                                    android.net.NetworkCapabilities
                                            .NET_CAPABILITY_INTERNET);
                        }
                    }
                    watch = AtnLabBoom.watchSeconds(net, st);
                    line += "\nnet=" + (net ? "UP" : "DOWN/airplane")
                            + "  unreachable " + watch + "s / 30s";
                } catch (Throwable t) {
                    line += "\nunreachable watch " + watch + "s / 30s";
                }
                if (st == AtnNative.TUN_ESTABLISHED) {
                    line += "\nMESH UP";
                } else if (st == AtnNative.TUN_HANDSHAKE) {
                    line += "\nHANDSHAKE - BOOM if no hub/net in 30s";
                } else {
                    line += "\ntap Start/reconnect after hub is listening";
                }
            }
        } catch (Throwable t) {
            line = "native not ready: " + t.getMessage()
                    + "\ndeviceAdmin=" + (admin ? "ON" : "OFF");
        }
        status.setText(line);
    }

    private static String stateName(int st) {
        if (st == AtnNative.TUN_CLOSED) {
            return "CLOSED";
        }
        if (st == AtnNative.TUN_HANDSHAKE) {
            return "HANDSHAKE";
        }
        if (st == AtnNative.TUN_ESTABLISHED) {
            return "ESTABLISHED";
        }
        return "UNKNOWN(" + st + ")";
    }

    private void appendLog(String s) {
        Log.i(TAG, s);
        lines.append(s).append('\n');
        if (lines.length() > 4000) {
            lines.delete(0, lines.length() - 3000);
        }
        if (logBox != null) {
            logBox.setText("events:\n" + lines.toString());
        }
    }
}
