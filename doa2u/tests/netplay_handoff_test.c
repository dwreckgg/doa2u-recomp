/* netplay_handoff.c: the round trip, the name rules (a remote host must not
 * be able to write outside the session folders), and refusal of damaged or
 * hostile bytes. */
#include "online/netplay_handoff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void fill(np_handoff *h)
{
    static const uint8_t ups[5] = { 1, 2, 3, 4, 5 };
    static const uint8_t meta[3] = { 9, 8, 7 };
    int i;
    memset(h, 0, sizeof *h);
    h->build_stamp = 0x12345678u;
    h->role = NP_HANDOFF_ROLE_JOINER;
    h->battle = 1;
    h->delay = 3;
    h->local_port = 7001;
    h->peer_ip[0] = 203; h->peer_ip[1] = 0; h->peer_ip[2] = 113; h->peer_ip[3] = 7;
    h->peer_port = 7000;
    h->token = 0xCAFEF00Du;
    h->seed = 0x1234ABCDu;
    for (i = 0; i < (int)NP_LAYOUT_MAX; i++) h->joiner_layout[i] = (uint8_t)(i * 3);
    h->joiner_laysel = 2;
    CHECK(np_handoff_add_file(h, "UserData/1BD01AB4D3F6/ups.dat", ups, sizeof ups));
    CHECK(np_handoff_add_file(h, "UserData/1BD01AB4D3F6/SaveMeta.xbx", meta, sizeof meta));
    CHECK(np_handoff_add_file(h, "TitleData/empty.bin", NULL, 0));
}

static void test_round_trip(void)
{
    np_handoff a, b;
    uint32_t need, got;
    uint8_t *buf;
    fill(&a);
    need = np_handoff_pack(&a, NULL, 0);
    CHECK(need > 0);
    buf = (uint8_t *)malloc(need);
    CHECK(np_handoff_pack(&a, buf, need - 1) == 0);                 /* short buffer */
    got = np_handoff_pack(&a, buf, need);
    CHECK(got == need);
    CHECK(np_handoff_unpack(&b, buf, got) == NULL);
    CHECK(b.build_stamp == a.build_stamp && b.role == a.role && b.battle == 1 && b.delay == 3);
    CHECK(b.local_port == 7001 && b.peer_port == 7000 && !memcmp(b.peer_ip, a.peer_ip, 4));
    CHECK(b.token == a.token && b.seed == a.seed);
    CHECK(!memcmp(b.joiner_layout, a.joiner_layout, NP_LAYOUT_MAX) && b.joiner_laysel == 2);
    CHECK(b.n_files == 3);
    CHECK(!strcmp(b.files[0].name, "UserData/1BD01AB4D3F6/ups.dat") && b.files[0].len == 5 &&
          !memcmp(b.files[0].data, a.files[0].data, 5));
    CHECK(!strcmp(b.files[1].name, "UserData/1BD01AB4D3F6/SaveMeta.xbx") && b.files[1].len == 3);
    CHECK(!strcmp(b.files[2].name, "TitleData/empty.bin") && b.files[2].len == 0);

    /* every truncation is refused, and refusal leaves nothing owned */
    {
        uint32_t cut;
        int all_refused = 1;
        for (cut = 0; cut < got; cut++) {
            np_handoff c;
            if (np_handoff_unpack(&c, buf, cut) == NULL) { all_refused = 0; np_handoff_free(&c); }
            else CHECK(c.n_files == 0);
        }
        CHECK(all_refused);
    }
    /* trailing bytes are refused too */
    {
        uint8_t *longer = (uint8_t *)malloc(got + 1);
        np_handoff c;
        memcpy(longer, buf, got);
        longer[got] = 0;
        CHECK(np_handoff_unpack(&c, longer, got + 1) != NULL);
        free(longer);
    }
    /* a wrong magic */
    {
        np_handoff c;
        buf[0] ^= 0xFF;
        CHECK(np_handoff_unpack(&c, buf, got) != NULL);
        buf[0] ^= 0xFF;
    }
    np_handoff_free(&a);
    np_handoff_free(&b);
    CHECK(a.n_files == 0 && b.n_files == 0);
    free(buf);
}

