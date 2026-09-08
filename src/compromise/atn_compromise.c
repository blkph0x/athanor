/*
 * Compromise vote encode/parse (DEC-0047).
 */
#include "atn_compromise.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void atn_compromise_init(atn_compromise *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->timeout_s = 300u;
    c->quorum = 1u;
    c->state = ATN_COMP_STATE_NONE;
}

static int is_ws(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
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

static int state_parse(const char *s, size_t n, uint8_t *out)
{
    if (n == 4 && memcmp(s, "none", 4) == 0) {
        *out = ATN_COMP_STATE_NONE;
        return ATN_OK;
    }
    if (n == 4 && memcmp(s, "open", 4) == 0) {
        *out = ATN_COMP_STATE_OPEN;
        return ATN_OK;
    }
    if (n == 12 && memcmp(s, "boom_pending", 12) == 0) {
        *out = ATN_COMP_STATE_BOOM_PENDING;
        return ATN_OK;
    }
    if (n == 4 && memcmp(s, "done", 4) == 0) {
        *out = ATN_COMP_STATE_DONE;
        return ATN_OK;
    }
    if (n == 7 && memcmp(s, "cleared", 7) == 0) {
        *out = ATN_COMP_STATE_CLEARED;
        return ATN_OK;
    }
    return ATN_ERR_PARAM;
}

static const char *state_name(uint8_t st)
{
    switch (st) {
    case ATN_COMP_STATE_OPEN:
        return "open";
    case ATN_COMP_STATE_BOOM_PENDING:
        return "boom_pending";
    case ATN_COMP_STATE_DONE:
        return "done";
    case ATN_COMP_STATE_CLEARED:
        return "cleared";
    default:
        return "none";
    }
}

static int act_parse(const char *s, size_t n, uint8_t *out)
{
    if (n == 4 && memcmp(s, "open", 4) == 0) {
        *out = ATN_COMP_ACT_OPEN;
        return ATN_OK;
    }
    if (n == 8 && memcmp(s, "vote_yes", 8) == 0) {
        *out = ATN_COMP_ACT_VOTE_YES;
        return ATN_OK;
    }
    if (n == 7 && memcmp(s, "vote_no", 7) == 0) {
        *out = ATN_COMP_ACT_VOTE_NO;
        return ATN_OK;
    }
    if (n == 4 && memcmp(s, "boom", 4) == 0) {
        *out = ATN_COMP_ACT_BOOM;
        return ATN_OK;
    }
    if (n == 5 && memcmp(s, "clear", 5) == 0) {
        *out = ATN_COMP_ACT_CLEAR;
        return ATN_OK;
    }
    return ATN_ERR_PARAM;
}

static const char *act_name(uint8_t a)
{
    switch (a) {
    case ATN_COMP_ACT_OPEN:
        return "open";
    case ATN_COMP_ACT_VOTE_YES:
        return "vote_yes";
    case ATN_COMP_ACT_VOTE_NO:
        return "vote_no";
    case ATN_COMP_ACT_BOOM:
        return "boom";
    case ATN_COMP_ACT_CLEAR:
        return "clear";
    default:
        return "open";
    }
}

int atn_compromise_parse(const char *text, size_t n, atn_compromise *c)
{
    size_t i = 0;
    if (c == NULL || (text == NULL && n > 0)) {
        return ATN_ERR_PARAM;
    }
    atn_compromise_init(c);
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
        if (klen == 7 && memcmp(line, "vote_id", 7) == 0) {
            if (parse_u32(line + vstart, vlen, &c->vote_id) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 12 && memcmp(line, "target_label", 12) == 0) {
            if (vlen == 0 || vlen >= ATN_COMP_LABEL_MAX) {
                return ATN_ERR_PARAM;
            }
            memcpy(c->target_label, line + vstart, vlen);
            c->target_label[vlen] = '\0';
        } else if (klen == 11 && memcmp(line, "opened_unix", 11) == 0) {
            if (parse_u32(line + vstart, vlen, &c->opened_unix) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 9 && memcmp(line, "timeout_s", 9) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK ||
                v < 30u || v > 86400u) {
                return ATN_ERR_PARAM;
            }
            c->timeout_s = v;
        } else if (klen == 3 && memcmp(line, "yes", 3) == 0) {
            if (parse_u32(line + vstart, vlen, &c->yes) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 2 && memcmp(line, "no", 2) == 0) {
            if (parse_u32(line + vstart, vlen, &c->no) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 6 && memcmp(line, "quorum", 6) == 0) {
            uint32_t v;
            if (parse_u32(line + vstart, vlen, &v) != ATN_OK || v < 1u ||
                v > 64u) {
                return ATN_ERR_PARAM;
            }
            c->quorum = v;
        } else if (klen == 5 && memcmp(line, "state", 5) == 0) {
            if (state_parse(line + vstart, vlen, &c->state) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 6 && memcmp(line, "action", 6) == 0) {
            if (act_parse(line + vstart, vlen, &c->action) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else {
            return ATN_ERR_PARAM;
        }
    }
    return ATN_OK;
}

int atn_compromise_load_file(const char *path, atn_compromise *c)
{
    FILE *f;
    char buf[ATN_COMP_MAX_TEXT];
    size_t n;
    if (c == NULL || path == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_compromise_init(c);
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_OK;
    }
    n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    return atn_compromise_parse(buf, n, c);
}

int atn_compromise_save_file(const char *path, const atn_compromise *c)
{
    char text[ATN_COMP_MAX_TEXT];
    size_t n = 0;
    FILE *f;
    if (path == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    if (atn_compromise_encode(c, text, sizeof(text), &n) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return ATN_ERR_PARAM;
    }
    if (fwrite(text, 1, n, f) != n) {
        fclose(f);
        return ATN_ERR_PARAM;
    }
    fclose(f);
    return ATN_OK;
}

int atn_compromise_encode(const atn_compromise *c, char *out, size_t out_cap,
                          size_t *out_n)
{
    int nw;
    if (c == NULL || out == NULL || out_cap < 64u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    nw = snprintf(out, out_cap,
                  "vote_id=%u\n"
                  "target_label=%s\n"
                  "opened_unix=%u\n"
                  "timeout_s=%u\n"
                  "yes=%u\n"
                  "no=%u\n"
                  "quorum=%u\n"
                  "state=%s\n",
                  (unsigned)c->vote_id, c->target_label,
                  (unsigned)c->opened_unix, (unsigned)c->timeout_s,
                  (unsigned)c->yes, (unsigned)c->no, (unsigned)c->quorum,
                  state_name(c->state));
    if (nw < 0 || (size_t)nw >= out_cap) {
        return ATN_ERR_PARAM;
    }
    *out_n = (size_t)nw;
    return ATN_OK;
}

int atn_compromise_encode_wire(const atn_compromise *c, uint8_t act,
                               uint8_t *out, size_t out_cap, size_t *out_n)
{
    char text[ATN_COMP_MAX_TEXT];
    size_t tn = 0;
    int nw;
    if (c == NULL || out == NULL || out_cap < 2u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    nw = snprintf(text, sizeof(text),
                  "action=%s\n"
                  "vote_id=%u\n"
                  "target_label=%s\n"
                  "timeout_s=%u\n"
                  "quorum=%u\n"
                  "yes=%u\n"
                  "state=%s\n",
                  act_name(act), (unsigned)c->vote_id, c->target_label,
                  (unsigned)c->timeout_s, (unsigned)c->quorum,
                  (unsigned)c->yes, state_name(c->state));
    if (nw < 0 || (size_t)nw >= sizeof(text)) {
        return ATN_ERR_PARAM;
    }
    tn = (size_t)nw;
    if (1u + tn > out_cap) {
        return ATN_ERR_PARAM;
    }
    out[0] = (uint8_t)ATN_COMP_WIRE;
    memcpy(out + 1, text, tn);
    *out_n = 1u + tn;
    return ATN_OK;
}

int atn_compromise_parse_wire(const uint8_t *msg, size_t n, atn_compromise *c)
{
    if (msg == NULL || c == NULL || n < 2u) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_COMP_WIRE) {
        return ATN_ERR_PARAM;
    }
    return atn_compromise_parse((const char *)(msg + 1), n - 1u, c);
}

int atn_compromise_timed_out(const atn_compromise *c, uint32_t now_unix)
{
    uint32_t end;
    if (c == NULL || c->state != ATN_COMP_STATE_OPEN || c->opened_unix == 0) {
        return 0;
    }
    end = c->opened_unix + c->timeout_s;
    return now_unix >= end ? 1 : 0;
}

int atn_compromise_quorum_met(const atn_compromise *c)
{
    if (c == NULL || c->state != ATN_COMP_STATE_OPEN) {
        return 0;
    }
    return c->yes >= c->quorum ? 1 : 0;
}
