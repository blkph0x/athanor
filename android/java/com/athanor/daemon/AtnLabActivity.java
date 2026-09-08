package com.athanor.daemon;

import android.app.Activity;
import android.app.admin.DevicePolicyManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
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
    private static final int REQ_MIC = 42;

    private TextView status;
    private TextView boomBanner;
    private TextView updateStatus;
    private TextView logBox;
    private TextView voiceStats;
    private TextView ringBanner;
    private TextView contactsBox;
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

        updateStatus = new TextView(this);
        updateStatus.setTextSize(14f);
        updateStatus.setTypeface(Typeface.MONOSPACE);
        updateStatus.setText("update: idle");
        root.addView(updateStatus);

        TextView note = new TextView(this);
        note.setText("LAB (DEC-0040/0046/0047): Device Admin + wrong PIN"
                + " x failMax => BOOM. Hub silence (boom_silence_s) also"
                + " BOOMs. Compromise vote YES/NO when hub opens a vote."
                + " Keys kept under log_only.");
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

        Button updReq = new Button(this);
        updReq.setText("Request update (U?)");
        updReq.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                boolean ok = AtnUpdate.requestFromHub();
                appendLog(ok ? "update request U? sent (tunnel)"
                        : "update request U? failed");
                paintStatus();
            }
        });
        root.addView(updReq);

        TextView voiceNote = new TextView(this);
        voiceNote.setText("Voice (DEC-0050): P2P E2E primary; hub relay is"
                + " opaque nested seal. Lab Call hub-loop = echo self-test.");
        root.addView(voiceNote);

        voiceStats = new TextView(this);
        voiceStats.setTypeface(Typeface.MONOSPACE);
        voiceStats.setTextSize(12f);
        voiceStats.setText("voice: idle");
        root.addView(voiceStats);

        ringBanner = new TextView(this);
        ringBanner.setTextSize(18f);
        ringBanner.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        ringBanner.setTextColor(Color.rgb(180, 40, 40));
        ringBanner.setVisibility(android.view.View.GONE);
        root.addView(ringBanner);

        contactsBox = new TextView(this);
        contactsBox.setTypeface(Typeface.MONOSPACE);
        contactsBox.setTextSize(12f);
        root.addView(contactsBox);
        refreshContacts(contactsBox);

        Button addContact = new Button(this);
        addContact.setText("Ensure demo contact 'hub'");
        addContact.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                AtnContacts.addDemoHub(AtnLabActivity.this);
                refreshContacts(contactsBox);
                appendLog("contacts updated");
            }
        });
        root.addView(addContact);

        Button callHub = new Button(this);
        callHub.setText("Call hub-loop");
        callHub.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                ensureMicThenCall();
            }
        });
        root.addView(callHub);

        Button callContact = new Button(this);
        callContact.setText("Call first contact (lab)");
        callContact.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                java.util.List<AtnContacts.Entry> list =
                        AtnContacts.load(AtnLabActivity.this);
                if (list.isEmpty()) {
                    appendLog("no contacts — tap Ensure demo contact");
                    return;
                }
                appendLog("call contact " + list.get(0).label
                        + " (P2P when reachable; else hub sealed via native)");
                ensureMicThenCall();
            }
        });
        root.addView(callContact);

        Button answer = new Button(this);
        answer.setText("Answer");
        answer.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
        boolean ok = AtnVoice.answer();
        AtnVoice.clearRing();
        appendLog(ok ? "voice answer" : "voice answer failed state="
                + AtnVoice.stateName());
            }
        });
        root.addView(answer);

        Button muteBtn = new Button(this);
        muteBtn.setText("Mute / unmute");
        muteBtn.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                boolean next = !AtnVoice.isMute();
                AtnVoice.setMute(next);
                appendLog(next ? "muted (stop frames)" : "unmuted");
            }
        });
        root.addView(muteBtn);

        Button hangup = new Button(this);
        hangup.setText("Hangup");
        hangup.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                boolean ok = AtnVoice.hangup();
                appendLog(ok ? "voice hangup" : "hangup ignored");
            }
        });
        root.addView(hangup);

        Button voteYes = new Button(this);
        voteYes.setText("Compromise vote YES");
        voteYes.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                if (AtnCompromise.openVoteId() <= 0) {
                    appendLog("no open compromise vote from hub");
                    return;
                }
                boolean ok = AtnCompromise.sendVote(true);
                appendLog(ok ? "sent vote_yes" : "vote_yes send failed");
            }
        });
        root.addView(voteYes);

        Button voteNo = new Button(this);
        voteNo.setText("Compromise vote NO");
        voteNo.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                if (AtnCompromise.openVoteId() <= 0) {
                    appendLog("no open compromise vote from hub");
                    return;
                }
                boolean ok = AtnCompromise.sendVote(false);
                appendLog(ok ? "sent vote_no" : "vote_no send failed");
            }
        });
        root.addView(voteNo);

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
        if (getIntent() != null && getIntent().getBooleanExtra("reconnect", false)) {
            startDaemon(true);
        } else if (getIntent() != null && getIntent().getBooleanExtra("autostart", false)) {
            /* Cold start: do not ACTION_RECONNECT (resets boom + races HS). */
            startDaemon(false);
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
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_MIC) {
            if (grantResults != null && grantResults.length > 0
                    && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                appendLog("RECORD_AUDIO granted");
                startHubLoopCall();
            } else {
                appendLog("RECORD_AUDIO denied — voice call aborted");
            }
        }
    }

    private void ensureMicThenCall() {
        if (Build.VERSION.SDK_INT >= 23) {
            if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                        new String[] { android.Manifest.permission.RECORD_AUDIO },
                        REQ_MIC);
                appendLog("requesting RECORD_AUDIO");
                return;
            }
        }
        startHubLoopCall();
    }

    private void startHubLoopCall() {
        int st = AtnNative.tunState();
        if (st != AtnNative.TUN_ESTABLISHED) {
            appendLog("voice call needs ESTABLISHED (state=" + stateName(st) + ")");
            return;
        }
        int id = (int) (System.currentTimeMillis() & 0x7fffffff);
        if (id == 0) {
            id = 1;
        }
        boolean ok = AtnVoice.callHubLoop(id);
        appendLog(ok ? "voice call hub-loop id=" + id
                : "voice call failed (busy or tunnel down)");
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
            if (rc != 0) {
                return;
            }
            /* rc=0 is send-only; wait briefly for hub echo (real liveness). */
            byte[] back = new byte[64];
            int n = 0;
            int i;
            for (i = 0; i < 15; i++) {
                n = AtnNative.tunRecv(back, 200);
                if (n > 0) {
                    AtnLabBoom.noteHubContact();
                    appendLog("ping echo n=" + n + " (hub live)");
                    return;
                }
            }
            appendLog("ping: no echo in ~3s (UDP return path dead? Start/reconnect)");
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
            appendLog("app-code fail #" + fails + "/" + AtnLabBoom.failMax()
                    + " verifyRc=" + vrc);
            if (vrc == AtnNative.ERR_LOCKOUT || fails >= AtnLabBoom.failMax()) {
                AtnLabBoom.trigger("wrong app code x" + AtnLabBoom.failMax()
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
        String compLine = "";
        if (AtnCompromise.openVoteId() > 0) {
            compLine = "\ncompromise vote OPEN id=" + AtnCompromise.openVoteId()
                    + " target=" + AtnCompromise.openTarget();
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
                    + AtnLabBoom.failMax()
                    + "\nappCodeFails=" + AtnLabBoom.pinFails()
                    + "/" + AtnLabBoom.failMax();
            if (!admin) {
                line += "\nENABLE DEVICE ADMIN then lock + wrong PIN x"
                        + AtnLabBoom.failMax();
            } else {
                boolean net = false;
                try {
                    android.net.ConnectivityManager cm =
                            (android.net.ConnectivityManager)
                                    getSystemService(CONNECTIVITY_SERVICE);
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
                } catch (Throwable ignored) {
                    /* net stays false */
                }
                long watch = AtnLabBoom.watchSeconds(net, st);
                line += "\nnet=" + (net ? "UP" : "DOWN/airplane");
                if (!AtnLabBoom.sawEstablished()) {
                    line += "\nunreachable armed after first ESTABLISHED";
                } else if (AtnLabBoom.meshLive(net)) {
                    line += "\nunreachable timer OFF (hub live)";
                } else {
                    line += "\nunreachable " + watch
                            + "s / " + (AtnLabBoom.silenceMs() / 1000L)
                            + "s (silence/airplane)";
                }
                if (st == AtnNative.TUN_ESTABLISHED
                        && AtnLabBoom.meshLive(net)) {
                    line += "\nMESH UP";
                } else if (st == AtnNative.TUN_ESTABLISHED) {
                    line += "\nESTABLISHED (stale — waiting hub/net)";
                } else if (st == AtnNative.TUN_HANDSHAKE) {
                    line += "\nHANDSHAKE - waiting hub (no BOOM until joined)";
                } else {
                    line += "\ntap Start/reconnect after hub is listening";
                }
            }
            line += compLine;
        } catch (Throwable t) {
            line = "native not ready: " + t.getMessage();
        }
        status.setText(line);
        if (updateStatus != null) {
            updateStatus.setText(AtnUpdate.statusLine());
        }
        if (voiceStats != null) {
            voiceStats.setText("voice: " + AtnVoice.statsText());
        }
        if (ringBanner != null) {
            if (AtnVoice.isRinging()) {
                ringBanner.setVisibility(android.view.View.VISIBLE);
                ringBanner.setText("RING: " + AtnVoice.ringLabel()
                        + " — tap Answer");
            } else {
                ringBanner.setVisibility(android.view.View.GONE);
            }
        }
    }

    private void refreshContacts(TextView box) {
        if (box == null) {
            return;
        }
        java.util.List<AtnContacts.Entry> list = AtnContacts.load(this);
        StringBuilder sb = new StringBuilder("contacts:\n");
        if (list.isEmpty()) {
            sb.append("  (empty)\n");
        } else {
            for (AtnContacts.Entry e : list) {
                sb.append("  ").append(e.label).append(" ")
                        .append(e.ipv4).append(':').append(e.port).append('\n');
            }
        }
        box.setText(sb.toString());
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
