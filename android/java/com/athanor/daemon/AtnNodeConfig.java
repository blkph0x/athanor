package com.athanor.daemon;

/**
 * Lab / daemon file atn-node.conf. DEC-0021 / 0027 / 0028 / 0029 / 0032.
 * Same keys as atn_cfg.c. ready() still requires peer_* only.
 */
public final class AtnNodeConfig {
    public static final int MAX_HUBS = 16; /* DEC-0032; match ATN_CFG_MAX_HUBS */
    public static final int FLUSH_ZEROIZE = 0;
    public static final int FLUSH_LOG_ONLY = 1;
    public static final int OUTAGE_NORMAL = 0;
    public static final int OUTAGE_MAINTENANCE = 1;
    public static final int OUTAGE_BLACKOUT = 2;
    public static final int OUTAGE_FARADAY = 3;
    public static final int OUTAGE_CAPTURE = 4;

    public int ipv4Host;
    public int port;
    public byte[] ek;
    public boolean haveIpv4;
    public boolean havePort;
    public boolean haveEk;
    public byte[] witness;
    public boolean haveWitness;
    public final Hub[] hubs = new Hub[MAX_HUBS];
    public int diag;
    public int flushMode = FLUSH_ZEROIZE;
    public int wipeArmed;
    public int outageClass = OUTAGE_NORMAL;

    public static final class Hub {
        public int ipv4Host;
        public int port;
        public byte[] ek;
        public boolean haveIpv4;
        public boolean havePort;
        public boolean haveEk;
    }

    private AtnNodeConfig() {
        for (int i = 0; i < MAX_HUBS; i++) {
            hubs[i] = new Hub();
        }
    }

    public boolean ready() {
        return haveIpv4 && havePort && haveEk && ek != null && ek.length == 1568;
    }

    public int hubCount() {
        if (!ready()) {
            return 0;
        }
        int n = 1;
        for (int h = 1; h < MAX_HUBS; h++) {
            if (hubs[h].haveIpv4 && hubs[h].havePort && hubs[h].haveEk) {
                n++;
            } else {
                break;
            }
        }
        return n;
    }

