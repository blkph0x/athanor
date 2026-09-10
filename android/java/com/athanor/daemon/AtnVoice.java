package com.athanor.daemon;

import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.AutomaticGainControl;
import android.media.audiofx.NoiseSuppressor;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.Arrays;

/**
 * Lab secure voice (DEC-0050/0053). Family 'A'. P2P E2E primary; hub path
 * must use nested seal (native). Lab hub-loop still echoes for self-test.
 * Soft JB/cadence from PROBE RTT + loss. Hub drop → HOLD + reconnect
 * (dmon hub bounce / single-up fallback) without ending the call; warn UI.
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
    public static final int OP_PROBE = 10;
    public static final int OP_PROBE_ACK = 11;

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
    public static final int JB_SLOTS = 48; /* capacity for WAN bounce */
    public static final int JB_TARGET_MIN = 2;  /* 40ms */
    public static final int JB_TARGET_MAX = 24; /* 480ms */
    public static final int JB_TARGET_DEFAULT = 6; /* 120ms start */
    private static final int PROBE_EVERY_FRAMES = 100; /* ~2s @ 20ms */

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
    private static int jbTarget = JB_TARGET_DEFAULT;
    private static int lastRttMs = -1;
    private static long probeSentMs;
    private static int probeSeq;
    private static boolean transportHold;
    private static int stateBeforeTransportHold;
    private static String qualityWarn = "";

    private static final JbSlot[] jb = new JbSlot[JB_SLOTS];
    private static int playSeq;
    private static boolean playArmed;
    private static short[] lastPcm;

    private static AudioRecord recorder;
    private static AudioTrack track;
    private static AcousticEchoCanceler aec;
    private static NoiseSuppressor ns;
    private static AutomaticGainControl agc;
    private static Thread captureThread;
    private static Thread playThread;
    private static volatile boolean mediaRun;
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static volatile boolean incomingRing;
    private static String ringLabel = "";
    private static Context appCtx;
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

    /** Application context for AudioManager speaker routing. */
    public static void setContext(Context ctx) {
        if (ctx != null) {
            appCtx = ctx.getApplicationContext();
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
                    + " jb=" + (jbTarget * FRAME_MS) + "ms"
                    + " rtt=" + (lastRttMs < 0 ? "?" : (lastRttMs + "ms"))
                    + " sent=" + framesSent
                    + " recv=" + framesRecv
                    + " mute=" + (mute ? 1 : 0)
                    + " spk=" + (speakerOn ? 1 : 0);
        }
    }

    /** Soft-quality / reroute banner (empty when clear). */
    public static String qualityWarn() {
        synchronized (LOCK) {
            return qualityWarn;
        }
    }

    public static int lastRttMs() {
        synchronized (LOCK) {
            return lastRttMs;
        }
    }

    public static int jbTargetMs() {
        synchronized (LOCK) {
            return jbTarget * FRAME_MS;
        }
    }

    /**
     * Hub / tunnel dropped mid-call. Do not hang up — HOLD + PLC while
     * dmon reconnects / hub-bounces / falls back to single-up.
     */
    public static void onTransportLost(String reason) {
        synchronized (LOCK) {
            if (state == IDLE || state == TERMINATING || state == RINGING) {
                return;
            }
            transportHold = true;
            qualityWarn = (reason != null && reason.length() > 0)
                    ? reason
                    : "Hub path lost — reconnecting (call held)";
            if (state == ACTIVE || state == OUTGOING || state == CONNECTING) {
                stateBeforeTransportHold = state;
                setStateLocked(HOLD, "transport lost");
            }
            Log.w(TAG, "transport lost: " + qualityWarn);
        }
    }

    /**
     * Tunnel back ESTABLISHED after bounce/failover. Resume media; soft
     * bump JB for new path latency; warn user that route changed.
     */
    public static void onTransportRestored(String reason) {
        synchronized (LOCK) {
            if (state == IDLE || state == TERMINATING) {
                return;
            }
            /* Ignore fresh ESTABLISHED when we never held (e.g. RINGING). */
            if (!transportHold && state != HOLD) {
                return;
            }
            int resume = stateBeforeTransportHold;
            if (resume != ACTIVE && resume != OUTGOING
                    && resume != CONNECTING) {
                resume = ACTIVE;
            }
            transportHold = false;
            stateBeforeTransportHold = IDLE;
            if (state == HOLD) {
                setStateLocked(resume, "transport restored");
            }
            /* Soft bump playout for unknown new-path RTT; PROBE retunes. */
            setJbTargetLocked(Math.min(JB_TARGET_MAX,
                    Math.max(jbTarget + 2, JB_TARGET_DEFAULT + 2)));
            if ("LOOP".equals(routeLabel)) {
                routeLabel = "LOOP (restored)";
            } else if (routeLabel.indexOf("bounce") < 0
                    && routeLabel.indexOf("restored") < 0) {
                routeLabel = routeLabel + " · bounce";
            }
            qualityWarn = (reason != null && reason.length() > 0)
                    ? reason
                    : "Mesh restored — call continuing (cadence retuned)";
            probeSentMs = 0L;
            Log.i(TAG, "transport restored: " + qualityWarn
                    + " jb=" + (jbTarget * FRAME_MS) + "ms"
                    + " resume=" + nameOf(resume));
        }
        maybeSendProbe();
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
        applySpeakerphone();
    }

    private static void applySpeakerphone() {
        Context ctx = appCtx;
        if (ctx == null) {
            return;
        }
        try {
            AudioManager am =
                    (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) {
                return;
            }
            boolean on;
            synchronized (LOCK) {
                on = speakerOn;
            }
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            am.setSpeakerphoneOn(on);
            Log.i(TAG, "AudioManager speakerphone=" + on);
        } catch (Exception e) {
            Log.w(TAG, "applySpeakerphone: " + e.getMessage());
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
            lastRttMs = -1;
            probeSentMs = 0L;
            transportHold = false;
            stateBeforeTransportHold = IDLE;
            qualityWarn = "";
            jbTarget = JB_TARGET_DEFAULT;
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
            /* Earpiece (normal call speaker). SPEAKER button → loudspeaker. */
            speakerOn = false;
        }
        startMedia();
        applySpeakerphone();
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
                    } else if (id == callId && (state == OUTGOING
                            || state == ACTIVE || state == CONNECTING
                            || state == HOLD)) {
                        /* Hub-loop / relay echo of our own OFFER — ignore. */
                        Log.i(TAG, "ignore OFFER echo id=" + id
                                + " state=" + nameOf(state));
                    } else {
                        int rc = AtnNative.tunSend(
                                encodeCtrl(OP_BUSY, codec, callId));
                        Log.i(TAG, "send ctrl BUSY tunSend rc=" + rc);
                    }
                    break;
                case OP_ACCEPT:
                    if (state == OUTGOING && id == callId) {
                        setStateLocked(ACTIVE, "recv ACCEPT");
                    } else if (id == callId && (state == ACTIVE
                            || state == CONNECTING || state == OUTGOING)) {
                        /* echo of accept — ignore */
                        Log.i(TAG, "ignore ACCEPT echo id=" + id);
                    }
                    break;
                case OP_REJECT:
                case OP_BUSY:
                case OP_HANGUP:
                    if (id == callId && "LOOP".equals(routeLabel)
                            && (op == OP_BUSY || op == OP_REJECT)) {
                        /* Hub echoed our mistaken BUSY — do not tear down. */
                        Log.i(TAG, "ignore " + op + " echo on LOOP");
                        break;
                    }
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
                case OP_PROBE:
                    /*
                     * Hub-loop echoes our PROBE (same op) — count as RTT.
                     * Real peers send PROBE; answer with PROBE_ACK only.
                     */
                    if (id == callId && probeSentMs != 0L
                            && routeLabel != null
                            && routeLabel.startsWith("LOOP")
                            && (state == ACTIVE || state == HOLD
                            || state == OUTGOING || state == CONNECTING)) {
                        applyRttLocked(System.currentTimeMillis() - probeSentMs);
                        probeSentMs = 0L;
                    } else if (id == callId && (state == ACTIVE
                            || state == HOLD || state == CONNECTING
                            || state == OUTGOING)
                            && !transportHold
                            && AtnNative.tunState()
                            == AtnNative.TUN_ESTABLISHED) {
                        int rc = AtnNative.tunSend(
                                encodeCtrl(OP_PROBE_ACK, codec, callId));
                        Log.i(TAG, "send ctrl PROBE_ACK rc=" + rc);
                    }
                    break;
                case OP_PROBE_ACK:
                    if (id == callId && probeSentMs != 0L) {
                        applyRttLocked(System.currentTimeMillis() - probeSentMs);
                        probeSentMs = 0L;
                    }
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
            if (!playArmed && jbCountLocked() >= jbTarget) {
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
            maybeAdaptFromLossLocked();
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
        boolean loop;
        synchronized (LOCK) {
            loop = "LOOP".equals(routeLabel);
        }
        Log.i(TAG, "media start loop=" + loop);
        int minRec = AudioRecord.getMinBufferSize(RATE_HZ,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        int minPlay = AudioTrack.getMinBufferSize(RATE_HZ,
                AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        try {
            /*
             * Cell-style path: VOICE_COMMUNICATION + STREAM_VOICE_CALL so the
             * platform can couple playback into AEC. Then attach
             * AcousticEchoCanceler on the record session to strip speaker
             * output from the mic (local howl), while WAN-delayed remote /
             * hub-loop echo still plays through. NS + AGC match typical
             * handset DSP.
             */
            int src = MediaRecorder.AudioSource.VOICE_COMMUNICATION;
            int stream = AudioManager.STREAM_VOICE_CALL;
            recorder = new AudioRecord(src, RATE_HZ, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(minRec, FRAME_SAMPLES * 4));
            track = new AudioTrack(stream, RATE_HZ,
                    AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(minPlay, FRAME_SAMPLES * 4),
                    AudioTrack.MODE_STREAM);
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                throw new IllegalStateException("AudioRecord not initialized");
            }
            if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                throw new IllegalStateException("AudioTrack not initialized");
            }
            attachVoiceFx(recorder.getAudioSessionId());
            recorder.startRecording();
            track.play();
            Log.i(TAG, "media started rec+track src=" + src + " stream=" + stream
                    + " aec=" + (aec != null ? 1 : 0)
                    + " ns=" + (ns != null ? 1 : 0)
                    + " agc=" + (agc != null ? 1 : 0));
        } catch (Exception e) {
            Log.w(TAG, "media start failed: " + e.getMessage());
            mediaRun = false;
            releaseVoiceFx();
            return;
        }
        captureThread = new Thread(new Runnable() {
            @Override
            public void run() {
                short[] buf = new short[FRAME_SAMPLES];
                int frames = 0;
                while (mediaRun) {
                    int n = 0;
                    try {
                        n = recorder.read(buf, 0, FRAME_SAMPLES);
                    } catch (Exception e) {
                        Log.w(TAG, "capture read fail: " + e.getMessage());
                        break;
                    }
                    if (n < FRAME_SAMPLES) {
                        continue;
                    }
                    sendPcmFrame(buf);
                    frames++;
                    if ((frames % PROBE_EVERY_FRAMES) == 0) {
                        maybeSendProbe();
                    }
                    if ((frames % 50) == 0) {
                        Log.i(TAG, "capture alive frames=" + frames
                                + " " + statsText());
                    }
                }
                Arrays.fill(buf, (short) 0);
                Log.i(TAG, "capture thread exit");
            }
        }, "atn-voice-cap");
        playThread = new Thread(new Runnable() {
            @Override
            public void run() {
                int frames = 0;
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
                        Log.w(TAG, "play write fail: " + e.getMessage());
                        break;
                    }
                    frames++;
                    if ((frames % 50) == 0) {
                        Log.i(TAG, "play alive frames=" + frames
                                + " " + statsText());
                    }
                }
                Log.i(TAG, "play thread exit");
            }
        }, "atn-voice-play");
        captureThread.start();
        playThread.start();
    }

    /**
     * Attach platform AEC / NS / AGC to the capture session (cellphone DSP).
     * Failures are non-fatal — call still runs without them.
     */
    private static void attachVoiceFx(int sessionId) {
        releaseVoiceFx();
        if (sessionId == 0) {
            Log.w(TAG, "voice fx: sessionId=0");
            return;
        }
        try {
            if (AcousticEchoCanceler.isAvailable()) {
                aec = AcousticEchoCanceler.create(sessionId);
                if (aec != null) {
                    aec.setEnabled(true);
                    Log.i(TAG, "AEC enabled session=" + sessionId);
                }
            } else {
                Log.w(TAG, "AEC not available on this device");
            }
        } catch (Exception e) {
            Log.w(TAG, "AEC attach: " + e.getMessage());
            aec = null;
        }
        try {
            if (NoiseSuppressor.isAvailable()) {
                ns = NoiseSuppressor.create(sessionId);
                if (ns != null) {
                    ns.setEnabled(true);
                    Log.i(TAG, "NS enabled");
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "NS attach: " + e.getMessage());
            ns = null;
        }
        try {
            if (AutomaticGainControl.isAvailable()) {
                agc = AutomaticGainControl.create(sessionId);
                if (agc != null) {
                    agc.setEnabled(true);
                    Log.i(TAG, "AGC enabled");
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "AGC attach: " + e.getMessage());
            agc = null;
        }
    }

    private static void releaseVoiceFx() {
        if (aec != null) {
            try {
                aec.setEnabled(false);
            } catch (Exception ignored) {
            }
            try {
                aec.release();
            } catch (Exception ignored) {
            }
            aec = null;
        }
        if (ns != null) {
            try {
                ns.setEnabled(false);
            } catch (Exception ignored) {
            }
            try {
                ns.release();
            } catch (Exception ignored) {
            }
            ns = null;
        }
        if (agc != null) {
            try {
                agc.setEnabled(false);
            } catch (Exception ignored) {
            }
            try {
                agc.release();
            } catch (Exception ignored) {
            }
            agc = null;
        }
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
        releaseVoiceFx();
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
        try {
            Context ctx = appCtx;
            if (ctx != null) {
                AudioManager am =
                        (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
                if (am != null) {
                    am.setSpeakerphoneOn(false);
                    am.setMode(AudioManager.MODE_NORMAL);
                }
            }
        } catch (Exception ignored) {
        }
    }

    private static void sendPcmFrame(short[] pcm) {
        synchronized (LOCK) {
            if (state != ACTIVE && state != OUTGOING && state != CONNECTING) {
                return;
            }
            if (mute || state == HOLD || transportHold) {
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

    /** Periodic latency probe; soft-retargets JB from RTT (DEC-0053). */
    public static void maybeSendProbe() {
        synchronized (LOCK) {
            if (state != ACTIVE && state != HOLD && state != CONNECTING
                    && state != OUTGOING) {
                return;
            }
            if (transportHold) {
                return;
            }
            if (AtnNative.tunState() != AtnNative.TUN_ESTABLISHED) {
                return;
            }
            if (probeSentMs != 0L
                    && (System.currentTimeMillis() - probeSentMs) < 1500L) {
                return; /* one outstanding */
            }
            int rc = AtnNative.tunSend(encodeCtrl(OP_PROBE, codec, callId));
            if (rc == 0) {
                probeSentMs = System.currentTimeMillis();
                probeSeq++;
                if ((probeSeq & 0x7) == 1) {
                    Log.i(TAG, "send ctrl PROBE id=" + callId
                            + " jb=" + (jbTarget * FRAME_MS) + "ms");
                }
            }
        }
    }

    private static void applyRttLocked(long rtt) {
        if (rtt < 0L) {
            rtt = 0L;
        }
        if (rtt > 5000L) {
            rtt = 5000L;
        }
        lastRttMs = (int) rtt;
        int want;
        if (rtt < 80L) {
            want = 4; /* 80ms */
        } else if (rtt < 150L) {
            want = 6; /* 120ms */
        } else if (rtt < 250L) {
            want = 8; /* 160ms */
        } else if (rtt < 400L) {
            want = 12; /* 240ms */
        } else if (rtt < 700L) {
            want = 16; /* 320ms */
        } else {
            want = 24; /* 480ms ceiling */
        }
        setJbTargetLocked(want);
        if (rtt >= 400L) {
            qualityWarn = "High latency " + lastRttMs
                    + "ms — playout " + (jbTarget * FRAME_MS) + "ms";
        } else if (qualityWarn.startsWith("High latency")
                || qualityWarn.startsWith("Mesh restored")
                || qualityWarn.startsWith("Hub path")
                || qualityWarn.startsWith("Hub dropped")
                || qualityWarn.startsWith("Reconnecting")) {
            /* Clear transient path warns once RTT looks healthy. */
            if (rtt < 250L && !transportHold) {
                qualityWarn = "";
            }
        }
        Log.i(TAG, "rtt=" + lastRttMs + "ms → jb target "
                + (jbTarget * FRAME_MS) + "ms");
    }

    private static void maybeAdaptFromLossLocked() {
        int total = framesRecv + framesLost;
        if (total < 40 || (total & 0x1f) != 0) {
            return;
        }
        int lossPct = (framesLost * 100) / total;
        if (lossPct >= 8 && jbTarget < JB_TARGET_MAX) {
            setJbTargetLocked(jbTarget + 2);
            qualityWarn = "Packet loss " + lossPct
                    + "% — widening playout to " + (jbTarget * FRAME_MS) + "ms";
            Log.w(TAG, qualityWarn);
        } else if (lossPct <= 1 && jbTarget > JB_TARGET_DEFAULT
                && lastRttMs >= 0 && lastRttMs < 200) {
            setJbTargetLocked(jbTarget - 1);
        }
    }

    private static void setJbTargetLocked(int slots) {
        if (slots < JB_TARGET_MIN) {
            slots = JB_TARGET_MIN;
        }
        if (slots > JB_TARGET_MAX) {
            slots = JB_TARGET_MAX;
        }
        if (slots > JB_SLOTS) {
            slots = JB_SLOTS;
        }
        jbTarget = slots;
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
        lastRttMs = -1;
        probeSentMs = 0L;
        transportHold = false;
        stateBeforeTransportHold = IDLE;
        qualityWarn = "";
        jbTarget = JB_TARGET_DEFAULT;
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
