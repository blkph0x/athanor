package com.athanor.daemon;

/**
 * Lab node file atn-node.conf. DEC-0021. Same keys as atn_cfg.c.
 */
public final class AtnNodeConfig {
    public int ipv4Host;
    public int port;
    public byte[] ek;
    public boolean haveIpv4;
    public boolean havePort;
    public boolean haveEk;

    private AtnNodeConfig() {}

    public boolean ready() {
        return haveIpv4 && havePort && haveEk && ek != null && ek.length == 1568;
    }

    public static AtnNodeConfig parse(String text) {
        AtnNodeConfig c = new AtnNodeConfig();
        if (text == null) {
            return null;
        }
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
            } else {
                return null;
            }
        }
        return c;
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

    private static int parseU8(String s) {
        if (s.length() == 0 || s.length() > 3) {
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

    private static int parsePort(String s) {
        if (s.length() == 0 || s.length() > 5) {
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

    private static byte[] parseHex(String s, int want) {
        if (s.length() != want * 2) {
            return null;
        }
        byte[] out = new byte[want];
        for (int i = 0; i < want; i++) {
            int a = nib(s.charAt(i * 2));
            int b = nib(s.charAt(i * 2 + 1));
            if (a < 0 || b < 0) {
                return null;
            }
            out[i] = (byte) ((a << 4) | b);
        }
        return out;
    }

    private static int nib(char ch) {
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
