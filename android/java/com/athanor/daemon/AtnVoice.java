package com.athanor.daemon;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.Arrays;

/**
 * Lab secure voice (DEC-0050). Family 'A'. P2P E2E primary; hub path must
 * use nested seal (native). Lab hub-loop still echoes for self-test.
 * Mute = stop sending frames. No recordings stored.
 */
public final class AtnVoice {
    private static final String TAG = "atn-voice";

    public static final byte WIRE = (byte) 'A';
    public static final byte CTRL = (byte) 'C';
    public static final byte AUDIO = (byte) 'F';

    public static final int OP_OFFER = 1;
    public static final int OP_ACCEPT = 2;
    public static final int OP_REJECT = 3;
    public static final int OP_HANGUP = 4;
    public static final int OP_BUSY = 5;
    public static final int OP_KEEPALIVE = 6;
    public static final int OP_CODEC = 7;

    public static final int CODEC_PCM16 = 0;

    public static final int IDLE = 0;
    public static final int OUTGOING = 1;
    public static final int RINGING = 2;
    public static final int CONNECTING = 3;
    public static final int ACTIVE = 4;
    public static final int HOLD = 5;
    public static final int TERMINATING = 6;

    public static final int RATE_HZ = 16000;
    public static final int FRAME_SAMPLES = 320;
    public static final int FRAME_MS = 20;
    public static final int AUDIO_HDR = 15;
    public static final int CTRL_LEN = 12;
    public static final int JB_SLOTS = 8;
    public static final int JB_TARGET = 3; /* ~60ms */

    private static final Object LOCK = new Object();
    private static int state = IDLE;
    private static int callId;
    private static int codec = CODEC_PCM16;
    private static boolean mute;
    private static int sendSeq;
    private static int sampleTs;
    private static int peerMaxSeq;
    private static int framesSent;
    private static int framesRecv;
    private static int framesDropped;
    private static int framesLost;
    private static int jitterMs;

    private static final JbSlot[] jb = new JbSlot[JB_SLOTS];
    private static int playSeq;
    private static boolean playArmed;
    private static short[] lastPcm;

    private static AudioRecord recorder;
    private static AudioTrack track;
    private static Thread captureThread;
    private static Thread playThread;
    private static volatile boolean mediaRun;
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static volatile boolean incomingRing;
    private static String ringLabel = "";
    private static String peerLabel = "";
    private static String routeLabel = "—";
    private static boolean speakerOn;
    private static long activeSinceMs;

    static {
        for (int i = 0; i < JB_SLOTS; i++) {
            jb[i] = new JbSlot();
        }
        lastPcm = new short[FRAME_SAMPLES];
    }

    private static void setStateLocked(int next, String why) {
        if (state == next) {
            return;
        }
        Log.i(TAG, "state " + nameOf(state) + " → " + nameOf(next)
                + " (" + why + ")");
        state = next;
        if (next == ACTIVE || next == CONNECTING || next == OUTGOING) {
            if (activeSinceMs == 0L) {
                activeSinceMs = System.currentTimeMillis();
            }
        }
        if (next == IDLE || next == TERMINATING) {
            activeSinceMs = 0L;
        }
    }

    private static String nameOf(int st) {
        switch (st) {
            case OUTGOING: return "OUTGOING";
            case RINGING: return "RINGING";
            case CONNECTING: return "CONNECTING";
            case ACTIVE: return "ACTIVE";
            case HOLD: return "HOLD";
            case TERMINATING: return "TERMINATING";
            default: return "IDLE";
        }
    }

    public static boolean isRinging() {
        synchronized (LOCK) {
            return state == RINGING || incomingRing;
        }
    }

    public static String ringLabel() {
        synchronized (LOCK) {
            return ringLabel;
        }
    }

    public static void clearRing() {
        synchronized (LOCK) {
            incomingRing = false;
            ringLabel = "";
        }
    }

    private AtnVoice() {}

    public static int state() {
        synchronized (LOCK) {
            return state;
        }
    }

    public static String stateName() {
        return nameOf(state());
    }

    public static String peerLabel() {
        synchronized (LOCK) {
            return peerLabel.length() == 0 ? "(none)" : peerLabel;
        }
    }

    public static String routeLabel() {
        synchronized (LOCK) {
            return routeLabel;
        }
    }

    public static String codecLabel() {
        synchronized (LOCK) {
            return codec == CODEC_PCM16 ? "PCM16@16k" : ("codec=" + codec);
        }
    }

