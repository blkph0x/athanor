package com.athanor.daemon;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Lab launcher (DEC-0038). Live tunnel status so Start is not a black hole.
 * Not a production UI.
 */
public class AtnLabActivity extends Activity {
    private static final String TAG = "atn-lab";
    private static final long UI_MS = 500L;

    private TextView status;
    private TextView logBox;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final StringBuilder lines = new StringBuilder();
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            paintStatus();
            ui.postDelayed(this, UI_MS);
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

        status = new TextView(this);
        status.setTextSize(16f);
        status.setTypeface(Typeface.MONOSPACE);
        status.setText("status: starting…");
        root.addView(status);

        TextView note = new TextView(this);
        note.setText("USB/Knox policy skipped on stub. Mesh uses Wi-Fi to hub.\n"
                + "diag=1 / log_only keeps keys during soak.");
        root.addView(note);

        Button start = new Button(this);
        start.setText("Start / reconnect mesh");
        start.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                appendLog("Start/reconnect pressed");
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
        if (getIntent() != null && getIntent().getBooleanExtra("autostart", false)) {
            startDaemon(true);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        ui.removeCallbacks(refresh);
        ui.post(refresh);
    }

    @Override
    protected void onPause() {
        ui.removeCallbacks(refresh);
        super.onPause();
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

    private void paintStatus() {
        String line;
        try {
            int st = AtnNative.tunState();
            int port = AtnNative.tunPort();
            line = "state=" + stateName(st)
                    + "  localUDP=" + port
                    + "\nknoxStub=" + AtnKnoxBuild.isStub()
                    + "  platform=" + AtnNative.platformId();
            if (st == AtnNative.TUN_ESTABLISHED) {
                line += "\nMESH UP - hub handshake OK";
            } else if (st == AtnNative.TUN_HANDSHAKE) {
                line += "\nwaiting for hub ACK...";
            } else {
                line += "\ntap Start/reconnect after hub is listening";
            }
        } catch (Throwable t) {
            line = "native not ready: " + t.getMessage();
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
