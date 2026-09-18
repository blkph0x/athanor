/*
 * DEC-0055 mesh text / file announce / chunk wire gates.
 */
#include "atn_mesh.h"
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("ok   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fail++;
    }
}

static void test_text(void)
{
    uint8_t wire[ATN_TUN_MAX_PT];
    size_t wn = 0;
    atn_mesh_text t;
    const char *body = "hello mesh";
    printf("--- mesh text ---\n");
    check("enc text",
          atn_mesh_encode_text("alice", (const uint8_t *)body,
                               (uint16_t)strlen(body), wire, sizeof(wire),
                               &wn) == ATN_OK);
    check("text <= MAX_PT", wn <= ATN_TUN_MAX_PT);
    check("parse text", atn_mesh_parse_text(wire, wn, &t) == ATN_OK);
    check("from", strcmp(t.from, "alice") == 0);
    check("body len", t.body_len == strlen(body));
    check("body", memcmp(t.body, body, t.body_len) == 0);
    {
        uint8_t bad[8] = { 'X', 'T', 1, 'a', 0, 1, 'z', 0 };
        check("bad family", atn_mesh_parse_text(bad, 7, &t) == ATN_ERR_STATE);
    }
}

static void test_file_chunk(void)
{
    uint8_t wire[ATN_TUN_MAX_PT];
    size_t wn = 0;
    atn_mesh_file f, g;
    uint8_t payload[64];
    uint32_t id = 0, off = 0;
    uint16_t len = 0;
    const uint8_t *data = NULL;
    unsigned i;
    printf("--- mesh file/chunk ---\n");
    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 3u);
    }
    atn_memzero(&f, sizeof(f));
    f.file_id = 42;
    f.size = sizeof(payload);
    memcpy(f.name, "note.bin", 9);
    for (i = 0; i < ATN_MESH_SHA_LEN; i++) {
        f.sha256[i] = (uint8_t)(0xa0u + i);
    }
    check("enc file",
          atn_mesh_encode_file(&f, wire, sizeof(wire), &wn) == ATN_OK);
    check("parse file", atn_mesh_parse_file(wire, wn, &g) == ATN_OK);
    check("file id", g.file_id == 42);
    check("file size", g.size == sizeof(payload));
    check("file name", strcmp(g.name, "note.bin") == 0);
    check("file sha", memcmp(g.sha256, f.sha256, ATN_MESH_SHA_LEN) == 0);

    check("enc chunk",
          atn_mesh_encode_chunk(42, 0, payload, (uint16_t)sizeof(payload),
                                wire, sizeof(wire), &wn) == ATN_OK);
    check("parse chunk",
          atn_mesh_parse_chunk(wire, wn, &id, &off, &len, &data) == ATN_OK);
    check("chunk id", id == 42);
    check("chunk off", off == 0);
    check("chunk len", len == sizeof(payload));
    check("chunk data", data != NULL && memcmp(data, payload, len) == 0);
    check("chunk too big",
          atn_mesh_encode_chunk(1, 0, payload, ATN_MESH_CHUNK_MAX + 1u,
                                wire, sizeof(wire), &wn) == ATN_ERR_LEN);
}

int main(void)
{
    printf("athanor mesh  platform=%s\n", atn_platform_id());
    test_text();
    test_file_chunk();
    if (g_fail) {
        printf("%d FAILED\n", g_fail);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