    public static String durationText() {
        synchronized (LOCK) {
            if (activeSinceMs == 0L
                    || state == IDLE || state == TERMINATING) {
                return "0:00";
            }
            long sec = (System.currentTimeMillis() - activeSinceMs) / 1000L;
            if (sec < 0L) {
                sec = 0L;
            }
            return (sec / 60L) + ":" + String.format("%02d", (int) (sec % 60L));
        }
    }

    public static String statsText() {
        synchronized (LOCK) {
            return "state=" + nameOf(state)
                    + " loss=" + framesLost
                    + " drop=" + framesDropped
                    + " jitterMs=" + jitterMs
                    + " sent=" + framesSent
                    + " recv=" + framesRecv
                    + " mute=" + (mute ? 1 : 0)
                    + " spk=" + (speakerOn ? 1 : 0);
        }
    }

    public static boolean isMute() {
        synchronized (LOCK) {
            return mute;
        }
    }

    public static void setMute(boolean m) {
        synchronized (LOCK) {
            mute = m;
            Log.i(TAG, m ? "mute ON (stop frames)" : "mute OFF");
        }
    }

    public static void setSpeaker(boolean on) {
        synchronized (LOCK) {
            speakerOn = on;
            Log.i(TAG, on ? "speaker ON" : "speaker OFF");
        }
    }

