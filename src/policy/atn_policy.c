/*
 * Org network + device policy encode/parse (DEC-0045 / DEC-0046).
 */
#include "atn_policy.h"
#include "atn_crypto.h"

#include <stdio.h>
#include <string.h>

void atn_policy_init(atn_policy *p)
{
    if (p == NULL) {
        return;
    }
    p->ver = 0;
    p->diag = 0;
    p->flush_mode = ATN_CFG_FLUSH_ZEROIZE;
    p->wipe_armed = 0;
    p->outage_class = ATN_CFG_OUTAGE_NORMAL;
    p->boom_silence_s = ATN_POLICY_BOOM_SILENCE_DEFAULT;
    p->password_fail_max = ATN_POLICY_FAIL_MAX_DEFAULT;
    p->biometric_allowed = 0;
    p->password_min_len = ATN_POLICY_PWD_MIN_DEFAULT;
    p->usb_data_block = 1;
    p->pwd_deny_check = 1;
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int parse_u32(const char *s, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    size_t i;
    if (n == 0 || n > 10) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return ATN_ERR_PARAM;
        }
        v = v * 10u + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return ATN_OK;
}

static int flush_parse(const char *s, size_t n, uint8_t *out)
{
    if (n == 7 && memcmp(s, "zeroize", 7) == 0) {
        *out = ATN_CFG_FLUSH_ZEROIZE;
        return ATN_OK;
    }
    if (n == 8 && memcmp(s, "log_only", 8) == 0) {
        *out = ATN_CFG_FLUSH_LOG_ONLY;
        return ATN_OK;
    }
    return ATN_ERR_PARAM;
}

static int outage_parse(const char *s, size_t n, uint8_t *out)
{
    if (n == 6 && memcmp(s, "normal", 6) == 0) {
        *out = ATN_CFG_OUTAGE_NORMAL;
        return ATN_OK;
    }
    if (n == 11 && memcmp(s, "maintenance", 11) == 0) {
        *out = ATN_CFG_OUTAGE_MAINTENANCE;
        return ATN_OK;
    }
    if (n == 8 && memcmp(s, "blackout", 8) == 0) {
        *out = ATN_CFG_OUTAGE_BLACKOUT;
        return ATN_OK;
    }
    if (n == 7 && memcmp(s, "faraday", 7) == 0) {
        *out = ATN_CFG_OUTAGE_FARADAY;
        return ATN_OK;
    }
    if (n == 7 && memcmp(s, "capture", 7) == 0) {
        *out = ATN_CFG_OUTAGE_CAPTURE;
        return ATN_OK;
    }
    return ATN_ERR_PARAM;
}

int atn_policy_parse(const char *text, size_t n, atn_policy *p)
{
    size_t i = 0;
    if (p == NULL || (text == NULL && n > 0)) {
        return ATN_ERR_PARAM;
    }
    atn_policy_init(p);
    while (i < n) {
        const char *line;
        size_t ln, eq, klen, vstart, vlen;
        while (i < n && (text[i] == '\r' || text[i] == '\n')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        line = text + i;
        ln = 0;
        while (i + ln < n && text[i + ln] != '\n' && text[i + ln] != '\r') {
            ln++;
        }
        i += ln;
        while (ln > 0 && is_ws(line[ln - 1])) {
            ln--;
        }
        if (ln == 0 || line[0] == '#') {
            continue;
        }
        eq = 0;
        while (eq < ln && line[eq] != '=') {
            eq++;
        }
        if (eq == 0 || eq >= ln) {
            return ATN_ERR_PARAM;
        }
        klen = eq;
        while (klen > 0 && is_ws(line[klen - 1])) {
            klen--;
        }
        vstart = eq + 1;
        while (vstart < ln && is_ws(line[vstart])) {
            vstart++;
        }
        vlen = ln - vstart;
        if (klen == 10 && memcmp(line, "policy_ver", 10) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            p->ver = v;
        } else if (klen == 4 && memcmp(line, "diag", 4) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v > 1u) {
                return ATN_ERR_PARAM;
            }
            p->diag = (uint8_t)v;
        } else if (klen == 10 && memcmp(line, "flush_mode", 10) == 0) {
            if (flush_parse(line + vstart, vlen, &p->flush_mode) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 10 && memcmp(line, "wipe_armed", 10) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v > 1u) {
                return ATN_ERR_PARAM;
            }
            p->wipe_armed = (uint8_t)v;
        } else if (klen == 12 && memcmp(line, "outage_class", 12) == 0) {
            if (outage_parse(line + vstart, vlen, &p->outage_class) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 14 && memcmp(line, "boom_silence_s", 14) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK ||
                v < 5u || v > 86400u) {
                return ATN_ERR_PARAM;
            }
            p->boom_silence_s = v;
        } else if (klen == 17 && memcmp(line, "password_fail_max", 17) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK ||
                v < 1u || v > 20u) {
                return ATN_ERR_PARAM;
            }
            p->password_fail_max = (uint8_t)v;
        } else if (klen == 17 && memcmp(line, "biometric_allowed", 17) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v > 1u) {
                return ATN_ERR_PARAM;
            }
            p->biometric_allowed = (uint8_t)v;
        } else if (klen == 16 && memcmp(line, "password_min_len", 16) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK ||
                v < 8u || v > 64u) {
                return ATN_ERR_PARAM;
            }
            p->password_min_len = (uint8_t)v;
        } else if (klen == 14 && memcmp(line, "usb_data_block", 14) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v > 1u) {
                return ATN_ERR_PARAM;
            }
            p->usb_data_block = (uint8_t)v;
        } else if (klen == 14 && memcmp(line, "pwd_deny_check", 14) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v > 1u) {
                return ATN_ERR_PARAM;
            }
            p->pwd_deny_check = (uint8_t)v;
        } else {
            return ATN_ERR_PARAM;
        }
    }
    if (p->flush_mode == ATN_CFG_FLUSH_LOG_ONLY && !p->diag) {
        return ATN_ERR_PARAM;
    }
    return ATN_OK;
}

