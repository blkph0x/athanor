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
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Lab launcher (DEC-0038/0039/0040/0050). Mesh / Call / Logs tabs.
 * Not a production UI.
 */
public class AtnLabActivity extends Activity {
    private static final String TAG = "atn-lab";
    private static final long UI_MS = 500L;
    private static final int REQ_ADMIN = 41;
    private static final int REQ_MIC = 42;
    private static final int TAB_MESH = 0;
    private static final int TAB_CALL = 1;
    private static final int TAB_LOGS = 2;

    private TextView status;
    private TextView boomBanner;
    private TextView updateStatus;
    private TextView logBox;
    private TextView voiceStats;
    private TextView ringBanner;
    private TextView contactsBox;
    private TextView callMeshBanner;
    private TextView callPeer;
    private TextView callState;
    private TextView callDuration;
    private TextView callRoute;
    private TextView callCodec;
    private EditText codeBox;
    private Button tabMeshBtn;
    private Button tabCallBtn;
    private Button tabLogsBtn;
    private ScrollView meshScroll;
    private ScrollView callScroll;
    private ScrollView logsScroll;
    private int activeTab = TAB_MESH;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final StringBuilder lines = new StringBuilder();
    private int lastLoggedTun = -999;
    private String lastLoggedVoice = "";
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
        float dens = getResources().getDisplayMetrics().density;
        int pad = (int) (12 * dens);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setTextSize(20f);
        title.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        title.setText(stub ? "Athanor LAB (stub)" : "Athanor LAB");
        root.addView(title);

        boomBanner = new TextView(this);
        boomBanner.setTextSize(18f);
        boomBanner.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        boomBanner.setTextColor(Color.RED);
        boomBanner.setVisibility(View.GONE);
        root.addView(boomBanner);

        LinearLayout tabBar = new LinearLayout(this);
        tabBar.setOrientation(LinearLayout.HORIZONTAL);
        tabMeshBtn = tabButton("Mesh", TAB_MESH);
        tabCallBtn = tabButton("Call", TAB_CALL);
        tabLogsBtn = tabButton("Logs", TAB_LOGS);
        tabBar.addView(tabMeshBtn, tabLp());
        tabBar.addView(tabCallBtn, tabLp());
        tabBar.addView(tabLogsBtn, tabLp());
        root.addView(tabBar);

        meshScroll = new ScrollView(this);
        meshScroll.addView(buildMeshPanel());
        root.addView(meshScroll, fillLp());

        callScroll = new ScrollView(this);
        callScroll.addView(buildCallPanel());
        callScroll.setVisibility(View.GONE);
        root.addView(callScroll, fillLp());

        logsScroll = new ScrollView(this);
        logBox = new TextView(this);
        logBox.setTypeface(Typeface.MONOSPACE);
        logBox.setTextSize(12f);
        logBox.setText("events:\n");
        logsScroll.addView(logBox);
        logsScroll.setVisibility(View.GONE);
        root.addView(logsScroll, fillLp());

