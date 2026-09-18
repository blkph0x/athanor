package com.athanor.daemon;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Contact roster (DEC-0050/0054). Sealed in AtnVault when Keystore available;
 * plaintext atn-contacts.conf is legacy migrate-once then removed.
 */
public final class AtnContacts {
    private static final String FILE = "atn-contacts.conf";

    public static final class Entry {
        public String label = "";
        public String ipv4 = "";
        public int port;
        public String ekHex = "";
        public String hubIpv4 = "";
        public int hubPort;
    }

    private AtnContacts() {}

    public static List<Entry> load(Context ctx) {
        List<Entry> out = new ArrayList<Entry>();
        byte[] sealed = AtnVault.get(ctx, AtnVault.CONTACTS);
        if (sealed != null) {
            try {
                String text = new String(sealed, StandardCharsets.UTF_8);
                parseInto(text, out);
            } finally {
                Arrays.fill(sealed, (byte) 0);
            }
            return out;
        }
        /* Legacy plaintext → migrate into vault. */
        File f = new File(ctx.getFilesDir(), FILE);
        if (!f.isFile()) {
            return out;
        }
        try {
            BufferedReader br = new BufferedReader(new InputStreamReader(
                    new FileInputStream(f), StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line).append('\n');
                Entry e = parseLine(line);
                if (e != null) {
                    out.add(e);
                }
            }
            br.close();
            if (out.size() > 0) {
                byte[] raw = sb.toString().getBytes(StandardCharsets.UTF_8);
                if (AtnVault.put(ctx, AtnVault.CONTACTS, raw)) {
                    //noinspection ResultOfMethodCallIgnored
                    f.delete();
                }
                Arrays.fill(raw, (byte) 0);
            }
        } catch (Exception ignored) {
        }
        return out;
    }

    public static boolean save(Context ctx, List<Entry> list) {
        StringBuilder sb = new StringBuilder();
        sb.append("# DEC-0050/0054 contacts (vault)\n");
        if (list != null) {
            for (Entry e : list) {
                if (e == null || e.label == null || e.label.length() == 0) {
                    continue;
                }
                sb.append(e.label).append(' ').append(e.ipv4).append(' ')
                        .append(e.port).append(' ').append(e.ekHex);
                if (e.hubIpv4 != null && e.hubIpv4.length() > 0 && e.hubPort > 0) {
                    sb.append(' ').append(e.hubIpv4).append(' ').append(e.hubPort);
                }
                sb.append('\n');
            }
        }
        byte[] raw = sb.toString().getBytes(StandardCharsets.UTF_8);
        boolean ok = AtnVault.put(ctx, AtnVault.CONTACTS, raw);
        Arrays.fill(raw, (byte) 0);
        if (ok) {
            File legacy = new File(ctx.getFilesDir(), FILE);
            //noinspection ResultOfMethodCallIgnored
            legacy.delete();
            return true;
        }
        /* Fallback plaintext only if vault/Keystore unavailable. */
        try {
            FileOutputStream fos = new FileOutputStream(
                    new File(ctx.getFilesDir(), FILE));
            fos.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            fos.close();
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private static void parseInto(String text, List<Entry> out) {
        String[] lines = text.split("\n");
        for (int i = 0; i < lines.length; i++) {
            Entry e = parseLine(lines[i]);
            if (e != null) {
                out.add(e);
            }
        }
    }

    public static boolean addDemoHub(Context ctx) {
        List<Entry> cur = load(ctx);
        for (Entry e : cur) {
            if ("hub".equals(e.label)) {
                return true;
            }
        }
        Entry e = new Entry();
        e.label = "hub";
        e.ipv4 = "127.0.0.1";
        e.port = 46000;
        e.ekHex = "00"; /* placeholder — replace via enroll */
        cur.add(e);
        return save(ctx, cur);
    }

    static Entry parseLine(String line) {
        if (line == null) {
            return null;
        }
        line = line.trim();
        if (line.length() == 0 || line.charAt(0) == '#') {
            return null;
        }
        String[] p = line.split("\\s+");
        if (p.length < 4) {
            return null;
        }
        Entry e = new Entry();
        e.label = p[0];
        e.ipv4 = p[1];
        try {
            e.port = Integer.parseInt(p[2]);
        } catch (Exception ex) {
            return null;
        }
        e.ekHex = p[3];
        if (p.length >= 6) {
            e.hubIpv4 = p[4];
            try {
                e.hubPort = Integer.parseInt(p[5]);
            } catch (Exception ex) {
                e.hubPort = 0;
            }
        }
        return e;
    }
}