int atn_policy_load_file(const char *path, atn_policy *p)
{
    FILE *f;
    char buf[ATN_POLICY_MAX_TEXT];
    size_t n;
    if (p == NULL || path == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_policy_init(p);
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_OK;
    }
    n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    return atn_policy_parse(buf, n, p);
}

int atn_policy_encode(const atn_policy *p, char *out, size_t out_cap,
                      size_t *out_n)
{
    const char *flush;
    const char *outage;
    int nw;
    if (p == NULL || out == NULL || out_cap < 64u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    flush = (p->flush_mode == ATN_CFG_FLUSH_LOG_ONLY) ? "log_only" : "zeroize";
    switch (p->outage_class) {
    case ATN_CFG_OUTAGE_MAINTENANCE:
        outage = "maintenance";
        break;
    case ATN_CFG_OUTAGE_BLACKOUT:
        outage = "blackout";
        break;
    case ATN_CFG_OUTAGE_FARADAY:
        outage = "faraday";
        break;
    case ATN_CFG_OUTAGE_CAPTURE:
        outage = "capture";
        break;
    default:
        outage = "normal";
        break;
    }
    nw = snprintf(out, out_cap,
                  "policy_ver=%u\n"
                  "diag=%u\n"
                  "flush_mode=%s\n"
                  "wipe_armed=%u\n"
                  "outage_class=%s\n"
                  "boom_silence_s=%u\n"
                  "password_fail_max=%u\n"
                  "biometric_allowed=%u\n"
                  "password_min_len=%u\n"
                  "usb_data_block=%u\n"
                  "pwd_deny_check=%u\n",
                  (unsigned)p->ver, (unsigned)p->diag, flush,
                  (unsigned)p->wipe_armed, outage,
                  (unsigned)p->boom_silence_s,
                  (unsigned)p->password_fail_max,
                  (unsigned)p->biometric_allowed,
                  (unsigned)p->password_min_len,
                  (unsigned)p->usb_data_block,
                  (unsigned)p->pwd_deny_check);
    if (nw < 0 || (size_t)nw >= out_cap) {
        return ATN_ERR_PARAM;
    }
    *out_n = (size_t)nw;
    return ATN_OK;
}

int atn_policy_encode_wire(const atn_policy *p, uint8_t *out, size_t out_cap,
                           size_t *out_n)
{
    char text[ATN_POLICY_MAX_TEXT];
    size_t tn = 0;
    if (out == NULL || out_cap < 2u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    if (atn_policy_encode(p, text, sizeof(text), &tn) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    if (1u + tn > out_cap) {
        return ATN_ERR_PARAM;
    }
    out[0] = (uint8_t)ATN_POLICY_WIRE;
    memcpy(out + 1, text, tn);
    *out_n = 1u + tn;
    return ATN_OK;
}

int atn_policy_parse_wire(const uint8_t *msg, size_t n, atn_policy *p)
{
    if (msg == NULL || p == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_POLICY_WIRE) {
        return ATN_ERR_PARAM;
    }
    if (n == 1u || (n == 2u && msg[1] == (uint8_t)'?')) {
        return ATN_ERR_STATE;
    }
    return atn_policy_parse((const char *)(msg + 1), n - 1u, p);
}

void atn_policy_to_cfg(const atn_policy *p, atn_cfg *c)
{
    if (p == NULL || c == NULL) {
        return;
    }
    c->diag = p->diag;
    c->have_diag = 1;
    c->flush_mode = p->flush_mode;
    c->have_flush_mode = 1;
    c->wipe_armed = p->wipe_armed;
    c->have_wipe_armed = 1;
    c->outage_class = p->outage_class;
    c->have_outage = 1;
}