    public static AtnNodeConfig parse(String text) {
        AtnNodeConfig c = new AtnNodeConfig();
        if (text == null) {
            return null;
        }
        boolean haveDiag = false;
        boolean haveFlush = false;
        boolean haveWipe = false;
        boolean haveOutage = false;
        String[] lines = text.split("\n", -1);
        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];
            if (line.endsWith("\r")) {
                line = line.substring(0, line.length() - 1);
            }
            if (line.length() == 0 || line.charAt(0) == '#') {
                continue;
            }
            int eq = line.indexOf('=');
            if (eq <= 0) {
                return null;
            }
            String k = line.substring(0, eq);
            String v = line.substring(eq + 1);
            if (k.equals("peer_ipv4")) {
                int ip = parseIpv4(v);
                if (ip < 0) {
                    return null;
                }
                c.ipv4Host = ip;
                c.haveIpv4 = true;
            } else if (k.equals("peer_port")) {
                int p = parsePort(v);
                if (p < 0) {
                    return null;
                }
                c.port = p;
                c.havePort = true;
            } else if (k.equals("peer_ek")) {
                byte[] ek = parseHex(v, 1568);
                if (ek == null) {
                    return null;
                }
                c.ek = ek;
                c.haveEk = true;
            } else if (k.equals("witness_id")) {
                byte[] w = parseHex(v, 8);
                if (w == null) {
                    return null;
                }
                c.witness = w;
                c.haveWitness = true;
            } else if (k.equals("diag")) {
                int d = parse01(v);
                if (d < 0) {
                    return null;
                }
                c.diag = d;
                haveDiag = true;
            } else if (k.equals("flush_mode")) {
                if (v.equals("zeroize")) {
                    c.flushMode = FLUSH_ZEROIZE;
                } else if (v.equals("log_only")) {
                    c.flushMode = FLUSH_LOG_ONLY;
                } else {
                    return null;
                }
                haveFlush = true;
            } else if (k.equals("wipe_armed")) {
                int d = parse01(v);
                if (d < 0) {
                    return null;
                }
                c.wipeArmed = d;
                haveWipe = true;
            } else if (k.equals("outage_class")) {
                if (v.equals("normal")) {
                    c.outageClass = OUTAGE_NORMAL;
                } else if (v.equals("maintenance")) {
                    c.outageClass = OUTAGE_MAINTENANCE;
                } else if (v.equals("blackout")) {
                    c.outageClass = OUTAGE_BLACKOUT;
                } else if (v.equals("faraday")) {
                    c.outageClass = OUTAGE_FARADAY;
                } else if (v.equals("capture")) {
                    c.outageClass = OUTAGE_CAPTURE;
                } else {
                    return null;
                }
                haveOutage = true;
            } else if (k.startsWith("hub") && k.length() >= 7) {
                /* hub2_* … hub16_* (DEC-0032). */
                int di = 3;
                int num = 0;
                while (di < k.length() && k.charAt(di) >= '0' && k.charAt(di) <= '9') {
                    num = num * 10 + (k.charAt(di) - '0');
                    if (num > MAX_HUBS) {
                        return null;
                    }
                    di++;
                }
                if (di >= k.length() || k.charAt(di) != '_' || num < 2 || num > MAX_HUBS) {
                    return null;
                }
                int idx = num - 1;
                String field = k.substring(di + 1);
                Hub hub = c.hubs[idx];
                if (field.equals("ipv4")) {
                    int ip = parseIpv4(v);
                    if (ip < 0) {
                        return null;
                    }
                    hub.ipv4Host = ip;
                    hub.haveIpv4 = true;
                } else if (field.equals("port")) {
                    int p = parsePort(v);
                    if (p < 0) {
                        return null;
                    }
                    hub.port = p;
                    hub.havePort = true;
                } else if (field.equals("ek")) {
                    byte[] ek = parseHex(v, 1568);
                    if (ek == null) {
                        return null;
                    }
                    hub.ek = ek;
                    hub.haveEk = true;
                } else {
                    return null;
                }
            } else {
                return null;
            }
        }
        if (!haveDiag) {
            c.diag = 0;
        }
        if (!haveWipe) {
            c.wipeArmed = 0;
        }
        if (!haveFlush) {
            c.flushMode = c.diag == 1 ? FLUSH_LOG_ONLY : FLUSH_ZEROIZE;
        }
        if (!haveOutage) {
            c.outageClass = OUTAGE_NORMAL;
        }
        if (c.flushMode == FLUSH_LOG_ONLY && c.diag == 0) {
            return null;
        }
        c.hubs[0].ipv4Host = c.ipv4Host;
        c.hubs[0].port = c.port;
        c.hubs[0].ek = c.ek;
        c.hubs[0].haveIpv4 = c.haveIpv4;
        c.hubs[0].havePort = c.havePort;
        c.hubs[0].haveEk = c.haveEk;
        boolean gap = false;
        for (int h = 1; h < MAX_HUBS; h++) {
            Hub hub = c.hubs[h];
            int bits = (hub.haveIpv4 ? 1 : 0) + (hub.havePort ? 1 : 0) + (hub.haveEk ? 1 : 0);
            if (bits != 0 && bits != 3) {
                return null;
            }
            if (bits == 0) {
                gap = true;
            } else if (gap) {
                return null;
            }
        }
        return c;
    }

    private static int parse01(String s) {
        if (s == null || s.length() != 1) {
            return -1;
        }
        if (s.charAt(0) == '0') {
            return 0;
        }
        if (s.charAt(0) == '1') {
            return 1;
        }
        return -1;
    }

    private static int parseIpv4(String s) {
        String[] p = s.split("\\.", -1);
        if (p.length != 4) {
            return -1;
        }
        int acc = 0;
        for (int i = 0; i < 4; i++) {
            int o = parseU8(p[i]);
            if (o < 0) {
                return -1;
            }
            acc = (acc << 8) | o;
        }
        return acc;
    }

    private static int parsePort(String s) {
        if (s == null || s.length() == 0 || s.length() > 5) {
            return -1;
        }
        int v = 0;
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch < '0' || ch > '9') {
                return -1;
            }
            v = v * 10 + (ch - '0');
            if (v > 65535) {
                return -1;
            }
        }
        if (v < 1) {
            return -1;
        }
        return v;
    }

    private static int parseU8(String s) {
        if (s == null || s.length() == 0 || s.length() > 3) {
            return -1;
        }
        int v = 0;
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch < '0' || ch > '9') {
                return -1;
            }
            v = v * 10 + (ch - '0');
            if (v > 255) {
                return -1;
            }
        }
        return v;
    }

    private static byte[] parseHex(String s, int outLen) {
        if (s == null || s.length() != outLen * 2) {
            return null;
        }
        byte[] out = new byte[outLen];
        for (int i = 0; i < outLen; i++) {
            int a = hexNib(s.charAt(2 * i));
            int b = hexNib(s.charAt(2 * i + 1));
            if (a < 0 || b < 0) {
                return null;
            }
            out[i] = (byte) ((a << 4) | b);
        }
        return out;
    }

    private static int hexNib(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    }
}
