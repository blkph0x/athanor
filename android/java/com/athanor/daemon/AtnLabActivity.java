package com.athanor.daemon;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Lab launcher (DEC-0038). Starts the mesh daemon for stub connectivity
 * soak. Not a production UI. Knox policy is skipped while ATN_STUB.
 */
public class AtnLabActivity extends Activity {
    private static final String TAG = "atn-lab";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        boolean stub = AtnKnoxBuild.isStub();
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setTextSize(20f);
        title.setText(stub ? "Athanor LAB (stub Knox)" : "Athanor LAB");
        root.addView(title);

        TextView note = new TextView(this);
        note.setText("Connectivity soak only. USB charge-only / Knox "
                + "password policy are NOT applied on stub builds "
                + "(DEC-0038). Place atn-node.conf in app filesDir "
                + "(diag=1). Hub: atnnode listen.");
        root.addView(note);

        Button start = new Button(this);
        start.setText("Start mesh daemon");
        start.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View v) {
                startDaemon(note, stub);
            }
        });
        root.addView(start);

        setContentView(root);
        Log.i(TAG, "platform lab activity knoxStub=" + stub);
        if (getIntent() != null && getIntent().getBooleanExtra("autostart", false)) {
            startDaemon(note, stub);
        }
    }

    private void startDaemon(TextView note, boolean stub) {
        Intent svc = new Intent(AtnLabActivity.this, AtnDaemonService.class);
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }
        Log.i(TAG, "startForegroundService requested knoxStub=" + stub);
        note.setText("Daemon start requested. Watch logcat tag atn-daemon.");
    }
}