static void test_names(void)
{
    CHECK(np_handoff_name_ok("UserData/1BD01AB4D3F6/ups.dat"));
    CHECK(np_handoff_name_ok("TitleData/a-b_c.bin"));
    CHECK(!np_handoff_name_ok(""));
    CHECK(!np_handoff_name_ok(NULL));
    CHECK(!np_handoff_name_ok("ups.dat"));                          /* not under a session folder */
    CHECK(!np_handoff_name_ok("UserData/"));                        /* no file */
    CHECK(!np_handoff_name_ok("UserData/../x.dat"));
    CHECK(!np_handoff_name_ok("UserData/a/../../x.dat"));
    CHECK(!np_handoff_name_ok("UserData/.."));
    CHECK(!np_handoff_name_ok("UserData/./x"));
    CHECK(!np_handoff_name_ok("/UserData/x"));
    CHECK(!np_handoff_name_ok("C:/UserData/x"));
    CHECK(!np_handoff_name_ok("UserData\\x"));                      /* backslash */
    CHECK(!np_handoff_name_ok("UserData//x"));                      /* empty component */
    CHECK(!np_handoff_name_ok("UserData/x:stream"));                /* alternate data stream */
    CHECK(!np_handoff_name_ok("SaveData/x"));
    CHECK(!np_handoff_name_ok("userdata/x"));                       /* the folder names exactly */
    CHECK(!np_handoff_name_ok("UserData/a/b/c/d"));                 /* deeper than a profile folder */
    {
        char longname[NP_HANDOFF_NAME_MAX + 8];
        memset(longname, 'a', sizeof longname);
        memcpy(longname, "UserData/", 9);
        longname[NP_HANDOFF_NAME_MAX] = 0;                          /* one too long */
        CHECK(!np_handoff_name_ok(longname));
    }
}

static void test_add_limits(void)
{
    np_handoff h;
    uint8_t one = 1;
    unsigned i;
    memset(&h, 0, sizeof h);
    CHECK(!np_handoff_add_file(&h, "UserData/../evil", &one, 1));
    CHECK(!np_handoff_add_file(&h, "UserData/big", &one, NP_HANDOFF_FILE_MAX + 1));
    for (i = 0; i < NP_HANDOFF_MAX_FILES; i++) {
        char n[32];
        sprintf(n, "UserData/f%u", i);
        CHECK(np_handoff_add_file(&h, n, &one, 1));
    }
    CHECK(!np_handoff_add_file(&h, "UserData/one_more", &one, 1));
    CHECK(h.n_files == NP_HANDOFF_MAX_FILES);
    np_handoff_free(&h);
}

/* A packed hand-off whose file name was rewritten to climb out: refused. */
static void test_hostile_name_in_bytes(void)
{
    np_handoff a, b;
    uint32_t n;
    uint8_t *buf, *p;
    memset(&a, 0, sizeof a);
    CHECK(np_handoff_add_file(&a, "UserData/AAAAAAAA.dat", "x", 1));
    n = np_handoff_pack(&a, NULL, 0);
    buf = (uint8_t *)malloc(n);
    CHECK(np_handoff_pack(&a, buf, n) == n);
    for (p = buf; p + 12 <= buf + n; p++)
        if (!memcmp(p, "AAAAAAAA.dat", 12)) { memcpy(p, "../../../evi", 12); break; }
    CHECK(np_handoff_unpack(&b, buf, n) != NULL);
    np_handoff_free(&a);
    free(buf);
}

int main(void)
{
    test_round_trip();
    test_names();
    test_add_limits();
    test_hostile_name_in_bytes();
    printf("netplay_handoff_test: %d failure(s)\n", failures);
    return failures;
}