        setContentView(root);
        showTab(TAB_MESH);
        appendLog("knoxStub=" + stub);
        if (!AtnDeviceAdminReceiver.isAdminActive(this)) {
            appendLog("Device Admin OFF - tap Enable lock-screen watch");
        } else {
            appendLog("Device Admin ON - lock phone and fail PIN x5");
        }
        if (getIntent() != null && getIntent().getBooleanExtra("reconnect", false)) {
            startDaemon(true);
        } else if (getIntent() != null && getIntent().getBooleanExtra("autostart", false)) {
            startDaemon(false);
        }
        if (getIntent() != null && getIntent().getBooleanExtra("request_admin", false)) {
            if (!AtnDeviceAdminReceiver.isAdminActive(this)) {
                appendLog("enroll requested Device Admin prompt");
                requestDeviceAdmin();
            }
        }
    }

    private Button tabButton(String label, final int tab) {
        Button b = new Button(this);
        b.setText(label);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showTab(tab);
            }
        });
        return b;
    }

    private static LinearLayout.LayoutParams tabLp() {
        return new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
    }

    private static LinearLayout.LayoutParams fillLp() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
    }

    private void showTab(int tab) {
        activeTab = tab;
        meshScroll.setVisibility(tab == TAB_MESH ? View.VISIBLE : View.GONE);
        callScroll.setVisibility(tab == TAB_CALL ? View.VISIBLE : View.GONE);
        logsScroll.setVisibility(tab == TAB_LOGS ? View.VISIBLE : View.GONE);
        styleTab(tabMeshBtn, tab == TAB_MESH);
        styleTab(tabCallBtn, tab == TAB_CALL);
        styleTab(tabLogsBtn, tab == TAB_LOGS);
    }

    private void styleTab(Button b, boolean on) {
        b.setTypeface(Typeface.SANS_SERIF, on ? Typeface.BOLD : Typeface.NORMAL);
        b.setBackgroundColor(on ? Color.rgb(40, 90, 140) : Color.rgb(60, 60, 60));
        b.setTextColor(Color.WHITE);
    }

    private LinearLayout buildMeshPanel() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);

        status = new TextView(this);
        status.setTextSize(15f);
        status.setTypeface(Typeface.MONOSPACE);
        status.setText("status: starting...");
        p.addView(status);

        updateStatus = new TextView(this);
        updateStatus.setTextSize(13f);
        updateStatus.setTypeface(Typeface.MONOSPACE);
        updateStatus.setText("update: idle");
        p.addView(updateStatus);

        TextView note = new TextView(this);
        note.setText("Mesh: Device Admin + wrong PIN x failMax => BOOM."
                + " Hub silence also BOOMs. Compromise votes when hub opens.");
        p.addView(note);

        p.addView(btn("Enable lock-screen watch (Device Admin)", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                requestDeviceAdmin();
            }
        }));
        p.addView(btn("Start / reconnect mesh", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                appendLog("Start/reconnect pressed (clears lab BOOM)");
                startDaemon(true);
            }
        }));
        p.addView(btn("Send lab ping", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendPing();
            }
        }));
        p.addView(btn("Request update (U?)", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean ok = AtnUpdate.requestFromHub();
                appendLog(ok ? "update request U? sent (tunnel)"
                        : "update request U? failed");
                paintStatus();
            }
        }));
        p.addView(btn("Compromise vote YES", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (AtnCompromise.openVoteId() <= 0) {
                    appendLog("no open compromise vote from hub");
                    return;
                }
                boolean ok = AtnCompromise.sendVote(true);
                appendLog(ok ? "sent vote_yes" : "vote_yes send failed");
            }
        }));
        p.addView(btn("Compromise vote NO", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (AtnCompromise.openVoteId() <= 0) {
                    appendLog("no open compromise vote from hub");
                    return;
                }
                boolean ok = AtnCompromise.sendVote(false);
                appendLog(ok ? "sent vote_no" : "vote_no send failed");
            }
        }));

        codeBox = new EditText(this);
        codeBox.setHint("optional app 2FA soak (not lock screen)");
        codeBox.setSingleLine(true);
        p.addView(codeBox);
        p.addView(btn("Submit app code (optional)", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                submitCode();
            }
        }));
        return p;
    }

    private LinearLayout buildCallPanel() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);

        callMeshBanner = new TextView(this);
        callMeshBanner.setTextSize(16f);
        callMeshBanner.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        callMeshBanner.setText("mesh: …");
        p.addView(callMeshBanner);

        ringBanner = new TextView(this);
        ringBanner.setTextSize(18f);
        ringBanner.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        ringBanner.setTextColor(Color.rgb(180, 40, 40));
        ringBanner.setVisibility(View.GONE);
        p.addView(ringBanner);

        callPeer = monoLine();
        callState = monoLine();
        callDuration = monoLine();
        callRoute = monoLine();
        callCodec = monoLine();
        voiceStats = monoLine();
        voiceStats.setTextSize(12f);
        p.addView(callPeer);
        p.addView(callState);
        p.addView(callDuration);
        p.addView(callRoute);
        p.addView(callCodec);
        p.addView(voiceStats);

        TextView voiceNote = new TextView(this);
        voiceNote.setText("Voice (DEC-0050): P2P E2E primary; hub relay opaque."
                + " Call hub-loop = echo self-test. Needs MESH ESTABLISHED.");
        p.addView(voiceNote);

        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        row1.addView(btn("Answer", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean ok = AtnVoice.answer();
                AtnVoice.clearRing();
                appendLog(ok ? "voice answer" : "voice answer failed state="
                        + AtnVoice.stateName());
            }
        }), tabLp());
        row1.addView(btn("Reject", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean ok = AtnVoice.reject();
                appendLog(ok ? "voice reject" : "reject ignored");
            }
        }), tabLp());
        p.addView(row1);

        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        row2.addView(btn("Mute", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean next = !AtnVoice.isMute();
                AtnVoice.setMute(next);
                appendLog(next ? "muted (stop frames)" : "unmuted");
            }
        }), tabLp());
        row2.addView(btn("Speaker", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                toggleSpeaker();
            }
        }), tabLp());
        row2.addView(btn("Hangup", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean ok = AtnVoice.hangup();
                appendLog(ok ? "voice hangup" : "hangup ignored");
            }
        }), tabLp());
        p.addView(row2);

        p.addView(btn("Call hub-loop", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ensureMicThenCall(true);
            }
        }));
        p.addView(btn("Call first contact", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                java.util.List<AtnContacts.Entry> list =
                        AtnContacts.load(AtnLabActivity.this);
                if (list.isEmpty()) {
                    appendLog("no contacts — tap Ensure demo contact");
                    return;
                }
                appendLog("call contact " + list.get(0).label
                        + " (P2P when reachable; else hub sealed via native)");
                ensureMicThenCall(false);
            }
        }));
        p.addView(btn("Ensure demo contact 'hub'", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                AtnContacts.addDemoHub(AtnLabActivity.this);
                refreshContacts(contactsBox);
                appendLog("contacts updated");
            }
        }));

        contactsBox = new TextView(this);
        contactsBox.setTypeface(Typeface.MONOSPACE);
        contactsBox.setTextSize(12f);
        p.addView(contactsBox);
        refreshContacts(contactsBox);
        return p;
    }

    private TextView monoLine() {
        TextView t = new TextView(this);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextSize(13f);
        return t;
    }

    private Button btn(String label, View.OnClickListener click) {
        Button b = new Button(this);
        b.setText(label);
        b.setOnClickListener(click);
        return b;
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

    private void ensureMicThenCall(boolean hubLoop) {
        if (!hubLoop) {
            /* Lab contact call still uses hub-loop path until dial lands. */
            ;
        }
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
            String why = "Call blocked: mesh not ESTABLISHED (state="
                    + stateName(st) + "). Open Mesh tab → Start/reconnect"
                    + " after hub is listening.";
            appendLog(why);
            if (callMeshBanner != null) {
                callMeshBanner.setTextColor(Color.rgb(180, 40, 40));
                callMeshBanner.setText(why);
            }
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

    private void toggleSpeaker() {
        try {
            AudioManager am = (AudioManager) getSystemService(AUDIO_SERVICE);
            if (am == null) {
                appendLog("speaker: no AudioManager");
                return;
            }
            boolean next = !am.isSpeakerphoneOn();
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            am.setSpeakerphoneOn(next);
            AtnVoice.setSpeaker(next);
            appendLog(next ? "speaker ON" : "speaker OFF (earpiece)");
        } catch (Throwable t) {
            appendLog("speaker error: " + t.getMessage());
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
            if (rc != 0) {
                return;
            }
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
            boomBanner.setVisibility(View.VISIBLE);
            boomBanner.setText("BOOM phone is dead now\n" + AtnLabBoom.reason());
            status.setText("LAB DEAD (diag/log_only - keys kept)\n"
                    + "tap Start/reconnect to reset soak");
            if (callMeshBanner != null) {
                callMeshBanner.setTextColor(Color.RED);
                callMeshBanner.setText("mesh: DEAD — " + AtnLabBoom.reason());
            }
            paintCallPanel();
            return;
        }
        boomBanner.setVisibility(View.GONE);
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
        int st = AtnNative.TUN_CLOSED;
        try {
            st = AtnNative.tunState();
            int port = AtnNative.tunPort();
            if (st != lastLoggedTun) {
                appendLog("mesh state → " + stateName(st));
                lastLoggedTun = st;
            }
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
                boolean net = networkUp();
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
        if (callMeshBanner != null) {
            if (st == AtnNative.TUN_ESTABLISHED) {
                callMeshBanner.setTextColor(Color.rgb(20, 120, 40));
                callMeshBanner.setText("mesh: ESTABLISHED — calls allowed");
            } else {
                callMeshBanner.setTextColor(Color.rgb(180, 40, 40));
                callMeshBanner.setText("mesh: " + stateName(st)
                        + " — call blocked until ESTABLISHED"
                        + " (Mesh tab → Start/reconnect)");
            }
        }
        paintCallPanel();
    }

    private void paintCallPanel() {
        if (callPeer == null) {
            return;
        }
        callPeer.setText("peer: " + AtnVoice.peerLabel());
        callState.setText("call: " + AtnVoice.stateName());
        callDuration.setText("duration: " + AtnVoice.durationText());
        callRoute.setText("route: " + AtnVoice.routeLabel());
        callCodec.setText("codec: " + AtnVoice.codecLabel());
        String vs = AtnVoice.statsText();
        voiceStats.setText("stats: " + vs);
        if (!vs.equals(lastLoggedVoice)
                && AtnVoice.state() != AtnVoice.IDLE) {
            /* Throttle: only mirror when non-idle stats string changes. */
            lastLoggedVoice = vs;
        }
        if (ringBanner != null) {
            if (AtnVoice.isRinging()) {
                ringBanner.setVisibility(View.VISIBLE);
                ringBanner.setText("RING: " + AtnVoice.ringLabel()
                        + " — Answer / Reject");
            } else {
                ringBanner.setVisibility(View.GONE);
            }
        }
    }

    private boolean networkUp() {
        try {
            android.net.ConnectivityManager cm =
                    (android.net.ConnectivityManager)
                            getSystemService(CONNECTIVITY_SERVICE);
            if (cm == null) {
                return false;
            }
            android.net.Network n = cm.getActiveNetwork();
            if (n == null) {
                return false;
            }
            android.net.NetworkCapabilities caps = cm.getNetworkCapabilities(n);
            return caps != null && caps.hasCapability(
                    android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET);
        } catch (Throwable ignored) {
            return false;
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