    public static boolean callHubLoop(int id) {
        synchronized (LOCK) {
            if (state != IDLE) {
                Log.w(TAG, "callHubLoop busy state=" + nameOf(state));
                return false;
            }
            if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
                Log.w(TAG, "callHubLoop blocked: tun not ESTABLISHED");
                return false;
            }
            callId = id;
            codec = CODEC_PCM16;
            peerLabel = "hub-loop";
            routeLabel = "LOOP";
            sendSeq = 0;
            sampleTs = 0;
            peerMaxSeq = 0;
            framesSent = framesRecv = framesDropped = framesLost = 0;
            resetJbLocked();
            setStateLocked(OUTGOING, "callHubLoop");
            byte[] offer = encodeCtrl(OP_OFFER, codec, callId);
            int rc = AtnNative.tunSend(offer);
            Log.i(TAG, "send ctrl OFFER id=" + callId + " tunSend rc=" + rc);
            if (rc != 0) {
                enterIdleLocked();
                return false;
            }
            rc = AtnNative.tunSend(encodeCtrl(OP_CODEC, codec, callId));
            Log.i(TAG, "send ctrl CODEC id=" + callId + " tunSend rc=" + rc);
            /* Lab hub-loop: become ACTIVE immediately; hub echoes AUDIO back. */
            setStateLocked(ACTIVE, "hub-loop self-accept");
        }
        startMedia();
        return true;
    }

    public static boolean answer() {
        synchronized (LOCK) {
            if (state != RINGING) {
                Log.w(TAG, "answer ignored state=" + nameOf(state));
                return false;
            }
            if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
                Log.w(TAG, "answer blocked: tun not ESTABLISHED");
                return false;
            }
            setStateLocked(CONNECTING, "answer");
            int rc = AtnNative.tunSend(encodeCtrl(OP_ACCEPT, codec, callId));
            Log.i(TAG, "send ctrl ACCEPT id=" + callId + " tunSend rc=" + rc);
            if (rc != 0) {
                return false;
            }
            if (routeLabel.equals("—")) {
                routeLabel = "VIA HUB";
            }
            setStateLocked(ACTIVE, "accepted");
        }
        startMedia();
        return true;
    }

    public static boolean reject() {
        synchronized (LOCK) {
            if (state != RINGING) {
                Log.w(TAG, "reject ignored state=" + nameOf(state));
                return false;
            }
            if (AtnNative.tunState() == AtnNative.TUN_ESTABLISHED) {
                int rc = AtnNative.tunSend(encodeCtrl(OP_REJECT, codec, callId));
                Log.i(TAG, "send ctrl REJECT id=" + callId + " tunSend rc=" + rc);
            }
            setStateLocked(TERMINATING, "reject");
            enterIdleLocked();
        }
        stopMedia();
        return true;
    }

    public static boolean hangup() {
        synchronized (LOCK) {
            if (state == IDLE || state == TERMINATING) {
                Log.w(TAG, "hangup ignored state=" + nameOf(state));
                return false;
            }
            setStateLocked(TERMINATING, "hangup");
            if (AtnNative.tunState() == AtnNative.TUN_ESTABLISHED) {
                int rc = AtnNative.tunSend(encodeCtrl(OP_HANGUP, codec, callId));
                Log.i(TAG, "send ctrl HANGUP id=" + callId + " tunSend rc=" + rc);
            }
            enterIdleLocked();
        }
        stopMedia();
        Log.i(TAG, "hangup complete → IDLE");
        return true;
    }

    /** Daemon drain: handle decrypted 'A' frames. Returns true if consumed. */
    public static boolean onFrame(byte[] msg, int n) {
        if (msg == null || n < 2 || msg[0] != WIRE) {
            return false;
        }
        if (n > 1024) {
            synchronized (LOCK) {
                framesDropped++;
            }
            return true;
        }
        if (msg[1] == CTRL) {
            return onCtrl(msg, n);
        }
        if (msg[1] == AUDIO) {
            return onAudio(msg, n);
        }
        synchronized (LOCK) {
            framesDropped++;
        }
        return true;
    }

    private static boolean onCtrl(byte[] msg, int n) {
        if (n < CTRL_LEN) {
            synchronized (LOCK) {
                framesDropped++;
            }
            return true;
        }
        int op = msg[2] & 0xff;
        int c = msg[3] & 0xff;
        int id = getBe32(msg, 4);
        synchronized (LOCK) {
            Log.i(TAG, "recv ctrl op=" + op + " id=" + id
                    + " state=" + nameOf(state));
            switch (op) {
                case OP_OFFER:
                    if (state == IDLE) {
                        callId = id;
                        codec = (c == CODEC_PCM16) ? c : CODEC_PCM16;
                        peerLabel = "incoming";
                        routeLabel = "VIA HUB";
                        setStateLocked(RINGING, "recv OFFER");
                        incomingRing = true;
                        ringLabel = "call " + id;
                        resetJbLocked();
                    } else if (state == OUTGOING && id == callId) {
                        /* Hub echo of our own offer (lab loopback) — ignore. */
                        ;
                    } else {
                        int rc = AtnNative.tunSend(
                                encodeCtrl(OP_BUSY, codec, callId));
                        Log.i(TAG, "send ctrl BUSY tunSend rc=" + rc);
                    }
                    break;
                case OP_ACCEPT:
                    if (state == OUTGOING && id == callId) {
                        setStateLocked(ACTIVE, "recv ACCEPT");
                    } else if (state == ACTIVE && id == callId) {
                        /* echo of accept — ignore */
                        ;
                    }
                    break;
                case OP_REJECT:
                case OP_BUSY:
                case OP_HANGUP:
                    if (state != IDLE && (id == 0 || id == callId)) {
                        setStateLocked(TERMINATING, "recv op=" + op);
                        enterIdleLocked();
                        main.post(new Runnable() {
                            @Override
                            public void run() {
                                stopMedia();
                            }
                        });
                    }
                    break;
                case OP_KEEPALIVE:
                case OP_CODEC:
                    break;
                default:
                    framesDropped++;
                    break;
            }
        }
        return true;
    }

    private static boolean onAudio(byte[] msg, int n) {
        if (n < AUDIO_HDR) {
            synchronized (LOCK) {
                framesDropped++;
            }
            return true;
        }
        int id = getBe32(msg, 2);
        int seq = getBe32(msg, 6);
        int ts = getBe32(msg, 10);
        int c = msg[14] & 0xff;
        if (c != CODEC_PCM16 || ((n - AUDIO_HDR) & 1) != 0) {
            synchronized (LOCK) {
                framesDropped++;
            }
            return true;
        }
        int plen = n - AUDIO_HDR;
        short[] pcm = new short[plen / 2];
        for (int i = 0; i < pcm.length; i++) {
            int lo = msg[AUDIO_HDR + 2 * i] & 0xff;
            int hi = msg[AUDIO_HDR + 2 * i + 1] & 0xff;
            pcm[i] = (short) (lo | (hi << 8));
        }
        synchronized (LOCK) {
            if (state != ACTIVE && state != HOLD && state != CONNECTING
                    && state != OUTGOING) {
                framesDropped++;
                return true;
            }
            /* Hub-loop: phone may hear its own offer path before ACTIVE. */
            if (id != callId) {
                framesDropped++;
                return true;
            }
            if (playArmed && seq < playSeq) {
                framesDropped++;
                return true;
            }
            if (jbFindLocked(seq) != null) {
                framesDropped++;
                return true;
            }
            while (jbCountLocked() >= JB_SLOTS) {
                jbDropOldestLocked();
            }
            JbSlot s = jbFreeLocked();
            if (s == null) {
                framesDropped++;
                return true;
            }
            s.used = true;
            s.seq = seq;
            s.ts = ts;
            s.pcm = pcm;
            if (seq > peerMaxSeq) {
                peerMaxSeq = seq;
            }
            framesRecv++;
            if (!playArmed && jbCountLocked() >= JB_TARGET) {
                int min = Integer.MAX_VALUE;
                for (int i = 0; i < JB_SLOTS; i++) {
                    if (jb[i].used && jb[i].seq < min) {
                        min = jb[i].seq;
                    }
                }
                playSeq = min;
                playArmed = true;
            }
            jitterMs = jbCountLocked() * FRAME_MS;
        }
        return true;
    }

    private static short[] popPcmLocked() {
        if (!playArmed) {
            return null;
        }
        JbSlot s = jbFindLocked(playSeq);
        if (s == null) {
            short[] plc = new short[FRAME_SAMPLES];
            for (int i = 0; i < FRAME_SAMPLES; i++) {
                plc[i] = (short) ((lastPcm[i] * 7) / 8);
            }
            framesLost++;
            playSeq++;
            return plc;
        }
        short[] out = s.pcm;
        System.arraycopy(out, 0, lastPcm, 0,
                Math.min(out.length, FRAME_SAMPLES));
        s.used = false;
        s.pcm = null;
        playSeq++;
        jitterMs = jbCountLocked() * FRAME_MS;
        return out;
    }

    private static void startMedia() {
        stopMedia();
        mediaRun = true;
        Log.i(TAG, "media start");
        int minRec = AudioRecord.getMinBufferSize(RATE_HZ,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        int minPlay = AudioTrack.getMinBufferSize(RATE_HZ,
                AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        try {
            recorder = new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                    RATE_HZ, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(minRec, FRAME_SAMPLES * 4));
            track = new AudioTrack(AudioManager.STREAM_VOICE_CALL, RATE_HZ,
                    AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(minPlay, FRAME_SAMPLES * 4),
                    AudioTrack.MODE_STREAM);
            recorder.startRecording();
            track.play();
            Log.i(TAG, "media started rec+track");
        } catch (Exception e) {
            Log.w(TAG, "media start failed: " + e.getMessage());
            mediaRun = false;
            return;
        }
        captureThread = new Thread(new Runnable() {
            @Override
            public void run() {
                short[] buf = new short[FRAME_SAMPLES];
                while (mediaRun) {
                    int n = 0;
                    try {
                        n = recorder.read(buf, 0, FRAME_SAMPLES);
                    } catch (Exception e) {
                        break;
                    }
                    if (n < FRAME_SAMPLES) {
                        continue;
                    }
                    sendPcmFrame(buf);
                }
                Arrays.fill(buf, (short) 0);
            }
        }, "atn-voice-cap");
        playThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (mediaRun) {
                    short[] pcm;
                    synchronized (LOCK) {
                        pcm = popPcmLocked();
                    }
                    if (pcm == null) {
                        try {
                            Thread.sleep(FRAME_MS);
                        } catch (InterruptedException e) {
                            break;
                        }
                        continue;
                    }
                    try {
                        track.write(pcm, 0, pcm.length);
                    } catch (Exception e) {
                        break;
                    }
                }
            }
        }, "atn-voice-play");
        captureThread.start();
        playThread.start();
    }

    private static void stopMedia() {
        if (mediaRun) {
            Log.i(TAG, "media stop");
        }
        mediaRun = false;
        if (captureThread != null) {
            try {
                captureThread.join(500);
            } catch (InterruptedException ignored) {
            }
            captureThread = null;
        }
        if (playThread != null) {
            try {
                playThread.join(500);
            } catch (InterruptedException ignored) {
            }
            playThread = null;
        }
        if (recorder != null) {
            try {
                recorder.stop();
            } catch (Exception ignored) {
            }
            try {
                recorder.release();
            } catch (Exception ignored) {
            }
            recorder = null;
        }
        if (track != null) {
            try {
                track.stop();
            } catch (Exception ignored) {
            }
            try {
                track.release();
            } catch (Exception ignored) {
            }
            track = null;
        }
    }

    private static void sendPcmFrame(short[] pcm) {
        synchronized (LOCK) {
            if (state != ACTIVE && state != OUTGOING && state != CONNECTING) {
                return;
            }
            if (mute || state == HOLD) {
                return;
            }
            if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
                return;
            }
            byte[] wire = encodeAudio(callId, sendSeq, sampleTs, codec, pcm);
            if (wire == null || wire.length > 1024) {
                framesDropped++;
                return;
            }
            int rc = AtnNative.tunSend(wire);
            if (rc == 0) {
                sendSeq++;
                sampleTs += FRAME_SAMPLES;
                framesSent++;
            } else if ((framesSent & 0x3f) == 0) {
                Log.w(TAG, "audio tunSend rc=" + rc + " seq=" + sendSeq);
            }
            Arrays.fill(wire, (byte) 0);
        }
    }

    public static byte[] encodeCtrl(int op, int codec, int callId) {
        byte[] out = new byte[CTRL_LEN];
        out[0] = WIRE;
        out[1] = CTRL;
        out[2] = (byte) op;
        out[3] = (byte) codec;
        putBe32(out, 4, callId);
        return out;
    }

    public static byte[] encodeAudio(int callId, int seq, int sampleTs,
                                     int codec, short[] pcm) {
        if (pcm == null) {
            return null;
        }
        byte[] out = new byte[AUDIO_HDR + pcm.length * 2];
        out[0] = WIRE;
        out[1] = AUDIO;
        putBe32(out, 2, callId);
        putBe32(out, 6, seq);
        putBe32(out, 10, sampleTs);
        out[14] = (byte) codec;
        for (int i = 0; i < pcm.length; i++) {
            out[AUDIO_HDR + 2 * i] = (byte) (pcm[i] & 0xff);
            out[AUDIO_HDR + 2 * i + 1] = (byte) ((pcm[i] >> 8) & 0xff);
        }
        return out;
    }

    private static void putBe32(byte[] p, int off, int v) {
        p[off] = (byte) ((v >>> 24) & 0xff);
        p[off + 1] = (byte) ((v >>> 16) & 0xff);
        p[off + 2] = (byte) ((v >>> 8) & 0xff);
        p[off + 3] = (byte) (v & 0xff);
    }

    private static int getBe32(byte[] p, int off) {
        return ((p[off] & 0xff) << 24) | ((p[off + 1] & 0xff) << 16)
                | ((p[off + 2] & 0xff) << 8) | (p[off + 3] & 0xff);
    }

    private static void enterIdleLocked() {
        resetJbLocked();
        Arrays.fill(lastPcm, (short) 0);
        if (state != IDLE) {
            Log.i(TAG, "enter IDLE from " + nameOf(state));
        }
        state = IDLE;
        callId = 0;
        sendSeq = 0;
        sampleTs = 0;
        mute = false;
        incomingRing = false;
        ringLabel = "";
        peerLabel = "";
        routeLabel = "—";
        activeSinceMs = 0L;
    }

    private static void resetJbLocked() {
        for (int i = 0; i < JB_SLOTS; i++) {
            jb[i].used = false;
            jb[i].pcm = null;
        }
        playArmed = false;
        playSeq = 0;
        jitterMs = 0;
    }

    private static int jbCountLocked() {
        int n = 0;
        for (int i = 0; i < JB_SLOTS; i++) {
            if (jb[i].used) {
                n++;
            }
        }
        return n;
    }

    private static JbSlot jbFindLocked(int seq) {
        for (int i = 0; i < JB_SLOTS; i++) {
            if (jb[i].used && jb[i].seq == seq) {
                return jb[i];
            }
        }
        return null;
    }

    private static JbSlot jbFreeLocked() {
        for (int i = 0; i < JB_SLOTS; i++) {
            if (!jb[i].used) {
                return jb[i];
            }
        }
        return null;
    }

    private static void jbDropOldestLocked() {
        int best = -1;
        int bestSeq = Integer.MAX_VALUE;
        for (int i = 0; i < JB_SLOTS; i++) {
            if (jb[i].used && jb[i].seq < bestSeq) {
                bestSeq = jb[i].seq;
                best = i;
            }
        }
        if (best >= 0) {
            jb[best].used = false;
            jb[best].pcm = null;
        }
    }

    private static final class JbSlot {
        boolean used;
        int seq;
        int ts;
        short[] pcm;
    }
}
